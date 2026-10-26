#include "roots.hpp"
#include "rt.hpp"

persistent<int>* counter;

void work() {
    counter = new persistent<int>(0);
    bump(counter, "a");
}
