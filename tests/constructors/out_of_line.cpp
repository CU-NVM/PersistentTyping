// out_of_line.cpp
//
// Constructors declared in the class and defined outside it: the default
// argument lives on the in-class declaration, the initialiser list and body
// on the out-of-line definition.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int b;
    Widget(int a, int b = 6);
    Widget();
    void print() const;
};

Widget::Widget(int a, int b) : a(a), b(b) { harness::crash_point_ctor(); }

Widget::Widget() {
    a = 1;
    b = 2;
}

void Widget::print() const {
    std::cout << "STATE a=" << a << " b=" << b << "\n";
}

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
