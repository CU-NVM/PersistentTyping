// string_member.cpp
//
// String fields, which the tool remaps to pmem_string: a `char*` filled with
// the new[] + strcpy idiom in the body, a `std::string` built in the
// initialiser list, and a `const char*` with an in-class initialiser.

#include "persistenttype.hpp"
#include "harness.hpp"
#include <cstring>
#include <string>

class Widget {
public:
    char*       name;
    std::string label;
    const char* tag = "default-tag";
    Widget(const char* n, const std::string& l) : label(l) {
        name = new char[std::strlen(n) + 1];
        std::strcpy(name, n);
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE name=" << name << " label=" << label << " tag=" << tag << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget("alpha", std::string("beta"));
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
