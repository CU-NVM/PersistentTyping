#include "overloaded_pers_root.hpp"
// overloaded.cpp
//
// An overload set, including an explicit constructor and one chosen by
// argument type (int vs double).

#include "persistenttype.hpp"
#include "harness.hpp"

class Widget {
public:
    int    kind;
    int    a;
    double d;
    Widget() : kind(0), a(0), d(0.0) {}
    explicit Widget(int a) : kind(1), a(a), d(0.0) { harness::crash_point_ctor(); }
    Widget(double d) : kind(2), a(0), d(d) {}
    Widget(int a, double d) : kind(3), a(a), d(d) {}
    void print() const {
        std::cout << "STATE kind=" << kind << " a=" << a << " d=" << d << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> kind;
    persistent<int> a;
    persistent<double> d;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent() : kind(0), a(0), d(0.0) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

    persistent(int a) : kind(1), a(a), d(0.0) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { harness::crash_point_ctor(); });
    }

    persistent(double d) : kind(2), a(0), d(d) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

    persistent(int a, double d) : kind(3), a(a), d(d) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE kind=" << kind << " a=" << a << " d=" << d << "\n";
    });
    }

};


class Holder {
public:
    Widget* w0 = nullptr;
    Widget* w1 = nullptr;
    Widget* w2 = nullptr;
    Widget* w3 = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w0 != nullptr; }
    void make() {
        w0 = new Widget();
        w1 = new Widget(7);
        w2 = new Widget(2.5);
        w3 = new Widget(3, 1.5);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const {
        w0->print();
        w1->print();
        w2->print();
        w3->print();
    }
};

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Widget>> w0{nullptr};
    pmem_ptr<persistent<Widget>> w1{nullptr};
    pmem_ptr<persistent<Widget>> w2{nullptr};
    pmem_ptr<persistent<Widget>> w3{nullptr};
    persistent<int> runs = 0;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Holder)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void bump() {
        pmem::obj::transaction::run(pmem_pool(), [&]() { runs++; });
    }

    bool has() const {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool { return w0.get() != nullptr; }();
        });
        return __pers_ret;
    }

    void make() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        w0 = new persistent<Widget>();
        w1 = new persistent<Widget>(7);
        w2 = new persistent<Widget>(2.5);
        w3 = new persistent<Widget>(3, 1.5);
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        w0.get()->print();
        w1.get()->print();
        w2.get()->print();
        w3.get()->print();
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
