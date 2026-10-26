#include "implicit_ctor_pers_root.hpp"
// implicit_ctor.cpp
//
// No user-declared constructor: the implicit default constructor runs the
// in-class initialisers, and a field without one is value-initialised by
// `new Widget()`. There is no constructor body, so crash-ctor is n/a.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int  hits = 42;
    int  plain;
    long big = 1L << 33;
    void print() const {
        std::cout << "STATE hits=" << hits << " plain=" << plain << " big=" << big << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> hits = 42;
    persistent<int> plain;
    persistent<long> big = 1L << 33;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE hits=" << hits << " plain=" << plain << " big=" << big << "\n";
    });
    }

};


class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget();
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { w->print(); }
};

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Widget>> w{nullptr};
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
        w = new persistent<Widget>();
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() { w.get()->print(); });
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
