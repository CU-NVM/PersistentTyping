// overloaded.cpp
//
// An overload set, including an explicit constructor and one chosen by
// argument type (int vs double).

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int    kind;
    int    a;
    double d;
    Widget() : kind(0), a(0), d(0.0) {}
    explicit Widget(int a) : kind(1), a(a), d(0.0) { harness::crash_point_ctor(); }
    Widget(double d) : kind(2), a(0), d(d) {}
    Widget(int a, double d) : kind(3), a(a), d(d) {}
    void print() const {
        std::cout << "STATE kind=" << kind << " a=" << a << " d=" << d << "\n";
    }
};

class Holder {
public:
    Widget* w0 = nullptr;
    Widget* w1 = nullptr;
    Widget* w2 = nullptr;
    Widget* w3 = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w0 != nullptr; }
    void make() {
        w0 = new Widget();
        w1 = new Widget(7);
        w2 = new Widget(2.5);
        w3 = new Widget(3, 1.5);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        w0->print();
        w1->print();
        w2->print();
        w3->print();
    }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
