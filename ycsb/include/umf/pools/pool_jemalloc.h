/*
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

#ifndef UMF_JEMALLOC_MEMORY_POOL_H
#define UMF_JEMALLOC_MEMORY_POOL_H 1

#ifdef __cplusplus
#include <atomic>
extern "C" {
using namespace std;
#else
#include <stdatomic.h>
#endif

#include <limits.h>
#include <assert.h>
#include <stdbool.h>
#include <umf/memory_pool.h>
#include <umf/memory_pool_ops.h>
#include <jemalloc/jemalloc.h>
//#include <../src/memory_pool_internal.h>

#include <umf/base.h>
#include <umf/memory_pool.h>
#include <umf/memory_pool_ops.h>
#include <umf/memory_provider.h>

#include <stdbool.h>

typedef struct umf_memory_pool_t {
    void *pool_priv;
    umf_memory_pool_ops_t ops;
    umf_pool_create_flags_t flags;

    // Memory provider used by the pool.
    umf_memory_provider_handle_t provider;
} umf_memory_pool_t;


/// Maximum number of jemalloc pools that can be live at once. One pool per NUMA
/// node, so this only needs to cover the node count of the largest machine.
#define UMF_JE_MAX_POOLS 16

/// @brief Configuration of Jemalloc Pool
typedef struct umf_jemalloc_pool_params_t {
    /// Set to true if umfMemoryProviderFree() should never be called.
    bool disable_provider_free ;
    /// Number of arenas to create for this pool. 0 selects the default
    /// (UMF_JE_ARENAS_PER_POOL from the environment, else the online CPU count),
    /// which gives one arena per thread up to that many threads.
    unsigned num_arenas;
} umf_jemalloc_pool_params_t;

umf_memory_pool_ops_t *umfJemallocPoolOps(void);


extern __thread unsigned thread_id;
extern atomic_int thread_count;

/// Dense 0-based id for the calling thread, assigned on first use.
inline unsigned __attribute__((always_inline)) tid(){
	if(thread_id==UINT_MAX){
		thread_id = atomic_fetch_add_explicit(&thread_count, 1, memory_order_relaxed);
	}
	return thread_id;
}

typedef struct jemalloc_memory_pool_t {
    umf_memory_provider_handle_t provider;
    unsigned arena_index; // base index of this pool's contiguous arena range
	unsigned num_arenas;  // number of arenas in that range
	unsigned pool_slot;   // index into the per-thread flag cache below
    // set to true if umfMemoryProviderFree() should never be called
    bool disable_provider_free;
} jemalloc_memory_pool_t;


/*
 * Per-thread, per-pool mallocx()/dallocx() flag cache.
 *
 * The flags for a given (thread, pool) pair never change, so they are computed
 * once by umfJemallocBindThread() and then read straight out of TLS. When the
 * pool slot is a compile-time constant -- which it is for the numa<T,k>
 * specializations the numa-clang-tool generates -- the whole fast path folds
 * down to one TLS bit test plus mallocx() with a constant-indexed flag word.
 */
extern __thread int umf_je_alloc_flags[UMF_JE_MAX_POOLS];
extern __thread int umf_je_free_flags[UMF_JE_MAX_POOLS];
extern __thread unsigned umf_je_bound_mask;

/// Slow path: bind the calling thread to `slot`'s arena and give it a tcache.
/// Called at most once per (thread, pool). Not for direct use.
void umfJemallocBindThread(unsigned slot);

/// Fast path taking the pool slot directly (for numa<T,k>, slot == k).
inline void* __attribute__((always_inline))
umfFastJemallocMallocSlot(unsigned slot, size_t size){
	assert(slot < UMF_JE_MAX_POOLS);
	if (__builtin_expect(!((umf_je_bound_mask >> slot) & 1u), 0)) {
		umfJemallocBindThread(slot);
	}
	return mallocx(size, umf_je_alloc_flags[slot]);
}

inline umf_result_t __attribute__((always_inline))
umfFastJemallocFreeSlot(unsigned slot, void* ptr){
	assert(slot < UMF_JE_MAX_POOLS);
	if (ptr != NULL) {
		// A pointer can only reach here after it was allocated from this slot,
		// so the thread is already bound and the flags are live.
		if (__builtin_expect(!((umf_je_bound_mask >> slot) & 1u), 0)) {
			umfJemallocBindThread(slot);
		}
		dallocx(ptr, umf_je_free_flags[slot]);
	}
	return UMF_RESULT_SUCCESS;
}

/// Fast path taking a pool handle, for callers that do not track slots.
/// Costs one extra dependent load over the *Slot form.
inline void* __attribute__((always_inline))
umfFastJemallocMalloc(umf_memory_pool_handle_t hPool, size_t size){
	assert(hPool!=NULL);
    jemalloc_memory_pool_t *je_pool = (jemalloc_memory_pool_t *)((void*)hPool->pool_priv);
	assert(je_pool);
	return umfFastJemallocMallocSlot(je_pool->pool_slot, size);
}

inline umf_result_t __attribute__((always_inline))
umfFastJemallocFree(umf_memory_pool_handle_t hPool, void* ptr){
	assert(hPool!=NULL);
    jemalloc_memory_pool_t *je_pool = (jemalloc_memory_pool_t *)((void*)hPool->pool_priv);
    assert(je_pool);
    return umfFastJemallocFreeSlot(je_pool->pool_slot, ptr);
}



#ifdef __cplusplus
}
#endif

#endif /* UMF_JEMALLOC_MEMORY_POOL_H */
