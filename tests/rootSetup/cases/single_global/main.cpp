#include "rt.hpp"

persistent<persistent<int>*> counter;

int main() {
    counter = new persistent<int>(0);
    bump(counter, "counter");
}
