// rt.hpp
//
// Runtime helper for the root-setup tests: bumps a persistent counter inside
// a transaction and prints it, so successive runs show whether the root was
// reread from the pool (1, 2, 3, ...) or recreated (1, 1, 1, ...).

#pragma once

#include <iostream>
#include "persistenttype.hpp"

template<typename T>
void bump(persistent<T>* p, const char* name) {
    pmem::obj::transaction::run(pmem_pool(), [&] { *p += 1; });
    std::cout << name << " " << p->load() << "\n";
}
