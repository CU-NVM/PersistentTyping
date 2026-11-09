#include "roots.hpp"

persistent<persistent<Counter>*> visits = new persistent<Counter>();

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] { visits->hits += 1; });
    std::cout << "hits " << visits->hits << " b " << hits_in_b() << "\n";
}
