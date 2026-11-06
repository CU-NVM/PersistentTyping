#pragma once
#ifndef PERSISTENTTYPE_HPP
#define PERSISTENTTYPE_HPP

#include <utility>
#include <type_traits>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include "pmem_allocator.hpp"


template<typename T>
class PersistentAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    /* rebind + converting constructor */
    template <typename U>
    struct rebind {
        using other = PersistentAllocator<U>;
    };
    template<typename U>
    PersistentAllocator(const PersistentAllocator<U>&) noexcept {}

    /* default constructor + copy constructor */
    PersistentAllocator() noexcept {}         //error handling. returns true if no exception handling is done
    PersistentAllocator(const PersistentAllocator&) noexcept {}

    /* memory allocation and deallocation */
    pointer allocate(size_type n) {
        return static_cast<pointer>(pmem_alloc(n * sizeof(T), alignof(T)));
    }

    void deallocate(pointer p, size_type n) noexcept {
        pmem_free(p);
    }

    /* construct  */
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

};


template <typename T1, typename T2>
bool operator==(const PersistentAllocator<T1>&, const PersistentAllocator<T2>&) noexcept {
    return true; // All instances of PersistentAllocator are considered equal
}

template <typename T1, typename T2>
bool operator!=(const PersistentAllocator<T1>&, const PersistentAllocator<T2>&) noexcept {
    return false; // All instances of PersistentAllocator are considered equal
}


template<typename T, template<typename> class Alloc = PersistentAllocator, typename E = void>
class persistent;

namespace persist_detail {
    // false, but only once T is known, so a static_assert using it fires when
    // the template is instantiated rather than when it is parsed.
    template<typename T> inline constexpr bool always_false = false;

    template<typename T> struct is_persistent : std::false_type {};
    template<typename T, template<typename> class A, typename E>
    struct is_persistent<persistent<T, A, E>> : std::true_type {};
}

// Arithmetic types and enums. Pointers have their own specialisation below,
// and void / nullptr_t have none.
template<typename T, template <typename> class Alloc>
class persistent<T, Alloc, typename std::enable_if<std::is_arithmetic<T>::value || std::is_enum<T>::value>::type>{
public:
    T contents;
    using allocator_type = Alloc<T>;

    inline T load() const
    __attribute__((always_inline)){
        return contents;
    }


    inline void store(T data)
    __attribute__((always_inline)){
        int succ = pmemobj_tx_add_range_direct(&contents, sizeof(contents));
        if(succ != 0) {
            throw std::runtime_error("Failed to add range to transaction");
        }
        contents = data;
    }

    persistent(){store((T)0);};
    persistent(const T& data){store(data);};

    // Read-only conversion. The mutable `operator T&()` is deliberately absent:
    // handing out a raw `T&` would let callers write to `contents` directly,
    // bypassing store()'s pmemobj_tx_add_range_direct snapshot and leaving the
    // write outside the undo log (unrecoverable on abort/crash). Every mutation
    // must go through operator=, the compound-assignment operators, or ++/--.
    inline operator const T&() const {
        return contents;
    }

    persistent& operator++()    { store(load() + 1); return *this; }          // ++x
    T operator++(int)           { T old = load(); store(old + 1); return old; } // x++
    persistent& operator--()    { store(load() - 1); return *this; }          // --x
    T operator--(int)           { T old = load(); store(old - 1); return old; } // x--


    persistent& operator+=(const T& v) { store(load() + v); return *this; }
    persistent& operator-=(const T& v) { store(load() - v); return *this; }
    persistent& operator*=(const T& v) { store(load() * v); return *this; }
    persistent& operator/=(const T& v) { store(load() / v); return *this; }
    persistent& operator%=(const T& v) { store(load() % v); return *this; }
    persistent& operator&=(const T& v) { store(load() & v); return *this; }
    persistent& operator|=(const T& v) { store(load() | v); return *this; }
    persistent& operator^=(const T& v) { store(load() ^ v); return *this; }
    persistent& operator<<=(const T& v){ store(load() << v); return *this; }
    persistent& operator>>=(const T& v){ store(load() >> v); return *this; }

    // operator new/new[] receive a size in BYTES, so they must go straight to
    // pmem_alloc. Routing through allocator_type::allocate() would be wrong:
    // allocate() takes a COUNT and multiplies by sizeof(T) internally, so the
    // byte count would be scaled a second time (4x over-allocation for int,
    // 8x for double, etc.). Correct-by-coincidence only for sizeof(T) == 1.
    // This also matches the operator new/delete the Clang tool emits into
    // generated persistent<T> specializations.
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(T)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(T)); }
    static void  operator delete(void* ptr)     { pmem_free(ptr); }
    static void  operator delete[](void* ptr)   { pmem_free(ptr); }

    persistent& operator=(const T& data){
        store(data);
        return *this;
    }
    persistent& operator=(const persistent& o) { store(o.load()); return *this; }
};


// A pointer that lives in pmem and points into pmem: persistent<U*>, spelled
// pmem_ptr<U> in generated code. It stores the target's PMEMoid, never a raw
// address, so it stays valid when the pool is mapped somewhere else on the
// next run. One PMEMoid and nothing else, with no destructor, so an array of
// them carries no array cookie.
//
// Reads go through the implicit conversion to U*, which is why `nd->next =
// table[k]`, `if (p)`, `p == q` and `delete p` compile with no rewriting.
// Writes go through operator=, which adds the slot to the transaction first.
//
// Rule 2: persistent<> marks only the layer it wraps, so the pointee must be
// persistent<...> itself, before the tool runs as well as after. persistent<C*>
// would be a pmem pointer to a DRAM C, which points at nothing after a restart.
template<typename U, template <typename> class Alloc>
class persistent<U*, Alloc, void> {
    static_assert(!std::is_void<U>::value, "persistent<void*>: a pmem pointer must have a pointee type");
    static_assert(!std::is_function<U>::value, "persistent<F*>: function pointers cannot live in pmem");
    // void and function pointees already failed above; one message is enough.
    static_assert(persist_detail::is_persistent<std::conditional_t<
                      std::is_void<U>::value || std::is_function<U>::value,
                      persistent<int>, std::remove_cv_t<U>>>::value,
                  "persistent<T*>: a pmem pointer's target must be persistent<...> too; "
                  "write persistent<persistent<T>*> (a pmem pointer to a pmem T)");

