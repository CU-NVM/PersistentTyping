#include "rt.hpp"

static persistent<int>* counter;

void work() {
    counter = new persistent<int>(100);
    bump(counter, "a");
}
