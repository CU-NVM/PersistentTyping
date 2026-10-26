#include "rt.hpp"

using PInt = persistent<int>;
typedef persistent<long> PLong;

PInt* c;

int main() {
    PLong* d = new PLong(7);
    c = new PInt(3);
    bump(c, "c");
    bump(d, "d");
}
