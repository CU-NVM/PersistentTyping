#include "rt.hpp"

static persistent<int>* counter;
void work();

int main() {
    counter = new persistent<int>(0);
    work();
    bump(counter, "main");
}
