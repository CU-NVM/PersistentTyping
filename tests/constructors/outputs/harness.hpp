// harness.hpp
//
// Shared driver for the constructor tests. Each test defines the class under
// test (Widget, ...) and a root class Holder with:
//
//   int  runs;          bumped once per RUN/KILL, proves state is reread
//   void bump();        runs++
//   bool has() const;   whether the object under test exists
//   void make();        allocates the object under test; calls
//                       harness::abort_point() and harness::crash_point_make()
//                       after the `new`, inside the same transaction
//   void show() const;  prints one "STATE ..." line
//
// The constructor under test may call harness::crash_point_ctor() in its body.
//
// The first argument picks the mode; results.py runs them in sequence on
// separate pools. No classes live here, so the tool passes this file through
// untouched; it is copied next to each transformed test.

#pragma once

#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "pmem_allocator.hpp"

namespace harness {

enum Mode { RUN, KILL, ABORT, CRASH_MAKE, CRASH_CTOR, INSPECT };
inline Mode g_mode = RUN;

inline void init(int argc, char** argv) {
    const char* m = argc > 1 ? argv[1] : "run";
    if      (!std::strcmp(m, "run"))        g_mode = RUN;
    else if (!std::strcmp(m, "kill"))       g_mode = KILL;
    else if (!std::strcmp(m, "abort"))      g_mode = ABORT;
    else if (!std::strcmp(m, "crash-make")) g_mode = CRASH_MAKE;
    else if (!std::strcmp(m, "crash-ctor")) g_mode = CRASH_CTOR;
    else if (!std::strcmp(m, "inspect"))    g_mode = INSPECT;
    else {
        std::cerr << "usage: " << argv[0]
                  << " [run|kill|abort|crash-make|crash-ctor|inspect]\n";
        std::exit(2);
    }
}

// Objects allocated in the pool, root object excluded. An aborted or
// crashed construction must leave this unchanged.
inline int object_count() {
    int n = 0;
    PMEMoid oid;
    POBJ_FOREACH(global_pool, oid) n++;
    return n;
}

// Simulated power cut: no destructors, no pmemobj_close, transaction left open.
// PMDK recovers the pool on the next pmemobj_open.
[[noreturn]] inline void die() {
    std::cout.flush();
    std::fflush(nullptr);
    std::raise(SIGKILL);
    std::abort();
}

inline void abort_point()      { if (g_mode == ABORT)      throw std::runtime_error("harness abort"); }
inline void crash_point_make() { if (g_mode == CRASH_MAKE) die(); }
inline void crash_point_ctor() { if (g_mode == CRASH_CTOR) die(); }

template<typename H>
int drive(H* h) {
    switch (g_mode) {
    case RUN:
    case KILL:
        h->bump();
        if (!h->has()) h->make();
        std::cout << "RUNS " << h->runs << "\n";
        h->show();
        std::cout << "OBJS " << object_count() << "\n";
        if (g_mode == KILL) die();
        return 0;

    case ABORT: {
        int before = object_count();
        try {
            h->make();
            std::cout << "NOABORT\n";
        } catch (const std::exception&) {
            std::cout << "ABORTED\n";
        }
        std::cout << "HAS " << h->has() << "\n";
        std::cout << "OBJS " << before << " " << object_count() << "\n";
        return 0;
    }

    case CRASH_MAKE:
    case CRASH_CTOR:
        std::cout << "OBJS " << object_count() << "\n";
        std::cout.flush();
        h->make();
        std::cout << "NOCRASH\n";
        return 0;

    case INSPECT:
        std::cout << "HAS " << h->has() << "\n";
        std::cout << "OBJS " << object_count() << "\n";
        return 0;
    }
    return 1;
}

} // namespace harness
