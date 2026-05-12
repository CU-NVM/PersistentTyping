// End-to-end smoke test for persistent<T>.
//
// Covers:
//   1. Primitive specialization with int — default ctor, value ctor,
//      assignment operator, implicit conversion to T&.
//   2. Pointer specialization — operator-> and conversion.
//   3. Class specialization — inheritance, perfect-forwarding ctor, field access.
//   4. operator new / operator delete route through the pmem pool.
//
// Build (from tests/):
//   clang++ -std=c++17 -I.. persistent_test.cpp -o persistent_test -lpmemobj
//
// Run:
//   rm -f /mnt/pmem-emu/global_persistent_pool
//   ./persistent_test

#include "../persistentLib/persistenttype.hpp"
#include "../persistentLib/pmem_allocator.hpp"
#include <cassert>
#include <iostream>

// A non-trivial user-defined type — exercises the class specialization.
struct Point {
    int x;
    int y;
    Point() : x(0), y(0) {}
    Point(int a, int b) : x(a), y(b) {}
    int sum() const { return x + y; }
};

// Helper: is this pointer inside the pmem-mapped pool?
// The pool maps a contiguous region of virtual address space; we can ask
// PMDK for the pool's base and size and check whether ptr is inside.
static bool in_pmem_pool(const void *ptr) {
    extern PMEMobjpool *global_pool;
    if (!global_pool) return false;
    void *base = pmemobj_direct(pmemobj_root(global_pool, 1));
    // base..base+pool_size covers everything in the pool; the root sits
    // near the start, so use that as a coarse anchor.
    auto p = reinterpret_cast<uintptr_t>(ptr);
    auto b = reinterpret_cast<uintptr_t>(base);
    // pool size is at least 1 GB per our header default
    return p >= b - (1UL << 30) && p < b + (1UL << 30);
}

int main() {
    // (1) Primitive specialization — int
    {
        persistent<int> a;           // default ctor → 0
        assert(a == 0);

        persistent<int> b(42);       // value ctor
        assert(b == 42);

        a = 7;                       // operator=
        assert(a == 7);

        int x = a;                   // implicit conversion to T&
        assert(x == 7);

        a = a + 3;                   // arithmetic via conversion
        assert(a == 10);
    }

    // (2) Pointer specialization
    {
        int target = 99;
        persistent<int *> p(&target);
        assert(*p == 99);            // dereference via conversion to int*&
        assert(static_cast<int *>(p) == &target);
    }

    // (3) Class specialization — inherits from Point
    {
        persistent<Point> origin;    // calls Point::Point() through inheritance
        assert(origin.x == 0 && origin.y == 0);

        persistent<Point> q(3, 4);   // perfect-forwarding ctor → Point(3, 4)
        assert(q.x == 3 && q.y == 4);
        assert(q.sum() == 7);        // inherited member function
    }

    // (4) operator new / delete route through pmem
    {
        auto *p = new persistent<int>(123);
        assert(*p == 123);
        assert(in_pmem_pool(p));     // allocated inside the pool
        delete p;                    // routes through pmem_free
    }

    std::cout << "persistent_test OK\n";
    return 0;
}
