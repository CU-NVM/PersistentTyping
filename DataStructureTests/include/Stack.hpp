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