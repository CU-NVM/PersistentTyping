// copy_ctor.cpp
//
// A user-written copy constructor, used to clone one persistent object into
// another.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int copies;
    Widget(int a) : a(a), copies(0) {}
    Widget(const Widget& o) : a(o.a), copies(o.copies + 1) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE a=" << a << " copies=" << copies << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    Widget* c = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(9);
        c = new Widget(*w);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        w->print();
        c->print();
    }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
