// string_member.cpp
//
// C string fields. A `char*` field is a pmem pointer to an array of
// persistent<char>, filled with the new[] + strcpy idiom in the constructor
// and replaced in rename() through a local char*; a `char tag[12]` field is
// built with strncpy + strcat. Both are read with strlen, strcmp and
// `std::cout <<`, and every write through a C function is logged first.

#include "persistenttype.hpp"
#include "harness.hpp"
#include <cstring>

class Widget {
public:
    char* name;
    char  tag[12];
    Widget(const char* n) {
        name = new char[std::strlen(n) + 1];
        std::strcpy(name, n);
        std::strncpy(tag, "tag-", sizeof(tag));
        std::strcat(tag, n);
        harness::crash_point_ctor();
    }
    void rename(const char* n) {
        char* fresh = new char[std::strlen(n) + 1];
        std::strcpy(fresh, n);
        delete[] name;
        name = fresh;
    }
    void print() const {
        std::cout << "STATE name=" << name << " len=" << std::strlen(name)
                  << " tag=" << tag << " is_beta=" << (std::strcmp(name, "beta") == 0) << "\n";
    }
};

class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget("alpha");
        w->rename("beta");
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
