#include "counter.hpp"
#include "rt.hpp"

persistent<persistent<int>*> counter = new persistent<int>(100);

void work() {
    bump(counter, "a");
}
