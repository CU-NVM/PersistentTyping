// default_ctor.cpp
//
// User-written default constructor that sets fields by assignment in the
// body (no initialiser list), including a field read back in a later
// assignment.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int  a;
    int  b;
    long sum;
    Widget() {
        a = 3;
        b = 4;
        sum = a + b;
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE a=" << a << " b=" << b << " sum=" << sum << "\n";
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
