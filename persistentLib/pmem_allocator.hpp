#pragma once

#ifndef _PMEM_ALLOCATOR_HPP_
#define _PMEM_ALLOCATOR_HPP_


#include <iostream>
#include <libpmemobj.h>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/transaction.hpp>
#include <unistd.h>
#include <stdexcept>
#include <string>


#define PMEM_POOL_SIZE (1024 * 1024 * 1024) // 1GB  
#define PMEM_POOL_PATH "/mnt/pmem-emu/global_persistent_pool"

inline PMEMobjpool *global_pool = nullptr;
inline pmem::obj::pool_base pmem_pool_handle;

// Helper function to get the pool path from environment variable or use default
inline const char* get_pool_path() {
    const char *p = getenv("PERSISTENT_POOL_PATH");
    return p ? p : PMEM_POOL_PATH;
}


__attribute__((constructor))
inline void pmem_alloc_init() {
    // Every TU that includes this header registers its own copy of this
    // constructor, so it runs once per TU; only the first may open the pool.
    if (global_pool != nullptr) return;
    if (access(get_pool_path(), F_OK) == 0) {
        // Pool already exists, open it
        global_pool = pmemobj_open(get_pool_path(), "pmem_pool");
        if (global_pool == NULL) {
            throw std::runtime_error("Failed to open existing PMEM pool");  
        }
        pmem_pool_handle = pmem::obj::pool_base(global_pool);
    }
    else{
        // Pool doesn't exist, create it
        global_pool = pmemobj_create(get_pool_path(), "pmem_pool", PMEM_POOL_SIZE, 0666);
        if (global_pool == NULL) {
            throw std::runtime_error("Failed to create PMEM pool");
        }
        pmem_pool_handle = pmem::obj::pool_base(global_pool);
    }
}

__attribute__((destructor))
inline void pmem_alloc_fini() {
    if (global_pool != nullptr) {
        pmem_pool_handle = pmem::obj::pool_base{}; // Reset to default-constructed state
        pmemobj_close(global_pool);
        global_pool = nullptr;
    }

}

/* function templates are inline by default*/
template<typename T> T* pmem_root() {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    PMEMoid root_oid = pmemobj_root(global_pool, sizeof(T));
    return static_cast<T*>(pmemobj_direct(root_oid));
}


inline pmem::obj::pool_base& pmem_pool() {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    return pmem_pool_handle;
}



// Every write, allocation and free in the pool needs an active transaction,
// which the caller provides: a method's own, pmem_get_or_create's, or the
// user's transaction::run (closure doc rule 5b). Opening one here instead
// would make each write atomic on its own but not the operation it belongs
// to, and would commit an allocation before it is linked anywhere. PMDK's
// transactional calls abort the process outside a transaction, so this checks
// first and throws an error the program can see.
inline void pmem_require_tx(const char* what) {
    if (pmemobj_tx_stage() != TX_STAGE_WORK)
        throw std::runtime_error(std::string(what) + " outside a transaction");
}

inline void* pmem_alloc(size_t size, size_t align){
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    pmem_require_tx("allocation in the persistent pool");
    // An abort of the transaction frees it, so a crash before the object is
    // linked from a root leaks nothing.
    PMEMoid oid = pmemobj_tx_alloc(size, 0);
    if (OID_IS_NULL(oid)) {
        throw std::runtime_error("Failed to allocate memory from PMEM pool");
    }
    return pmemobj_direct(oid);
}

inline void pmem_free(void* ptr) {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    pmem_require_tx("delete of a persistent object");
    // Takes effect only when the transaction commits.
    if (pmemobj_tx_free(pmemobj_oid(ptr)) != 0) {
        throw std::runtime_error("Failed to free memory from PMEM pool");
    }
}

inline bool pmem_contains(void* ptr) {
    if(global_pool == nullptr) {
        return false;
    }
    return pmemobj_pool_by_ptr(ptr) == global_pool;
}



// pmem_ptr<T> and pmem_get_or_create are in persistenttype.hpp: pmem_ptr<T>
// is now persistent<T*>.

#endif // _PMEM_ALLOCATOR_HPP_