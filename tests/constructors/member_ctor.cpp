// member_ctor.cpp
//
// Class-by-value members: one with no default constructor that must be built
// in the initialiser list (`pt(3, 4)`), and a nested class whose own
// constructor uses an initialiser list.

#include "persistenttype.hpp"
#include "harness.hpp"

class Pt {
public:
    int x;
    int y;
    Pt(int x, int y) : x(x), y(y) {}
};

class Range {
public:
    int lo;
    int hi;
    Range(int lo, int len) : lo(lo), hi(lo + len) {}
};

class Widget {
public:
    Pt    pt;
    Range r;
    int   id;
    Widget(int id) : pt(3, 4), r(id, 5), id(id) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE pt=" << pt.x << "," << pt.y << " r=" << r.lo << ".." << r.hi
                  << " id=" << id << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(20);
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
