// delegating.cpp
//
// Delegating constructors: a default and a one-argument constructor both
// forward to the two-argument one, and each adds its own body on top.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int b;
    int tag = 0;
    Widget(int a, int b) : a(a), b(b) { tag = tag + 1; }
    Widget(int a) : Widget(a, a * 10) { tag = tag + 10; harness::crash_point_ctor(); }
    Widget() : Widget(2) { tag = tag + 100; }
    void print() const {
        std::cout << "STATE a=" << a << " b=" << b << " tag=" << tag << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    Widget* d = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(5);
        d = new Widget();
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        w->print();
        d->print();
    }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
