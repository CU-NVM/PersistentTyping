// rt.hpp
//
// Runtime helper for the root-setup tests: bumps a persistent counter inside
// a transaction and prints it, so successive runs show whether the root was
// reread from the pool (1, 2, 3, ...) or recreated (1, 1, 1, ...).

#pragma once

#include <csignal>
#include <cstdlib>
#include <iostream>
#include "persistenttype.hpp"

template<typename T>
void bump(persistent<T>* p, const char* name) {
    pmem::obj::transaction::run(pmem_pool(), [&] { *p += 1; });
    std::cout << name << " " << p->load() << "\n";
}

// Objects in the pool, the root object excluded.
inline int object_count() {
    int n = 0;
    PMEMoid oid;
    POBJ_FOREACH(global_pool, oid) n++;
    return n;
}

// Simulated power cut on run `n` of the case (see `crash:` in run_tests.py):
// no destructors, no pmemobj_close, any open transaction left open.
inline void crash_on_run(int n) {
    const char* run = std::getenv("PERSIST_RUN");
    if (run && std::atoi(run) == n) {
        std::cout.flush();
        std::raise(SIGKILL);
    }
}
