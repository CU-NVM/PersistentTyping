// implicit_ctor.cpp
//
// No user-declared constructor: the implicit default constructor runs the
// in-class initialisers, and a field without one is value-initialised by
// `new Widget()`. There is no constructor body, so crash-ctor is n/a.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int  hits = 42;
    int  plain;
    long big = 1L << 33;
    void print() const {
        std::cout << "STATE hits=" << hits << " plain=" << plain << " big=" << big << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget();
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
