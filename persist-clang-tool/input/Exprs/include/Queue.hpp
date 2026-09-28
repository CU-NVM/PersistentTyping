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
                
#ifndef _QUEUE_HPP_
#define _QUEUE_HPP_

#include "Node.hpp"
#include <iostream>

class Queue
{
private:
	Node *front;
	Node *rear;

public:

	/*!
	 * \brief Default Queue Constructor
	 *
	 */
	Queue();

	/*!
	 * \brief Queue Destructor
	 *
	 * Need to remove each element one by one. There are two pointers maintained,
	 * the front Node and the rear Node. Must delete from front variable, while keeping track of
	 * next node so that we don't lose our Queue.  
	 * This can be done by calling our del function in a while loop.
	 *
	 * \sa Queue::del()
	 */
	virtual ~Queue();

	/*!
	 * \brief Function for removing a single Queue node
	 *
	 * The Queue::del() function removes the least recently
	 * added node on the Queue. It also simultaneously updates the pointer
	 * to the front and rear of the Queue.
	 *
	 * \return The data from the removed node.
	 */
	virtual int del();

	/*!
	 * \brief add function for adding Queue variables
	 *
	 * The add function adds a new Queue node to the top of the Queue. 
	 *
	 * \param[in] data Data to be added to front of Queue. This data is wrapped by a Node.
	 * 
	 * \note To avoid Memory Allocation issues, if allocation fails (i.e. overflow)
	 	Queue::add() is returned from immediately.
	 */

	virtual void add(int);


	/*!
	 * \brief A function to display the contents of the Queue.
	 * 
	 * This function iterates over and prints each node in the Queue.
	 * The first node printed is the top Node.
	 */
	virtual void display();

};

template<>
class numa<Queue,0>{
public: 
    static void* operator new(std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc(0 ,sizeof(Queue),alignof(Queue));
        #else
            p = numa_alloc_onnode(sz* sizeof(Queue), 0);
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
            p= umf_alloc(0 ,sizeof(Queue),alignof(Queue));
        #else
            p = numa_alloc_onnode(sz* sizeof(Queue), 0);
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
		    numa_free(ptr, 1 * sizeof(Queue));
        #endif
    }

    static void operator delete[](void* ptr){
		// cout<<"doing numa free \n";
        #ifdef UMF
			umf_free(0,ptr);
		#else
		    numa_free(ptr, 1 * sizeof(Queue));
        #endif
    }
public:
numa (){
    this->front = __null;
    this->rear = __null;
}
virtual ~numa()
{
	while(front != NULL)
	{
		Node *temp = front;
		front = front->getLink();
		int data = temp->getData();
		delete temp;
	}

}
virtual int del(){
    if (this->front == __null) {
        return -1;
    }
    Node *temp = this->front;
    this->front = this->front->getLink();
    int data = temp->getData();
    delete temp;
    if (this->front == __null) {
        this->rear = __null;
    }
    return data;
}
virtual void add(int initData){
    if (this->front == __null) {
        this->front = reinterpret_cast<Node*>(new numa<Node,0>(initData));
        this->front->setLink(this->rear);
        this->rear = this->front;
        return;
    }
    Node *newNode = reinterpret_cast<Node*>(new numa<Node,0>(initData));
    this->rear->setLink(newNode);
    newNode->setLink(__null);
    this->rear = newNode;
}
virtual void display(){
    Node *temp = this->front;
    while (temp != __null)
        {
            if (temp == this->front) {
                std::cout << "FRONT " << std::endl;
            }
            std::cout << temp->getData() << std::endl;
            temp = temp->getLink();
        }
}
private:
numa<Node*,0> front;
numa<Node*,0> rear;
};

template<>
class numa<Queue,1>{
public: 
    static void* operator new(std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc(1 ,sizeof(Queue),alignof(Queue));
        #else
            p = numa_alloc_onnode(sz* sizeof(Queue), 1);
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
            p= umf_alloc(1 ,sizeof(Queue),alignof(Queue));
        #else
            p = numa_alloc_onnode(sz* sizeof(Queue), 1);
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
		    numa_free(ptr, 1 * sizeof(Queue));
        #endif
    }

    static void operator delete[](void* ptr){
		// cout<<"doing numa free \n";
        #ifdef UMF
			umf_free(1,ptr);
		#else
		    numa_free(ptr, 1 * sizeof(Queue));
        #endif
    }
public:
numa (){
    this->front = __null;
    this->rear = __null;
}
virtual ~numa()
{
	while(front != NULL)
	{
		Node *temp = front;
		front = front->getLink();
		int data = temp->getData();
		delete temp;
	}

}
virtual int del(){
    if (this->front == __null) {
        return -1;
    }
    Node *temp = this->front;
    this->front = this->front->getLink();
    int data = temp->getData();
    delete temp;
    if (this->front == __null) {
        this->rear = __null;
    }
    return data;
}
virtual void add(int initData){
    if (this->front == __null) {
        this->front = reinterpret_cast<Node*>(new numa<Node,1>(initData));
        this->front->setLink(this->rear);
        this->rear = this->front;
        return;
    }
    Node *newNode = reinterpret_cast<Node*>(new numa<Node,1>(initData));
    this->rear->setLink(newNode);
    newNode->setLink(__null);
    this->rear = newNode;
}
virtual void display(){
    Node *temp = this->front;
    while (temp != __null)
        {
            if (temp == this->front) {
                std::cout << "FRONT " << std::endl;
            }
            std::cout << temp->getData() << std::endl;
            temp = temp->getLink();
        }
}
private:
numa<Node*,1> front;
numa<Node*,1> rear;
};

Queue::Queue()
{
	front = NULL;
	rear = NULL;
}

Queue::~Queue()
{
	while(front != NULL)
	{
		Node *temp = front;
		front = front->getLink();
		int data = temp->getData();
		delete temp;
	}

}

int Queue::del()
{
	if(front == NULL)
	{
		return -1;
	}
	Node *temp = front;
	front = front->getLink();
	int data = temp->getData();
	delete temp;

	if(front == NULL)
	{
		rear = NULL;
	}

	return data;

}

void Queue::add(int initData)
{
	if(front == NULL)
	{
		front = new Node(initData);
		front->setLink(rear);
		rear = front;
		return;
	}

	Node *newNode = new Node(initData);
	rear->setLink(newNode);
	newNode->setLink(NULL);
	rear = newNode;

}

void Queue::display()
{
	Node *temp = front;
	while(temp != NULL)
	{
		if(temp == front)
		{
			std::cout << "FRONT " << std::endl;
		}

		std::cout << temp->getData() << std::endl;
		temp = temp->getLink();
	}
}

#endif //_QUEUE_HPP_