#include "rt.hpp"

static long base() { return 40; }

persistent<persistent<int>*> bare = new persistent<int>;
persistent<persistent<int>*> empty = new persistent<int>();
persistent<persistent<int>*> expr = new persistent<int>(2 + 3 * 4);
persistent<persistent<long>*> call = new persistent<long>(base() + 2);
persistent<persistent<double>*> dbl = new persistent<double>(1.0);


int main() {
    bump(bare, "bare");
    bump(empty, "empty");
    bump(expr, "expr");
    bump(call, "call");
    bump(dbl, "dbl");
}
