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

template<typename T, template <typename> class Alloc>
class persistent<T, Alloc, typename std::enable_if<std::is_fundamental<T>::value || std::is_pointer<T>::value>::type>{
public:
    T contents;
    using allocator_type = Alloc<T>;

    inline T load()
    __attribute__((always_inline)){
        return contents;
    }

    inline void store(T data)
    __attribute__((always_inline)){
        contents = data;
    }

    persistent(){store((T)0);};
    persistent(const T& data) : contents(data) {}

    inline operator T&() {
        return contents;
    }

    inline T operator->() {
        static_assert(std::is_pointer<T>::value, "operator-> is only valid for pointer types");
        return load();
    }

    static void* operator new(std::size_t sz){
        allocator_type alloc;
        return alloc.allocate(sz);
    }

    static void* operator new[](std::size_t sz){
        allocator_type alloc;
        return alloc.allocate(sz);
    }

    static void operator delete(void *ptr) {
        allocator_type alloc;
        alloc.deallocate(static_cast<typename allocator_type::pointer>(ptr), 1);
    }
    static void operator delete[](void *ptr) {
        allocator_type alloc;
        alloc.deallocate(static_cast<typename allocator_type::pointer>(ptr), 1);
    }

    persistent& operator=(const T& data){
        store(data);
        return *this;
    }
};


template<typename T, template <typename> class Alloc>
class persistent<T, Alloc, typename std::enable_if<!std::is_fundamental<T>::value && !std::is_pointer<T>::value>::type>: public T{
public:
    persistent() {}

    template<typename... Args>
    explicit persistent(Args&&... args) : T(std::forward<Args>(args)...) {}
};
#endif