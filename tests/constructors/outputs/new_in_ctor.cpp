#include "new_in_ctor_pers_root.hpp"
// new_in_ctor.cpp
//
// A constructor that allocates: one child in the initialiser list, two more
// in the body, linked through a pointer field. crash-ctor kills the process
// after all three allocations, so recovery must free all of them.

#include "persistenttype.hpp"
#include "harness.hpp"

class Node {
public:
    int   v;
    Node* next;
    Node(int v) : v(v), next(nullptr) {}
};

template<>
class persistent<Node> {
public:
    persistent<int> v;
    pmem_ptr<persistent<Node>> next;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Node)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int v) : v(v), next(nullptr) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {});
    }

};


class Chain {
public:
    Node* head;
    int   len;
    Chain(int first) : head(new Node(first)), len(1) {
        head->next = new Node(first + 1);
        head->next->next = new Node(first + 2);
        len = 3;
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE len=" << len << " nodes=";
        Node* cur = head;
        while (cur) {
            std::cout << cur->v << " ";
            cur = cur->next;
        }
        std::cout << "\n";
    }
};

template<>
class persistent<Chain> {
public:
    pmem_ptr<persistent<Node>> head;
    persistent<int> len;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Chain)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int first) : head(new persistent<Node>(first)), len(1) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        head.get()->next = new persistent<Node>(first + 1);
        head.get()->next.get()->next = new persistent<Node>(first + 2);
        len = 3;
        harness::crash_point_ctor();
    });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE len=" << len << " nodes=";
        persistent<Node>* cur = head.get();
        while (cur) {
            std::cout << cur->v << " ";
            cur = cur->next.get();
        }
        std::cout << "\n";
    });
    }

};


class Holder {
public:
    Chain* c = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return c != nullptr; }
    void make() {
        c = new Chain(100);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { c->print(); }
};

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Chain>> c{nullptr};
    persistent<int> runs = 0;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Holder)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void bump() {
        pmem::obj::transaction::run(pmem_pool(), [&]() { runs++; });
    }

    bool has() const {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool { return c.get() != nullptr; }();
        });
        return __pers_ret;
    }

    void make() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        c = new persistent<Chain>(100);
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() { c.get()->print(); });
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
