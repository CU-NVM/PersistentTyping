#include "rt.hpp"

persistent<persistent<int>*> used;

int main() {
    used = new persistent<int>(0);
    bump(used, "used");
}
