#include "rt.hpp"

persistent<persistent<persistent<int>*>*> counters;

int main() {
    int n = 3;
    counters = new persistent<persistent<int>*>[n];
    for (int i = 0; i < n; i++) counters[i] = new persistent<int>(100 * i);
    for (int i = 0; i < n; i++) bump(counters[i], "c");
    std::cout << "objs " << object_count() << "\n";
}
