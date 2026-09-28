#include "persistenttype.hpp"

/*! \file Stack.hpp
 * \brief Interface for a simple Stack class
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#ifndef _STACK_HPP_
#define _STACK_HPP_

#include "Node.hpp"
#include "iostream"	
using namespace std;
class Stack {
    Node* top  = nullptr;
    int   size = 0;
public:
    void push(int v); 
    int pop();
    bool isEmpty() const;
    void print() const;
};

template<>
class persistent<Stack> {
public:
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Stack)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void push(int v) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
	persistent<Node>* n = new persistent<Node>(v);
	n->next = top.get();
	top = n;
	size++;
});
    }

    int pop() {
        int __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> int {
	if (!top.get()) return 0;
	persistent<Node>* old = top.get();
	int v = old->value;
	top = old->next.get();
	size--;
	delete old;
	return v;
}();
        });
        return __pers_ret;
    }

    bool isEmpty() const {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool {
	return size == 0;
}();
        });
        return __pers_ret;
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
	persistent<Node>* cur = top.get();
	cout << "stack: ";
	while (cur) {
		cout << cur->value << " ";
		cur = cur->next.get();
	}
	cout << "\n";
});
    }

private:
    pmem_ptr<persistent<Node>> top;
    persistent<int> size;
};



void Stack::push(int v) {
	Node* n = new Node(v);
	n->next = top;
	top = n;
	size++;
}

int Stack::pop() {
	if (!top) return 0;
	Node* old = top;
	int v = old->value;
	top = old->next;
	size--;
	delete old;
	return v;
}

bool Stack::isEmpty() const {
	return size == 0;
}


void Stack::print() const {
	Node* cur = top;
	cout << "stack: ";
	while (cur) {
		cout << cur->value << " ";
		cur = cur->next;
	}
	cout << "\n";
}
#endif //_STACK_HPP_