    PMEMoid oid_;

    // Only a slot that is itself in pmem needs logging; a copy on the DRAM
    // stack does not.
    void snapshot_if_pmem() {
        if (pmem_contains(&oid_)) {
            if (pmemobj_tx_add_range_direct(&oid_, sizeof(oid_)) != 0)
                throw std::runtime_error("pmem_ptr assignment outside a transaction");
        }
    }

    // A pmem pointer to DRAM must never exist. pmemobj_oid() returns OID_NULL
    // for an address outside the pool, which would otherwise store a silent
    // null.
    static PMEMoid oid_of(U* raw) {
        if (raw == nullptr) return OID_NULL;
        PMEMoid oid = pmemobj_oid(raw);
        if (OID_IS_NULL(oid))
            throw std::runtime_error("pmem_ptr: target is not in the persistent pool");
        return oid;
    }

public:
    persistent() : oid_{OID_NULL} {}
    persistent(std::nullptr_t) : oid_{OID_NULL} {}
    persistent(U* raw) : oid_{oid_of(raw)} {}
    persistent(const persistent&) = default;

    U* get() const { return static_cast<U*>(pmemobj_direct(oid_)); }
    operator U*() const { return get(); }
    U& operator*() const { return *get(); }
    U* operator->() const { return get(); }

private:
    // Writes `oid` into the slot, logged. Outside a transaction a slot in
    // pmem gets one of its own, so a root slot assigned from main() (a root
    // is its slot, see root-setup) is still atomic.
    void set(PMEMoid oid) {
        if (pmem_contains(&oid_) && pmemobj_tx_stage() != TX_STAGE_WORK) {
            pmem::obj::transaction::run(pmem_pool(), [&] { set(oid); });
            return;
        }
        snapshot_if_pmem();
        oid_ = oid;
    }

public:
    persistent& operator=(U* raw) {
        set(oid_of(raw));
        return *this;
    }
    persistent& operator=(const persistent& other) {
        set(other.oid_);
        return *this;
    }

    // A slot array (`new pmem_ptr<persistent<T>>[n]`) is itself allocated in pmem.
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(PMEMoid)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(PMEMoid)); }
    static void  operator delete(void* ptr)     { pmem_free(ptr); }
    static void  operator delete[](void* ptr)   { pmem_free(ptr); }
};

template<typename X>
using pmem_ptr = persistent<X*>;

// A class C. Before the tool runs, persistent<C> inherits C so the user's
// program compiles and the tool can parse it. In transformed code every
// reachable C has a generated `template<> class persistent<C>`, so reaching
// this definition means one was never generated and the object would silently
// live in DRAM; PERSIST_TRANSFORMED turns that into a compile error.
template<typename T, template <typename> class Alloc>
class persistent<T, Alloc, typename std::enable_if<std::is_class<T>::value>::type>
#ifndef PERSIST_TRANSFORMED
    : public T
#endif
{
#ifdef PERSIST_TRANSFORMED
    static_assert(persist_detail::always_false<T>,
                  "persistent<C> was never specialised by persist-tool; C is not reachable from __pers_root");
#else
public:
    persistent() {}

    template<typename... Args>
    explicit persistent(Args&&... args) : T(std::forward<Args>(args)...) {}
#endif
};

// Every other T (references, unions, arrays, void, functions) has no
// persistent form.
template<typename T, template <typename> class Alloc, typename E>
class persistent {
    static_assert(persist_detail::always_false<T>,
                  "persistent<T>: T must be an arithmetic type, an enum, a pointer or a class");
};

// A C string in pmem is an array of persistent<char> (P(char*) is
// pmem_ptr<persistent<char>>), but the C library takes char*. persist-tool
// wraps such an argument in one of these; users never write them.
namespace persist_detail {
    static_assert(sizeof(persistent<char>) == 1 && std::is_standard_layout<persistent<char>>::value,
                  "persistent<char> must be laid out exactly like char");

    // Read-only view, for strlen, strcmp, printf, `std::cout <<`, ...
    inline const char* chars(const persistent<char>* p) {
        return reinterpret_cast<const char*>(p);
    }

    // For a C function about to write `n` bytes at `p` (strcpy, memcpy, ...):
    // adds them to the transaction first, as persistent<char>::store would.
    inline char* writable(persistent<char>* p, std::size_t n) {
        if (p && pmem_contains(p) && pmemobj_tx_add_range_direct(p, n) != 0)
            throw std::runtime_error("write to a C string in pmem outside a transaction");
        return reinterpret_cast<char*>(p);
    }
}

// Returns *slot, creating it from `args` in one transaction if it is null.
template<typename T, typename... Args>
T* pmem_get_or_create(persistent<T*>& slot, Args&&... args) {
    if (slot) return slot.get();

    T* obj = nullptr;
    pmem::obj::transaction::run(pmem_pool(), [&]{
        obj = new T(std::forward<Args>(args)...);
        slot = obj;
    });
    return obj;
}
#endif