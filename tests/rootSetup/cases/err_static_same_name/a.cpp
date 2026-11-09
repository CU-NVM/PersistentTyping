#include "rt.hpp"

static persistent<persistent<int>*> counter = new persistent<int>(100);

void work() {
    bump(counter, "a");
}
