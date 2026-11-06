#include "rt.hpp"

persistent<int>** counters;

int main() {
    counters = new persistent<int>*[3];
    for (int i = 0; i < 3; i++) counters[i] = new persistent<int>(i);
    long at = (long)pmemobj_oid(&counters[0]).off;
    persistent<long>* first_at = new persistent<long>(at);
    std::cout << "same " << (first_at->load() == at) << "\n";
    std::cout << "objs " << object_count() << "\n";
}
