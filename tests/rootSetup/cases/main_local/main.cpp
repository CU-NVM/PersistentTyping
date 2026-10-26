#include "rt.hpp"

int main() {
    persistent<int>* a = new persistent<int>(10);   // initialised in the declaration
    persistent<long>* b;
    b = new persistent<long>(20);                   // assigned later
    {
        persistent<int>* inner = new persistent<int>(30);   // nested block: still main's
        bump(inner, "inner");
    }
    bump(a, "a");
    bump(b, "b");
}
