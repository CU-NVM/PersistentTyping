#include "slots.hpp"
#include "rt.hpp"

persistent<persistent<int>*> slots[2];

int main() {
    slots[0] = new persistent<int>(0);
    slots[1] = new persistent<int>(5);
    fb();
    bump(slots[0], "main");
}
