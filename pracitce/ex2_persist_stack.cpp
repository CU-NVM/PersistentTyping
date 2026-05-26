#include "../persistentLib/persistenttype.hpp"
#include <iostream>

using namespace pmem::obj;


// ---------- Regular DRAM versions ----------
class Node {
public:
    int   value;
    Node* next;
    Node(int v) : value(v), next(nullptr) {}
};

class Stack {
    Node* top;
    int size;
public:
    Stack() : top(nullptr), size(0) {}
    void push (int v) {
        Node* n = new Node(v);
        n->next = top;
        top = n;
        size = size + 1;
    }
    void pop(){
        if(!top) return;
        Node* old = top;
        int v = old->value;
        top = old->next;
        size = size - 1;
        delete old;
    }
    void print() const {
        auto cur = top;
        std::cout << "stack (top -> bottom): ";
        while (cur) { 
            std::cout << cur->value << " "; 
            cur = cur->next; }
        std::cout << "(size=" << size << ")\n";
    }
};


// ---------- Persistent specializations ----------
template<>
class persistent<Node> {       // FULL specialization — not the generic class spec
public:
    persistent<int>             value;
    pmem_ptr<persistent<Node>>  next;

    persistent(int v) : value(v) {}

    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(persistent<Node>)); }
    static void  operator delete(void* p)       { pmem_free(p); }
};

template<>
class persistent<Stack> {
    pmem_ptr<persistent<Node>> top;
    persistent<int> size;
public:
    static void* operator new(std::size_t sz) 
    { 
        return pmem_alloc(sz, alignof(persistent<Stack>)); 
    }

    static void* operator new[](std::size_t sz) 
    { 
        return pmem_alloc(sz, alignof(persistent<Stack>)); 
    }

    static void operator delete(void* p) 
    { 
        pmem_free(p); 
    }

    static void operator delete[](void* p) 
    { 
        pmem_free(p); 
    }
    
    persistent() : top(nullptr), size(0) {}


    void push (int v) {
        transaction::run (pmem_pool(), [&]{
            persistent<Node>* n = new persistent<Node>(v);
            n->next = top;
            top = n;
            size = size + 1;
        });
    }
    
    void pop(){
        transaction::run(pmem_pool(), [&]{
            if(!top) return;
            persistent<Node>* old = top.get();
            int v = old->value;
            top = old->next;
            size = size - 1;
            delete old;
        });
    }

    void print() const {
        auto cur = top;
        std::cout << "stack (top -> bottom): ";
        while (cur) { 
            std::cout << cur->value << " ";  
            cur = cur->next; 
        }
        std::cout << "\n";
    }
};


struct Root {
    pmem_ptr<persistent<Stack>> stack;
};


int main(int argc, char* argv[]) {
    bool crash_mode = (argc > 1 && std::string(argv[1]) == "--crash");
    Root* root = pmem_root<Root>();
    auto stack = pmem_get_or_create<persistent<Stack>>(root->stack);
    
    std::cout << "Before :\n";
    stack->print();

    if(crash_mode) { 
        transaction::run(pmem_pool(), [&]{
            stack->push(10);
            stack->push(20);
            stack->push(30);
            std::cerr<< "Crashing now...\n";
            std::abort();
        });
    } else {
        transaction::run(pmem_pool(), [&]{
            for(int i = 1; i <= 3; i++){
                stack->push(i*10);
            }
        });
    }

    std::cout << "After :\n";
    stack->print();

    return 0;
}
