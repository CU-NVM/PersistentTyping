#ifdef UMF 
	                #include "numatype.hpp"
	                #include <umf/mempolicy.h>
	                #include <umf/memspace.h>
                    #include "utils_examples.h"
                    #include "umf_numa_allocator.hpp"
                    #include <numa.h>
                    #include <numaif.h>
                    #include <stdio.h>
                    #include <string.h>
                #endif
                #include "numatype.hpp"
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


/*!
 * \class Stack
 *
 * \brief Interface for a simple Stack class.
 *
 * This is the interface for a simple stack class.
 * The class utilizes the \class Node class as stack objects.
 * Thus, the stack is implemented as a Linked List.
 *
 */

class Stack
{

private:
	Node *top; //< Pointer to the top of the Stack


public:
	/*!
	 * \brief Default Stack Constructor
	 *
	 */
	Stack();

	/*!
	 * \brief Stack Destructor
	 *
	 * Need to remove each element one by one. The only pointer maintained
	 * is the top Node. Must delete top variable, while keeping track of
	 * next node so that we don't lose our stack.  
	 * This can be done by calling our pop function in a while loop.
	 *
	 * \sa Stack::pop()
	 */
	virtual ~Stack();

	/*!
	 * \brief Function for removing a single stack node
	 *
	 * The Stack::pop() function removes the most recently
	 * added node on the stack. It also simultaneously updates the pointer
	 * to the top of the stack.
	 *
	 * \return The data from the removed node.
	 */
	virtual int pop();

	/*!
	 * \brief Push function for adding stack variables
	 *
	 * The push function adds a new stack node to the top of the stack. 
	 *
	 * \param[in] data Data to be added to top of stack. This data is wrapped by a Node.
	 * 
	 * \note To avoid Memory Allocation issues, if allocation fails (i.e. overflow)
	 	Stack::push() is returned from immediately.
	 */
	virtual void push(int);


	/*!
	 * \brief A function to display the contents of the stack.
	 * 
	 * This function iterates over and prints each node in the stack.
	 * The first node printed is the top Node.
	 */
	virtual void display();


};

template<>
class numa<Stack,0>{
public: 
    static void* operator new(std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc(0 ,sizeof(Stack),alignof(Stack));
        #else
            p = numa_alloc_onnode(sz* sizeof(Stack), 0);
        #endif
        
        if (p == nullptr) {
            std::cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        return p;
    }

    static void* operator new[](std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc(0 ,sizeof(Stack),alignof(Stack));
        #else
            p = numa_alloc_onnode(sz* sizeof(Stack), 0);
        #endif
        
        if (p == nullptr) {
            std::cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        return p;
    }

    static void operator delete(void* ptr){
        // cout<<"doing numa free \n";
        #ifdef UMF
			umf_free(0,ptr);
		#else
		    numa_free(ptr, 1 * sizeof(Stack));
        #endif
    }

    static void operator delete[](void* ptr){
		// cout<<"doing numa free \n";
        #ifdef UMF
			umf_free(0,ptr);
		#else
		    numa_free(ptr, 1 * sizeof(Stack));
        #endif
    }
public:
numa (){
    this->top = __null;
}
virtual ~numa()
{
	
	if(top == NULL)
	{
		return;
	}
	Node *temp;
	while(top != NULL)
	{
		temp = top;
		top = top->getLink();
		delete temp;
	}

}
virtual int pop(){
    if (this->top == __null) {
        return -1;
    }
    Node *retN = this->top;
    this->top = this->top->getLink();
    int data = retN->getData();
    delete retN;
    retN = __null;
    return data;
}
virtual void push(int data){
    Node *newN = reinterpret_cast<Node*>(new numa<Node,0>(data));
    if (newN == __null) {
        std::cerr << "Stack full" << std::endl;
        return;
    }
    newN->setLink(this->top);
    this->top = newN;
}
virtual void display(){
    if (this->top == __null) {
        std::cout << "Stack Empty!!" << std::endl;
        return;
    }
    int i = 0;
    Node *temp = this->top;
    while (temp != __null)
        {
            if (i == 0)
                std::cout << "TOP ";
            std::cout << temp->getData() << std::endl;
            temp = temp->getLink();
            i++;
        }
}
private:
numa<Node*,0> top;
};

template<>
class numa<Stack,1>{
public: 
    static void* operator new(std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc(1 ,sizeof(Stack),alignof(Stack));
        #else
            p = numa_alloc_onnode(sz* sizeof(Stack), 1);
        #endif
        
        if (p == nullptr) {
            std::cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        return p;
    }

    static void* operator new[](std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc(1 ,sizeof(Stack),alignof(Stack));
        #else
            p = numa_alloc_onnode(sz* sizeof(Stack), 1);
        #endif
        
        if (p == nullptr) {
            std::cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        return p;
    }

    static void operator delete(void* ptr){
        // cout<<"doing numa free \n";
        #ifdef UMF
			umf_free(1,ptr);
		#else
		    numa_free(ptr, 1 * sizeof(Stack));
        #endif
    }

    static void operator delete[](void* ptr){
		// cout<<"doing numa free \n";
        #ifdef UMF
			umf_free(1,ptr);
		#else
		    numa_free(ptr, 1 * sizeof(Stack));
        #endif
    }
public:
numa (){
    this->top = __null;
}
virtual ~numa()
{
	
	if(top == NULL)
	{
		return;
	}
	Node *temp;
	while(top != NULL)
	{
		temp = top;
		top = top->getLink();
		delete temp;
	}

}
virtual int pop(){
    if (this->top == __null) {
        return -1;
    }
    Node *retN = this->top;
    this->top = this->top->getLink();
    int data = retN->getData();
    delete retN;
    retN = __null;
    return data;
}
virtual void push(int data){
    Node *newN = reinterpret_cast<Node*>(new numa<Node,1>(data));
    if (newN == __null) {
        std::cerr << "Stack full" << std::endl;
        return;
    }
    newN->setLink(this->top);
    this->top = newN;
}
virtual void display(){
    if (this->top == __null) {
        std::cout << "Stack Empty!!" << std::endl;
        return;
    }
    int i = 0;
    Node *temp = this->top;
    while (temp != __null)
        {
            if (i == 0)
                std::cout << "TOP ";
            std::cout << temp->getData() << std::endl;
            temp = temp->getLink();
            i++;
        }
}
private:
numa<Node*,1> top;
};


Stack::Stack()
{
	top = NULL;
}


Stack::~Stack()
{
	
	if(top == NULL)
	{
		return;
	}
	Node *temp;
	while(top != NULL)
	{
		temp = top;
		top = top->getLink();
		delete temp;
	}

}


int Stack::pop()
{
	if(top == NULL)
	{
		return -1;
	}
	Node *retN = top;
	top = top->getLink();
	int data = retN->getData();
	delete retN;
	retN = NULL;
	return data;
}


void Stack::push(int data)
{
	Node *newN = new Node(data);
	if(newN == NULL)
	{
		std::cerr << "Stack full" << std::endl;
		return;
	}

	//Node *newNext = newN->getLink();
	newN->setLink(top);
	top = newN;

}

void Stack::display()
{
	if(top == NULL)
	{
		std::cout << "Stack Empty!!" << std::endl;
		return;
	}

	int i = 0;
	
	Node *temp = top;
	while (temp != NULL)
	{
		
		if(i == 0)
			std::cout << "TOP ";

		std::cout << temp->getData() << std::endl;
		temp = temp->getLink();
		i++;
	}
}


#endif //_STACK_HPP_