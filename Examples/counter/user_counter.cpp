// user_counter.cpp
//
// THE USER-EXPOSED MINIMAL FORM.
//
// This is what the human writes. There is no Root struct, no PMEMoid, no
// find-or-create branch — those are the typer's responsibility (Phase 5).
// The user writes only:
//
//   - a `persistent<T>*` declaration via `new persistent<T>(...)`
//   - transaction-wrapped writes
//
// This file COMPILES AND RUNS by itself, but it does NOT persist across runs.
// Every invocation allocates a brand-new persistent<int> at some new offset
// in the pool, increments it from 0 to 1, prints "counter = 1", and exits —
// leaving the previous run's counter orphaned in pmem.
//
// The lack of persistence is intentional. The point of this file is to
// demonstrate that the user-facing source is valid C++ that the recursive
// typer can later analyze and rewrite into the persisting form
// (see transformed_user_counter.cpp in this directory).
//
// Build:
//   clang++ -std=c++17 -I../../persistentLib user_counter.cpp -o user_counter -lpmemobj
//
// Run:
//   ./user_counter           # prints "counter = 1"
//   ./user_counter           # still prints "counter = 1" — no persistence
//
// Compare with: ./transformed_user_counter (which DOES persist).

#include "../../persistentLib/persistenttype.hpp"
#include <iostream>

int main() {
    persistent<int>* counter;

    pmem::obj::transaction::run(pmem_pool(), [&]{
        counter = new persistent<int>(0);
        *counter = *counter + 1;
    });

    std::cout << "counter = " << *counter << "\n";
    return 0;
}
