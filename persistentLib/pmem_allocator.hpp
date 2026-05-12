#pragma once

#ifndef _PMEM_ALLOCATOR_HPP_
#define _PMEM_ALLOCATOR_HPP_


#include<iostream>
#include <libpmemobj.h>
#include <unistd.h>
#include <stdexcept>


#define PMEM_POOL_SIZE (1024 * 1024 * 1024) // 1GB  
#define PMEM_POOL_PATH "/mnt/pmem-emu/global_persistent_pool"

inline PMEMobjpool *global_pool = nullptr;

// Helper function to get the pool path from environment variable or use default
inline const char* get_pool_path() {
    const char *p = getenv("PERSISTENT_POOL_PATH");
    return p ? p : PMEM_POOL_PATH;
}


__attribute__((constructor))
inline void pmem_alloc_init() {
    if (access(get_pool_path(), F_OK) == 0) {
        // Pool already exists, open it
        global_pool = pmemobj_open(get_pool_path(), "pmem_pool");
        if (global_pool == NULL) {
            throw std::runtime_error("Failed to open existing PMEM pool");  
        }
    }
    else{
        // Pool doesn't exist, create it
        global_pool = pmemobj_create(get_pool_path(), "pmem_pool", PMEM_POOL_SIZE, 0666);
        if (global_pool == NULL) {
            throw std::runtime_error("Failed to create PMEM pool");
        }
    }
}

__attribute__((destructor))
inline void pmem_alloc_fini() {
    if (global_pool != nullptr) {
        pmemobj_close(global_pool);
        global_pool = nullptr;
    }
}


inline void* pmem_alloc(size_t size, size_t align){
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    PMEMoid oid;
    int succ = pmemobj_alloc(global_pool, &oid, size, 0, NULL, NULL);
    if(succ != 0) {
        throw std::runtime_error("Failed to allocate memory from PMEM pool");
    }
    return pmemobj_direct(oid);
}

inline void pmem_free(void* ptr) {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    PMEMoid oid = pmemobj_oid(ptr);
    pmemobj_free(&oid);
}

#endif // _PMEM_ALLOCATOR_HPP_