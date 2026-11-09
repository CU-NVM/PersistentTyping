#include "roots.hpp"
#include "rt.hpp"

persistent<persistent<int>*> counter = new persistent<int>(0);

void work() {
    bump(counter, "a");
}
