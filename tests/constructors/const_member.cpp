// const_member.cpp
//
// A const field, which can only be set in the initialiser list.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    const int id;
    int       val;
    Widget(int id) : id(id), val(id * 2) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE id=" << id << " val=" << val << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(12);
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
