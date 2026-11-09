#include "counter.hpp"

persistent<int> runs = 100;              // a scalar root held by value
persistent<Counter> visits(7);           // a class root held by value

int calls() {
    static persistent<long> n;           // a by-value static local, created when first reached
    pmem::obj::transaction::run(pmem_pool(), [&] { n += 1; });
    return n;
}

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] { runs += 1; visits.hits += 1; });
    std::cout << "runs " << runs << " start " << visits.start << " hits " << visits.hits
              << " calls " << calls() << "\n";
}
