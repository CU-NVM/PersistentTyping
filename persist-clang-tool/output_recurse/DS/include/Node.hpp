#include "persistenttype.hpp"


#ifndef _NODE_HPP_
#define _NODE_HPP_

class Node {
public:
    int   value;
    Node* next;
    Node(int v) : value(v), next(nullptr) {}
};

template<>
class persistent<Node> {
public:
    persistent<int> value;
    pmem_ptr<persistent<Node>> next;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Node)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(int v) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
    value = v;
    next = nullptr;
});
    }

};



#endif //_NODE_HPP_