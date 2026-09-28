
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

#include "numa_nodemap.hpp"
#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>

#include <mutex>

#include <iostream>

#ifndef NUMA_NODE_NUM
    #warning "NUMA NODES for je malloc handle is set to 2."
    #define NUMA_NODE_NUM 2
#endif


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
    unsigned phys = numa_node_map(numa);
    result = umfMemspaceCreateFromNumaArray(&phys, 1, &hMemspace);
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

static constexpr unsigned NUM_NODES = NUMA_NODE_NUM;
static_assert(NUM_NODES <= UMF_JE_MAX_POOLS,
              "NUMA_NODE_NUM exceeds UMF_JE_MAX_POOLS; raise it in pool_jemalloc.h");

// inline (not plain) globals: this header is included from several TUs, and one
// shared set of pools is required -- pool slot k must mean logical node k.
inline umf_memory_provider_handle_t NUMA_HANDLES[NUM_NODES]={};
inline umf_memory_pool_handle_t jemalloc_pool[NUM_NODES]={};

inline void umf_alloc_init() {
    // Each including TU registers its own constructor (see below), so guard the
    // body -- creating the pools twice would hand out two slots per node.
    static bool initialized = false;
    if (initialized) { return; }
    initialized = true;

    umf_memspace_handle_t hMemspace= NULL;
    umf_mempolicy_handle_t hPolicy = NULL;
    for (unsigned i = 0; i < NUM_NODES; ++i) {

        unsigned phys = numa_node_map(i);
        auto r = umfMemspaceCreateFromNumaArray(&phys, 1, &hMemspace);
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
    }
}

// Internal linkage, so each TU gets its own .init_array entry; the guard inside
// umf_alloc_init() makes the redundant calls no-ops. Keeping the pools up before
// main() matters because static initializers may already allocate numa<T,k>.
namespace {
__attribute__((constructor)) void umf_alloc_ctor() { umf_alloc_init(); }
}


// Pools are created in logical-node order in umf_alloc_init(), so the pool slot
// the fast path wants is exactly NodeId. Taking the slot form skips the
// hPool->pool_priv->pool_slot chase; with NodeId a compile-time constant (which
// it is in every numa<T,k> specialization the numa-clang-tool generates) this
// collapses to a TLS bit test plus mallocx() with a constant flag word.
//
// These must be inline: they used to be ordinary out-of-line definitions in a
// header, which cost a call per allocation and blocked constant folding.
inline void* umf_alloc(unsigned NodeId, size_t size, size_t allign){
    (void)allign; // jemalloc's size classes already satisfy natural alignment
    void *ptr = umfFastJemallocMallocSlot(NodeId, size);
    assert(ptr && "Could not allocate pool");
    return ptr;
}

inline void umf_free(unsigned NodeId, void* ptr){
    umfFastJemallocFreeSlot(NodeId, ptr);
}
#endif
