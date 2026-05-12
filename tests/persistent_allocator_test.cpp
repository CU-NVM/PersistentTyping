// Smoke test for PersistentAllocator<T>.
//
// Tests the STL allocator contract end-to-end by plugging the allocator
// into two containers:
//   - std::vector<int>: uses allocate/deallocate directly on int.
//   - std::list<int>:   uses rebind + converting constructor to allocate
//                       list nodes (NOT ints) — exercises the parts of
//                       the contract that vector skips.
//
// Build (from tests/):
//   clang++ -std=c++17 -I.. persistent_allocator_test.cpp -o persistent_allocator_test -lpmemobj
//
// Run:
//   rm -f /mnt/pmem-emu/global_persistent_pool
//   ./persistent_allocator_test

#include "../persistentLib/persistenttype.hpp"
#include <cassert>
#include <iostream>
#include <list>
#include <vector>

int main() {
    // Vector — exercises allocate/deallocate/construct on T directly.
    {
        std::vector<int, PersistentAllocator<int>> v;
        for (int i = 0; i < 1000; ++i) v.push_back(i * i);
        assert(v.size() == 1000);
        for (int i = 0; i < 1000; ++i) assert(v[i] == i * i);
    }

    // List — exercises rebind and the converting constructor.
    // std::list<int> internally allocates list nodes, not ints, so it
    // must rebind PersistentAllocator<int> → PersistentAllocator<Node>.
    {
        std::list<int, PersistentAllocator<int>> l;
        for (int i = 0; i < 100; ++i) l.push_back(i);
        int expected = 0;
        for (int x : l) {
            assert(x == expected);
            ++expected;
        }
        assert(expected == 100);
    }

    // Allocator equality — any two PersistentAllocator instances should
    // compare equal regardless of their value_type.
    PersistentAllocator<int> a;
    PersistentAllocator<double> b;
    assert(a == b);
    assert(!(a != b));

    std::cout << "persistent_allocator_test OK\n";
    return 0;
}
