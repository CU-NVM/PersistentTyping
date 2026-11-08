#include "rt.hpp"

persistent<persistent<int>*> bare;
persistent<persistent<int>*> empty;
persistent<persistent<int>*> expr;
persistent<persistent<long>*> call;
persistent<persistent<double>*> dbl;

static long base() { return 40; }

int main() {
    bare  = new persistent<int>;
    empty = new persistent<int>();
    expr  = new persistent<int>(2 + 3 * 4);
    call  = new persistent<long>(base() + 2);
    dbl   = new persistent<double>(1.0);
    bump(bare, "bare");
    bump(empty, "empty");
    bump(expr, "expr");
    bump(call, "call");
    bump(dbl, "dbl");
}
