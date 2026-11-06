#include "rt.hpp"

persistent<int>** counters;

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] {
        counters = new persistent<int>*[4];
        crash_on_run(1);
    });
    std::cout << "objs " << object_count() << "\n";
    for (int i = 0; i < 4; i++) counters[i] = new persistent<int>(i);
    bump(counters[3], "last");
    std::cout << "objs " << object_count() << "\n";
}
