// default_args.cpp
//
// Constructor default arguments, called with zero, one and two arguments,
// plus a default argument on an ordinary method.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int b;
    Widget(int a = 7, int b = 9) : a(a), b(b) { harness::crash_point_ctor(); }
    int scaled(int k = 3) const { return a * k; }
    void print() const {
        std::cout << "STATE a=" << a << " b=" << b << " scaled=" << scaled()
                  << "," << scaled(10) << "\n";
    }
};

class Holder {
public:
    Widget* none = nullptr;
    Widget* one  = nullptr;
    Widget* two  = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return none != nullptr; }
    void make() {
        none = new Widget();
        one  = new Widget(1);
        two  = new Widget(1, 2);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        none->print();
        one->print();
        two->print();
    }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
