#include "rt.hpp"

persistent<persistent<int>*> latest;     // starts null; no initializer

// An ordinary assignment to the slot: every run stores a new object, and the
// pool grows by one each time (the old object is left behind on purpose).
void record(int v) { latest = new persistent<int>(v); }

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] { record(object_count() + 1); });
    std::cout << "latest " << *latest << " objs " << object_count() << "\n";
}
