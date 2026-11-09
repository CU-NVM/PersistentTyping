#include "rt.hpp"

int main() {
    static persistent<persistent<int>*> a = new persistent<int>(10);   // initialised in the declaration
    static persistent<persistent<long>*> b = new persistent<long>(20);
    {
        static persistent<persistent<int>*> inner = new persistent<int>(30);   // nested block: still main's
        bump(inner, "inner");
    }
    bump(a, "a");
    bump(b, "b");
}
