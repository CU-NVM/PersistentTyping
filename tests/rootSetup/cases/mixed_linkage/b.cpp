#include "rt.hpp"

static persistent<persistent<int>*> b_hits = new persistent<int>(50);   // b.cpp's own, despite the name

void fb() {
    bump(b_hits, "b");
}
