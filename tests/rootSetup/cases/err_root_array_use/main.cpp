#include "rt.hpp"

persistent<int>* slots[2];

static void use() {
    persistent<int>** all = slots;
    (void)all;
}

int main() { use(); }
