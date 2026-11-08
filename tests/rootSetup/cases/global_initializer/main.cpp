#include "rt.hpp"

persistent<persistent<int>*> g = new persistent<int>(3);

int main() {
    bump(g, "g");
}
