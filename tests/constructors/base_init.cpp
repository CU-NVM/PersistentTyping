// base_init.cpp
//
// XFAIL: base-class initialisers are rejected until the Inheritance TODO.
//
// A derived class whose constructor initialises its base.

#include "persistenttype.hpp"
#include "harness.hpp"

class Base {
public:
    int b;
    Base(int b) : b(b) {}
};

class Widget : public Base {
public:
    int d;
    Widget(int b, int d) : Base(b), d(d) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE b=" << b << " d=" << d << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(1, 2);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { w->print(); }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
