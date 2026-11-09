#include "rt.hpp"

persistent<persistent<int>*> used = new persistent<int>(0);

int main() {
    bump(used, "used");
}
