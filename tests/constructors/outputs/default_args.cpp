#include "default_args_pers_root.hpp"
// default_args.cpp
//
// Constructor default arguments, called with zero, one and two arguments,
// plus a default argument on an ordinary method.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int b;
    Widget(int a = 7, int b = 9) : a(a), b(b) { harness::crash_point_ctor(); }
    int scaled(int k = 3) const { return a * k; }
    void print() const {
        std::cout << "STATE a=" << a << " b=" << b << " scaled=" << scaled()
                  << "," << scaled(10) << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> a;
    persistent<int> b;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int a = 7, int b = 9) : a(a), b(b) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { harness::crash_point_ctor(); });
    }

    int scaled(int k = 3) const {
        int __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> int { return a * k; }();
        });
        return __pers_ret;
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE a=" << a << " b=" << b << " scaled=" << scaled()
                  << "," << scaled(10) << "\n";
    });
    }

};


class Holder {
public:
    Widget* none = nullptr;
    Widget* one  = nullptr;
    Widget* two  = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return none != nullptr; }
    void make() {
        none = new Widget();
        one  = new Widget(1);
        two  = new Widget(1, 2);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        none->print();
        one->print();
        two->print();
    }
};

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Widget>> none{nullptr};
    pmem_ptr<persistent<Widget>> one{nullptr};
    pmem_ptr<persistent<Widget>> two{nullptr};
    persistent<int> runs = 0;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Holder)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void bump() {
        pmem::obj::transaction::run(pmem_pool(), [&]() { runs++; });
    }

    bool has() const {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool { return none.get() != nullptr; }();
        });
        return __pers_ret;
    }

    void make() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        none = new persistent<Widget>();
        one  = new persistent<Widget>(1);
        two  = new persistent<Widget>(1, 2);
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        none.get()->print();
        one.get()->print();
        two.get()->print();
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
