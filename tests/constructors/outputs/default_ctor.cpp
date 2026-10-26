#include "default_ctor_pers_root.hpp"
// default_ctor.cpp
//
// User-written default constructor that sets fields by assignment in the
// body (no initialiser list), including a field read back in a later
// assignment.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int  a;
    int  b;
    long sum;
    Widget() {
        a = 3;
        b = 4;
        sum = a + b;
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE a=" << a << " b=" << b << " sum=" << sum << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> a;
    persistent<int> b;
    persistent<long> sum;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        a = 3;
        b = 4;
        sum = a + b;
        harness::crash_point_ctor();
    });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE a=" << a << " b=" << b << " sum=" << sum << "\n";
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
