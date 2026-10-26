// in_class_init.cpp
//
// Default member initialisers of every wrapper kind: fundamental with `=`,
// braces, a narrowing conversion the original allows, a class by value built
// with arguments / no arguments / braces, a null pointer, and a pointer
// initialised with `new`. The constructor overrides one of them (`count`)
// through its initialiser list.

#include "persistenttype.hpp"
#include "harness.hpp"

class Pt {
public:
    int x = 0;
    int y = 0;
    Pt() {}
    Pt(int a, int b) : x(a), y(b) {}
};

class Node {
public:
    int   v;
    Node* next = nullptr;
    Node(int v) : v(v) {}
};

class Widget {
public:
    int   capacity = 16;
    int   count{3};
    int   trunc = 3.7;
    long  big = 1L << 40;
    Pt    origin = Pt(4, 5);
    Pt    zero = Pt();
    Pt    braced{6, 7};
    Node* empty = nullptr;
    Node* head = new Node(11);
    Widget(int c) : count(c) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE capacity=" << capacity << " count=" << count << " trunc=" << trunc
                  << " big=" << big << " origin=" << origin.x << "," << origin.y
                  << " zero=" << zero.x << "," << zero.y
                  << " braced=" << braced.x << "," << braced.y
                  << " empty=" << (empty == nullptr) << " head=" << head->v << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(8);
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
