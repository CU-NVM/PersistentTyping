#include "init_list_pers_root.hpp"
// init_list.cpp
//
// Member initialiser list: a parameter shadowing its field (`size(size)`),
// a list written out of declaration order, expressions over other
// parameters, value-init `x()` and brace-init `x{..}`.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int size;
    int doubled;
    int capacity;
    int zero;
    int braced;
    Widget(int size, int capacity)
        : capacity(capacity), doubled(size * 2), size(size), zero(), braced{9} {
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE size=" << size << " doubled=" << doubled << " capacity=" << capacity
                  << " zero=" << zero << " braced=" << braced << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> size;
    persistent<int> doubled;
    persistent<int> capacity;
    persistent<int> zero;
    persistent<int> braced;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int size, int capacity) : size(size), doubled(size * 2), capacity(capacity), zero(), braced{9} {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        harness::crash_point_ctor();
    });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE size=" << size << " doubled=" << doubled << " capacity=" << capacity
                  << " zero=" << zero << " braced=" << braced << "\n";
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
        w = new Widget(5, 16);
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
        w = new persistent<Widget>(5, 16);
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
