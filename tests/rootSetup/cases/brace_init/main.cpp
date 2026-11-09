#include "rt.hpp"

persistent<persistent<int>*> c = new persistent<int>{5};

int main() {
    bump(c, "c");
}
