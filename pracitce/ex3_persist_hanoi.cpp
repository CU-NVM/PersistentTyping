#include <iostream>
#include "../persistentLib/persistenttype.hpp"


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
    int pop(){
        if(!top) return 0;
        Node* old = top;
        int v = old->value;
        top = old->next;
        size = size - 1;
        delete old;
        return v;
    }
    void print() const {
        auto cur = top;
        std::cout << "stack (top -> bottom): ";
        while (cur) { 
            std::cout << cur->value << " "; 
            cur = cur->next; }
        std::cout << "(size=" << size << ")\n";
    }
    bool isEmpty() const {
        return size == 0;
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
        persistent<Node>* n = new persistent<Node>(v);
        n->next = top;
        top = n;
        size = size + 1;
    }
    
    int pop(){
        int v;
        if(!top) return 0;
        persistent<Node>* old = top.get();
        v = old->value;
        top = old->next;
        size = size - 1;
        delete old;
        return v;
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

    bool isEmpty() const {
        return size == 0;
    }
};

void moveDisks(int n, persistent<Stack>* from, persistent<Stack>* to, persistent<Stack>* aux, bool crash_mode){
    if(n<=0) return;
    moveDisks(n-1, from, aux, to, crash_mode);
    if(crash_mode){
        transaction::run(pmem_pool(), [&]{
            int disk = from->pop(); // pop inside transaction to ensure it's logged in PMEM
            std::cerr<<"Crashing after popping "<<disk<<" from stack\n";
            abort(); // simulate crash right after popping from 'from' stack, before pushing to 'to' stack
            to->push(disk); // push inside transaction to ensure it's logged in PMEM
        });
    }else {
        transaction::run(pmem_pool(), [&]{
            int disk = from->pop();
            to->push(disk);
        });
    }
    moveDisks(n-1, aux, to, from, crash_mode);
}

struct Root {
    pmem_ptr<persistent<Stack>> s1;
    pmem_ptr<persistent<Stack>> s2;
    pmem_ptr<persistent<Stack>> s3;
};

int main(int argc, char* argv[]) {
    bool crash_mode = (argc > 1 && std::string(argv[1]) == "--crash");
    Root* root = pmem_root<Root>();
    persistent<Stack>* s1 = pmem_get_or_create<persistent<Stack>>(root->s1);
    persistent<Stack>* s2 = pmem_get_or_create<persistent<Stack>>(root->s2);
    persistent<Stack>* s3 = pmem_get_or_create<persistent<Stack>>(root->s3);
    bool freshly_seeded = false;
    transaction::run(pmem_pool(), [&]{
        if(s1->isEmpty() && s2->isEmpty() && s3->isEmpty()) {
            s1->push(3); s1->push(2); s1->push(1);
            freshly_seeded = true;
        }
    });
    if (freshly_seeded) moveDisks(3, s1, s2, s3, crash_mode);
    
    s1->print();
    s2->print();
    s3->print();
    return 0;
}