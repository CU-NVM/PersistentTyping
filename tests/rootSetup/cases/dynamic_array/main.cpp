#include "rt.hpp"

static const int n = 3;
persistent<persistent<persistent<int>*>*> counters = new persistent<persistent<int>*>[n];

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] {
        for (int i = 0; i < n; i++) if (!counters[i]) counters[i] = new persistent<int>(100 * i);
    });
    for (int i = 0; i < n; i++) bump(counters[i], "c");
    std::cout << "objs " << object_count() << "\n";
}
