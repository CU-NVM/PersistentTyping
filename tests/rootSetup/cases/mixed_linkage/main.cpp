#include "rt.hpp"

void fa();
void fb();

int main() {
    static persistent<persistent<long>*> total = new persistent<long>(7);
    fa();
    fb();
    bump(total, "total");
}
