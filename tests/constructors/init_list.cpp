// init_list.cpp
//
// Member initialiser list: a parameter shadowing its field (`size(size)`),
// a list written out of declaration order, expressions over other
// parameters, value-init `x()` and brace-init `x{..}`.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int size;
    int doubled;
    int capacity;
    int zero;
    int braced;
    Widget(int size, int capacity)
        : capacity(capacity), doubled(size * 2), size(size), zero(), braced{9} {
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE size=" << size << " doubled=" << doubled << " capacity=" << capacity
                  << " zero=" << zero << " braced=" << braced << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(5, 16);
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
