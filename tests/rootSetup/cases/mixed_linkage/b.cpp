#include "rt.hpp"

static persistent<int>* hits;   // b.cpp's own, despite the name

void fb() {
    hits = new persistent<int>(50);
    bump(hits, "b");
}
