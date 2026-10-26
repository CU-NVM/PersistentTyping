#include "member_ctor_pers_root.hpp"
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

template<>
class persistent<Pt> {
public:
    persistent<int> x;
    persistent<int> y;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Pt)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int x, int y) : x(x), y(y) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

};


class Range {
public:
    int lo;
    int hi;
    Range(int lo, int len) : lo(lo), hi(lo + len) {}
};

template<>
class persistent<Range> {
public:
    persistent<int> lo;
    persistent<int> hi;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Range)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int lo, int len) : lo(lo), hi(lo + len) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

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

template<>
class persistent<Widget> {
public:
    persistent<Pt> pt;
    persistent<Range> r;
    persistent<int> id;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int id) : pt(3, 4), r(id, 5), id(id) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { harness::crash_point_ctor(); });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE pt=" << pt.x << "," << pt.y << " r=" << r.lo << ".." << r.hi
                  << " id=" << id << "\n";
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
        w = new Widget(20);
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
        w = new persistent<Widget>(20);
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
