#pragma once
#ifndef PMEM_STRING_HPP
#define PMEM_STRING_HPP

#include "pmem_allocator.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

// Variable-length null-terminated string with its characters in pmem.
// Design rationale, invariants, and failure modes: see Docs/PersistentLib.md.
class pmem_string {
private:
    PMEMoid data_;
    size_t  size_;

    void snapshot_if_pmem() {
        if (pmem_contains(this)) {
            int succ = pmemobj_tx_add_range_direct(this, sizeof(*this));
            if (succ != 0) {
                throw std::runtime_error("pmem_string mutation outside txn.");
            }
        }
    }

    // Callers must have snapshotted before calling this.
    void release() {
        if (!OID_IS_NULL(data_)) {
            pmem_free(pmemobj_direct(data_));
            data_ = OID_NULL;
            size_ = 0;
        }
    }

public:
    pmem_string() : data_(OID_NULL), size_(0) {}
    pmem_string(const char* s) : data_(OID_NULL), size_(0) { assign(s); }

    // Frees the payload without clearing the fields; clearing would be an
    // unlogged pmem write. See PersistentLib.md.
    ~pmem_string() {
        if (!OID_IS_NULL(data_)) pmem_free(pmemobj_direct(data_));
    }

    pmem_string(const pmem_string&)            = delete;
    pmem_string& operator=(const pmem_string&) = delete;

    // Statement order is load-bearing (throw-before-allocate, then
    // copy-before-release for self-assignment safety).
    void assign(const char* s) {
        size_t n = std::strlen(s);
        snapshot_if_pmem();
        void* raw = pmem_alloc(n + 1, alignof(char));
        if (!raw) throw std::runtime_error("pmem_string: allocation failed");
        std::memcpy(raw, s, n + 1);
        release();
        data_ = pmemobj_oid(raw);
        size_ = n;
    }

    void assign(const std::string& s) { assign(s.c_str()); }

    pmem_string& operator=(const char* s)        { assign(s); return *this; }
    pmem_string& operator=(const std::string& s) { assign(s); return *this; }

    const char* c_str() const {
        return OID_IS_NULL(data_) ? "" : static_cast<const char*>(pmemobj_direct(data_));
    }

    // Read-only implicit conversion, so a field remapped from `char*` still
    // works at every read site (strcmp, ostream<<, any const char* parameter)
    // without the tool having to rewrite it. Safe in a way the deleted
    // `persistent<T>::operator T&()` was not: const, so nothing can be written
    // through it and bypass the undo log.
    operator const char*() const { return c_str(); }

    size_t size()   const { return size_; }
    size_t length() const { return size_; }
    bool   empty()  const { return size_ == 0; }

    bool operator==(const char* s) const { return std::strcmp(c_str(), s) == 0; }
    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator==(const pmem_string& o) const { return *this == o.c_str(); }
    bool operator!=(const pmem_string& o) const { return !(*this == o); }

    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(pmem_string)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(pmem_string)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }
};

#endif
