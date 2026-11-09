#include "rt.hpp"

int f(int)    { static persistent<int> cache = 10; pmem::obj::transaction::run(pmem_pool(), [&] { cache += 1; }); return cache; }
int f(double) { static persistent<int> cache = 20; pmem::obj::transaction::run(pmem_pool(), [&] { cache += 1; }); return cache; }
int g()       { static persistent<int> cache = 30; pmem::obj::transaction::run(pmem_pool(), [&] { cache += 1; }); return cache; }

int main() {
    std::cout << "f(int) " << f(1) << " f(double) " << f(1.0) << " g " << g() << "\n";
}
