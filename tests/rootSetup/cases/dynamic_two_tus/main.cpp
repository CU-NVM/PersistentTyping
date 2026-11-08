#include "counters.hpp"
#include "rt.hpp"

int main() {
    counters = new persistent<persistent<int>*>[2];
    for (int i = 0; i < 2; i++) counters[i] = new persistent<int>(i);
    bump_all(2);
    bump(counters[1], "main");
    std::cout << "objs " << object_count() << "\n";
}
