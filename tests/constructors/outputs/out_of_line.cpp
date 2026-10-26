#include "out_of_line_pers_root.hpp"
// out_of_line.cpp
//
// Constructors declared in the class and defined outside it: the default
// argument lives on the in-class declaration, the initialiser list and body
// on the out-of-line definition.

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int a;
    int b;
    Widget(int a, int b = 6);
    Widget();
    void print() const;
};

template<>
class persistent<Widget> {
public:
    persistent<int> a;
    persistent<int> b;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int a, int b = 6) : a(a), b(b) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { harness::crash_point_ctor(); });
    }

    persistent() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
    a = 1;
    b = 2;
});
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
    std::cout << "STATE a=" << a << " b=" << b << "\n";
});
    }

};


Widget::Widget(int a, int b) : a(a), b(b) { harness::crash_point_ctor(); }

Widget::Widget() {
    a = 1;
    b = 2;
}

void Widget::print() const {
    std::cout << "STATE a=" << a << " b=" << b << "\n";
}

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
