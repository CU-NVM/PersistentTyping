#include "slots.hpp"
#include "rt.hpp"

persistent<persistent<int>*> slots[2];

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] {
        if (!slots[0]) slots[0] = new persistent<int>(0);
        if (!slots[1]) slots[1] = new persistent<int>(5);
    });
    fb();
    bump(slots[0], "main");
}
