// Smoke test for persistentLib/pmem_allocator.hpp.
//
// Build:
//   clang++ -std=c++17 -I.. pmem_allocator_test.cpp -o pmem_allocator_test -lpmemobj
//
// Run (from tests/):
//   rm -f /mnt/pmem-emu/global_persistent_pool
//   ./pmem_allocator_test

#include "../persistentLib/pmem_allocator.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    void *p = pmem_alloc(64, 8);
    assert(p != nullptr);

    std::memset(p, 0xab, 64);
    for (int i = 0; i < 64; ++i) {
        assert(static_cast<unsigned char *>(p)[i] == 0xab);
    }

    pmem_free(p);

    std::cout << "pmem_allocator_test OK\n";
    return 0;
}
