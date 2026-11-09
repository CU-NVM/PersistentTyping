#include "rt.hpp"

persistent<persistent<persistent<int>*>*> counters;

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] {
        if (!counters) counters = new persistent<persistent<int>*>[4];
        crash_on_run(1);
    });
    std::cout << "objs " << object_count() << "\n";
    pmem::obj::transaction::run(pmem_pool(), [&] {
        for (int i = 0; i < 4; i++) if (!counters[i]) counters[i] = new persistent<int>(i);
    });
    bump(counters[3], "last");
    std::cout << "objs " << object_count() << "\n";
}
