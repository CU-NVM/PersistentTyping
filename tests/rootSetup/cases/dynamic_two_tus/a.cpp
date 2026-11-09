#include "counters.hpp"
#include "rt.hpp"

persistent<persistent<persistent<int>*>*> counters = new persistent<persistent<int>*>[2];

void bump_all(int n) {
    for (int i = 0; i < n; i++) bump(counters[i], "a");
}
