#include "rt.hpp"

persistent<persistent<int>*> c;

int main() {
    c = new persistent<int>{5};
    bump(c, "c");
}
