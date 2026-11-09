#include "rt.hpp"

static int base = 40;                    // file-local: visible to a.cpp's init function
persistent<persistent<int>*> from_a = new persistent<int>(base + 2);

void show_a() { std::cout << "a " << *from_a << "\n"; }
