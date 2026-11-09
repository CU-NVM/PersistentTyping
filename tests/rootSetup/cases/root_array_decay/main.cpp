#include "rt.hpp"

persistent<persistent<int>*> slots[2];

// The root array can be handed around whole: its slot has the same type as
// the array it replaces, so it decays to a pointer like any C++ array.
static int sum(persistent<persistent<int>*>* all, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += *all[i];
    return s;
}

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] {
        if (!slots[0]) slots[0] = new persistent<int>(1);
        if (!slots[1]) slots[1] = new persistent<int>(2);
    });
    bump(slots[0], "s0");
    std::cout << "sum " << sum(slots, 2) << "\n";
}
