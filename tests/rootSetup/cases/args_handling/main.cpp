#include "counter.hpp"
#include "rt.hpp"

persistent<int>* counter;

int main() {
    counter = new persistent<int>(0);
    bump(counter, "counter");
}
