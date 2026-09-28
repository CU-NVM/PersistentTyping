// transformed_user_counter.cpp
//
// WHAT THE COMPILER PRODUCES FROM user_counter.cpp.
//
// The recursive typer (Phase 5) takes the user's minimal form and inserts
// the discoverability machinery needed for the counter to survive across
// process restarts. Specifically:
//
//   1. Aggregates every top-level `persistent<T>*` declaration into a
//      `__pers_root` struct, each as a `pmem_ptr<...>` field. This struct
//      occupies PMDK's pool root slot — a fixed, findable location in pmem.
//
//   2. Rewrites the user's `new persistent<T>(args...)` allocation into a
//      `pmem_get_or_create<...>(root_slot, args...)` call. On the first run
//      this allocates the object and records its OID in the root slot; on
//      every subsequent run it finds the existing object via that slot.
//
//   3. Leaves the user's `transaction::run` blocks untouched — the user
//      already wrote those, and the typer only adds what was missing.
//
// This file COMPILES, RUNS, AND PERSISTS. Each invocation increments the
// same counter found in pmem from the previous run.
//
// Build:
//   clang++ -std=c++17 -I../../persistentLib transformed_user_counter.cpp -o transformed_user_counter -lpmemobj
//
// Run (after `rm -f /mnt/pmem-emu/global_persistent_pool` to start fresh):
//   ./transformed_user_counter   # counter = 1
//   ./transformed_user_counter   # counter = 2
//   ./transformed_user_counter   # counter = 3
//
// Compare with: ./user_counter (which compiles but always prints 1).

#include "persistenttype.hpp"
#include <iostream>

// ---------------------------------------------------------------------------
// TYPER-GENERATED: one __pers_root per program, aggregating every top-level
// `persistent<T>*` declaration. Lives at the pool's root slot (`pmemobj_root`).
// ---------------------------------------------------------------------------
struct __pers_root {
    pmem_ptr<persistent<int>> counter;
};

// TYPER-GENERATED: global root pointer, initialized once at program startup.
// Safe at file scope because pmem_alloc_init() carries __attribute__((constructor)),
// which the linker runs before any C++ static-storage initialization. So the
// pool is already open by the time pmem_root<>() is called here.
__pers_root* __root = pmem_root<__pers_root>();
// ---------------------------------------------------------------------------

int main() {
    // TYPER-REWRITTEN: `persistent<int>* counter = new persistent<int>(0)`
    // becomes a find-or-create through the corresponding slot. References the
    // global __root above.
    persistent<int>* counter =
        pmem_get_or_create<persistent<int>>(__root->counter, 0);

    // UNCHANGED FROM USER CODE: the user's transaction block carries through
    // verbatim. The only edit is that the `new` was hoisted out and replaced.
    pmem::obj::transaction::run(pmem_pool(), [&]{
        *counter = *counter + 1;
    });

    std::cout << "counter = " << *counter << "\n";
    return 0;
}
