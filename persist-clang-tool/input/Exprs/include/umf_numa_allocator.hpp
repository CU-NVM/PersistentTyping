
#pragma once
#ifndef _UMF_NUMA_ALLOCATOR_HPP_
#define _UMF_NUMA_ALLOCATOR_HPP_

#include <umf/mempolicy.h>
#include <umf/memspace.h>
#include <jemalloc/jemalloc.h>
#include <umf/pools/pool_jemalloc.h>

#include <stdbool.h>
#include <cassert>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <umf/ipc.h>
#include <umf/memory_pool.h>
#include <umf/pools/pool_proxy.h>
#include <umf/pools/pool_scalable.h>
#include <umf/providers/provider_level_zero.h>
#include <umf/providers/provider_os_memory.h>

#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>

#include <mutex>

#include <iostream>

// Function to create a memory provider which allocates memory from the specified NUMA node
// by using umfMemspaceCreateFromNumaArray
int createMemoryProviderFromArray(umf_memory_provider_handle_t *hProvider,
                                  unsigned numa) {
    int ret = 0;
    umf_result_t result;
    umf_memspace_handle_t hMemspace = NULL;
    umf_mempolicy_handle_t hPolicy = NULL;

    // Create a memspace - memspace is a list of memory sources.
    // In this example, we create a memspace that contains single numa node;
    result = umfMemspaceCreateFromNumaArray(&numa, 1, &hMemspace);
    if (result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "umfMemspaceCreateFromNumaArray() failed.\n");
        return -1;
    }

    // Create a mempolicy - mempolicy defines how we want to use memory from memspace.
    // In this example, we want to bind memory to the specified numa node.
    result = umfMempolicyCreate(UMF_MEMPOLICY_BIND, &hPolicy);
    if (result != UMF_RESULT_SUCCESS) {
        ret = -1;
        fprintf(stderr, "umfMempolicyCreate failed().\n");
        goto error_memspace;
    }

    // Create a memory provider using the memory space and memory policy
    result = umfMemoryProviderCreateFromMemspace(hMemspace, hPolicy, hProvider);
    if (result != UMF_RESULT_SUCCESS) {
        ret = -1;
        fprintf(stderr, "umfMemoryProviderCreateFromMemspace failed().\n");
        goto error_mempolicy;
    }

    // After creating the memory provider, we can destroy the memspace and mempolicy
error_mempolicy:
    umfMempolicyDestroy(hPolicy);
error_memspace:
    umfMemspaceDestroy(hMemspace);
    return ret;
}

static constexpr unsigned NUM_NODES = 2;
static umf_memory_provider_handle_t NUMA_HANDLES[NUM_NODES]={};
umf_memory_pool_handle_t jemalloc_pool[NUM_NODES]={};
    
static std::mutex umf_lock[NUM_NODES];

__attribute__((constructor))
void umf_alloc_init() {
    void* ptr = NULL;
    umf_memspace_handle_t hMemspace= NULL;
    umf_mempolicy_handle_t hPolicy = NULL;
    for (unsigned i = 0; i < NUM_NODES; ++i) {
        auto r = umfMemspaceCreateFromNumaArray(&i, 1, &hMemspace);
        if (r != UMF_RESULT_SUCCESS) {
            throw std::runtime_error("Could not create space");
        }
        auto policy = umfMempolicyCreate(UMF_MEMPOLICY_BIND, &hPolicy);
        if (policy != UMF_RESULT_SUCCESS) {
            throw std::runtime_error("Could not create policy");
        }
        auto h = umfMemoryProviderCreateFromMemspace(hMemspace, hPolicy, &NUMA_HANDLES[i]);
        if (h != UMF_RESULT_SUCCESS) {
            throw std::runtime_error("Could not create policy");
        }
        auto pool = umfPoolCreate(umfJemallocPoolOps(), NUMA_HANDLES[i], NULL,  UMF_POOL_CREATE_FLAG_DISABLE_TRACKING, &jemalloc_pool[i]);
        if(pool != UMF_RESULT_SUCCESS){
            throw std::runtime_error("Could not create pool");
        }
        umfMempolicyDestroy(hPolicy);
        umfMemspaceDestroy(hMemspace);
        // size_t sz;
        // unsigned narenas;
        // bool tcache;
        // size_t tcache_max;
    
        // sz = sizeof(unsigned);
        // printf("sz: %zu\n", sz);  
        // mallctl("opt.narenas", &narenas, &sz, NULL, 0);
       
        // mallctl("opt.tcache_max", &tcache_max, &sz, NULL, 0);
        // printf("Number of arenas: %u\n", narenas);
        // printf("Thread cache enabled: %s\n", tcache ? "yes" : "no");
        // printf("Thread cache max: %zu\n", tcache_max);

        // ptr = umfPoolAlignedMalloc(jemalloc_pool[i], 100*1024*1024, sizeof(char));
        // printf("Allocated %u bytes\n", 100*1024*1024);
        // if(ptr == NULL){
        //     throw std::runtime_error("Could not allocate pool");
        // }
        // for(int j = 0; j < 100*1024*1024; j+=4096){
        //     ((char*)ptr)[j] = 'a';
        // }
        // if(umfPoolFree(jemalloc_pool[i], ptr) != UMF_RESULT_SUCCESS){
        //     throw std::runtime_error("Could not free pool");
        // }
    }
}


void* umf_alloc(unsigned NodeId, size_t size, size_t allign){
    // umf_lock[NodeId].lock();
    void *ptr = NULL;
    //ptr = umfPoolMalloc(jemalloc_pool[NodeId], size);
    ptr = umfFastJemallocMalloc(jemalloc_pool[NodeId], size);
    // printf("Allocated %u bytes\n", size);
    assert(ptr && "Could not allocate pool");
    //umf_result_t ret = umfMemoryProviderAlloc(NUMA_HANDLES[NodeId], size, allign, &ptr);
    // umf_lock[NodeId].unlock();
    // if (ret==UMF_RESULT_SUCCESS){
    //     throw std::runtime_error("Could not allocate pool");
    // }
    return ptr;
}

void umf_free(unsigned NodeId,void* ptr){
    if(umfFastJemallocFree(jemalloc_pool[NodeId], ptr) != UMF_RESULT_SUCCESS){
        throw std::runtime_error("Could not free pool");
    }
    //umfMemoryProviderDestroy(NUMA_HANDLES[NodeId]);
}
#endif