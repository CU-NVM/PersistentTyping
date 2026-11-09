#include "counters.hpp"
#include "rt.hpp"

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] {
        for (int i = 0; i < 2; i++) if (!counters[i]) counters[i] = new persistent<int>(i);
    });
    bump_all(2);
    bump(counters[1], "main");
    std::cout << "objs " << object_count() << "\n";
}
