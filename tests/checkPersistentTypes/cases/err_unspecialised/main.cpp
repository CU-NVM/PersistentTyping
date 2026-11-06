#include "persistenttype.hpp"
#include <iostream>

struct Orphan { int x; };

static void stray() {
    persistent<Orphan>* o = new persistent<Orphan>();
    std::cout << o->x << "\n";
}

persistent<int>* counter;

int main() {
    counter = new persistent<int>(0);
    stray();
}
