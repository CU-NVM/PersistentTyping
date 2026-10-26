#include "copy_ctor_pers_root.hpp"
// copy_ctor.cpp
//
// A user-written copy constructor, used to clone one persistent object into
// another.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int copies;
    Widget(int a) : a(a), copies(0) {}
    Widget(const Widget& o) : a(o.a), copies(o.copies + 1) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE a=" << a << " copies=" << copies << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> a;
    persistent<int> copies;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int a) : a(a), copies(0) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

    persistent(const Widget & o) : a(o.a), copies(o.copies + 1) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { harness::crash_point_ctor(); });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE a=" << a << " copies=" << copies << "\n";
    });
    }

};


class Holder {
public:
    Widget* w = nullptr;
    Widget* c = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget(9);
        c = new Widget(*w);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        w->print();
        c->print();
    }
};

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Widget>> w{nullptr};
    pmem_ptr<persistent<Widget>> c{nullptr};
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
        w = new persistent<Widget>(9);
        c = new persistent<Widget>(*w.get());
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        w.get()->print();
        c.get()->print();
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
