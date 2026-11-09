#include "rt.hpp"

static persistent<persistent<int>*> counter = new persistent<int>(0);
void work();

int main() {
    work();
    bump(counter, "main");
}
