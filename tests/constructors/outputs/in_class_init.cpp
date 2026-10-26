#include "in_class_init_pers_root.hpp"
// in_class_init.cpp
//
// Default member initialisers of every wrapper kind: fundamental with `=`,
// braces, a narrowing conversion the original allows, a class by value built
// with arguments / no arguments / braces, a null pointer, and a pointer
// initialised with `new`. The constructor overrides one of them (`count`)
// through its initialiser list.

#include "persistenttype.hpp"
#include "harness.hpp"

class Pt {
public:
    int x = 0;
    int y = 0;
    Pt() {}
    Pt(int a, int b) : x(a), y(b) {}
};

template<>
class persistent<Pt> {
public:
    persistent<int> x = 0;
    persistent<int> y = 0;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Pt)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

    persistent(int a, int b) : x(a), y(b) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

};


class Node {
public:
    int   v;
    Node* next = nullptr;
    Node(int v) : v(v) {}
};

template<>
class persistent<Node> {
public:
    persistent<int> v;
    pmem_ptr<persistent<Node>> next{nullptr};
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Node)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int v) : v(v) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

};


class Widget {
public:
    int   capacity = 16;
    int   count{3};
    int   trunc = 3.7;
    long  big = 1L << 40;
    Pt    origin = Pt(4, 5);
    Pt    zero = Pt();
    Pt    braced{6, 7};
    Node* empty = nullptr;
    Node* head = new Node(11);
    Widget(int c) : count(c) { harness::crash_point_ctor(); }
    void print() const {
        std::cout << "STATE capacity=" << capacity << " count=" << count << " trunc=" << trunc
                  << " big=" << big << " origin=" << origin.x << "," << origin.y
                  << " zero=" << zero.x << "," << zero.y
                  << " braced=" << braced.x << "," << braced.y
                  << " empty=" << (empty == nullptr) << " head=" << head->v << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    persistent<int> capacity = 16;
    persistent<int> count{3};
    persistent<int> trunc = 3.7;
    persistent<long> big = 1L << 40;
    persistent<Pt> origin{4, 5};
    persistent<Pt> zero{};
    persistent<Pt> braced{6, 7};
    pmem_ptr<persistent<Node>> empty{nullptr};
    pmem_ptr<persistent<Node>> head{new persistent<Node>(11)};
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int c) : count(c) {
        pmem::obj::transaction::run(pmem_pool(), [&]() { harness::crash_point_ctor(); });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE capacity=" << capacity << " count=" << count << " trunc=" << trunc
                  << " big=" << big << " origin=" << origin.x << "," << origin.y
                  << " zero=" << zero.x << "," << zero.y
                  << " braced=" << braced.x << "," << braced.y
                  << " empty=" << (empty.get() == nullptr) << " head=" << head.get()->v << "\n";
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
        w = new Widget(8);
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
        w = new persistent<Widget>(8);
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
