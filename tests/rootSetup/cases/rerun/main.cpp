#include "rt.hpp"

persistent<persistent<int>*> counter = new persistent<int>(0);

int main() {
    bump(counter, "counter");
}
