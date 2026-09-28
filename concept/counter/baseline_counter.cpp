// baseline_counter.cpp
//
// WHAT THE USER WOULD WRITE WITH RAW PMDK — no persistent<T> type, no
// pmem_ptr, no pmem_get_or_create, no persistify tool.
//
// Compare against:
//   - counter/user_counter.cpp — the minimal user-facing form (our typing
//     system's input; requires the tool).
//   - Output/counter/user_counter.cpp — what our tool produces from the above.
//
// Every persistent primitive is explicit here:
//   - pool<root>: the PMDK pool object.
//   - root struct: aggregates persistent-reachable state (equivalent to our
//     __pers_root but hand-written).
//   - persistent_ptr<T>: PMDK's OID-backed smart pointer.
//   - p<T>: PMDK's scalar wrapper with transactional store hooks.
//   - transaction::run: the caller's responsibility to wrap every write.
//   - pool::open vs pool::create: manual find-or-create at startup.
//
// This file COMPILES, RUNS, AND PERSISTS. Each invocation increments the
// same counter across process restarts.
//
// Build:
//   clang++ -std=c++17 baseline_counter.cpp -o baseline_counter -lpmemobj
//
// Run (after `rm -f /mnt/pmem-emu/baseline_counter.pool` to start fresh):
//   ./baseline_counter    # counter = 1
//   ./baseline_counter    # counter = 2
//   ./baseline_counter    # counter = 3

#include <iostream>
#include <filesystem>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/transaction.hpp>

using namespace pmem::obj;

constexpr const char* POOL_PATH = "/mnt/pmem-emu/baseline_counter.pool";
constexpr const char* POOL_LAYOUT = "baseline_counter";
constexpr size_t POOL_SIZE = 1024 * 1024 * 8;   // 8 MiB (PMDK minimum)

// Aggregates all persistent-reachable state. Lives at PMDK's pool root slot,
// which is the only findable location across process restarts.
struct root {
    persistent_ptr<p<int>> counter;
};

int main() {
    // Manual find-or-create for the pool file itself.
    pool<root> pop;
    if (std::filesystem::exists(POOL_PATH)) {
        pop = pool<root>::open(POOL_PATH, POOL_LAYOUT);
    } else {
        pop = pool<root>::create(POOL_PATH, POOL_LAYOUT, POOL_SIZE, 0666);
    }

    auto r = pop.root();

    // Manual find-or-create for the counter object itself, plus the increment.
    // The whole thing must be transactional: allocation and increment are one
    // undo-log unit, so a crash mid-run rolls back cleanly.
    transaction::run(pop, [&] {
        if (r->counter == nullptr) {
            r->counter = make_persistent<p<int>>(0);
        }
        *(r->counter) = *(r->counter) + 1;
    });

    std::cout << "counter = " << *(r->counter) << "\n";
    pop.close();
    return 0;
}
