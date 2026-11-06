#include "persistenttype.hpp"
#include <iostream>

struct Outer {
    struct Inner { int v; Inner(int v) : v(v) {} };
    Inner* in;
    int n;
    Outer() : in(new Inner(7)), n(0) {}
    void bump() { n++; in->v++; }
};

persistent<Outer>* o;

int main() {
    o = new persistent<Outer>();
    o->bump();
    std::cout << "n " << o->n << " v " << o->in->v << "\n";
}
