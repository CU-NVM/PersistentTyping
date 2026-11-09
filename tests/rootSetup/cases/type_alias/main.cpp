#include "rt.hpp"

using PInt = persistent<int>;
typedef persistent<long> PLong;

persistent<PInt*> c = new PInt(3);

int main() {
    static persistent<PLong*> d = new PLong(7);
    bump(c, "c");
    bump(d, "d");
}
