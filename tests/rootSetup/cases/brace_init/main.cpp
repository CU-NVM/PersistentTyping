#include "rt.hpp"

persistent<int>* c;

int main() {
    c = new persistent<int>{5};
    bump(c, "c");
}
