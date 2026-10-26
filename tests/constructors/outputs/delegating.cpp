#include "delegating_pers_root.hpp"
// delegating.cpp
//
// Delegating constructors: a default and a one-argument constructor both
// forward to the two-argument one, and each adds its own body on top.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int b;
    int tag = 0;
    Widget(int a, int b) : a(a), b(b) { tag = tag + 1; }
    Widget(int a) : Widget(a, a * 10) { tag = tag + 10; harness::crash_point_ctor(); }
    Widget() : Widget(2) { tag = tag + 100; }
    void print() const {
        std::cout << "STATE a=" << a << " b=" << b << " tag=" << tag << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> a;
    persistent<int> b;
    persistent<int> tag = 0;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int a, int b) : a(a), b(b) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { tag = tag + 1; });
    }

    persistent(int a) : persistent(a, a * 10) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { tag = tag + 10; harness::crash_point_ctor(); });
    }

    persistent() : persistent(2) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { tag = tag + 100; });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE a=" << a << " b=" << b << " tag=" << tag << "\n";
    });
    }

};


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

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Widget>> w{nullptr};
    pmem_ptr<persistent<Widget>> d{nullptr};
    persistent<int> runs = 0;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Holder)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void bump() {
        pmem::obj::transaction::run(pmem_pool(), [&]() { runs++; });
    }

    bool has() const {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool { return w.get() != nullptr; }();
        });
        return __pers_ret;
    }

    void make() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        w = new persistent<Widget>(5);
        d = new persistent<Widget>();
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        w.get()->print();
        d.get()->print();
    });
    }

};


__pers_root* __pers_get_root() {
    static __pers_root* root = pmem_root<__pers_root>();
    return root;
}

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = pmem_get_or_create<persistent<Holder>>(__pers_get_root()->h);
    return harness::drive(h);
}
