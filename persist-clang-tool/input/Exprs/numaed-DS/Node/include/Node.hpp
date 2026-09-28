#ifndef _NODE_HPP_
#define _NODE_HPP_


#include "numatype.hpp"

class Node
{
private:
	int data;
	Node *link;

public:
	Node() : data(0)
	{}
	Node(int);
	Node(int, Node*);
	virtual Node *getLink(){ return link; }
	virtual void setLink(Node *n) { link = n; }

	virtual int getData(){ return data; }
	virtual void setData(int n){ data = n; }

	virtual ~Node();
};

Node::Node(int initData)
{
	data = initData;
}

Node::~Node()
{
	link = nullptr;
}

Node::Node(int initData, Node *node)
{
	data = initData;
	link = node;
}

template<>
class numa<Node, 0>: public Node {
	public: 
    static void* operator new(std::size_t sz){
        std::cout<<"new operator called"<<std::endl;
		 void* p = numa_alloc_onnode(sz * sizeof(Node), 0);
        if (p == nullptr) {
            throw std::bad_alloc();
        }
        return p;
    }

    static void* operator new[](std::size_t sz){
		std::cout<<"new operator called"<<std::endl;
		 void* p = numa_alloc_onnode(sz * sizeof(Node), 0);
        if (p == nullptr) {
            throw std::bad_alloc();
        }
        return p;
    }

    static void operator delete(void* ptr){
		std::cout<<"delete operator called"<<std::endl;
		numa_free(ptr, 1 * sizeof(Node));
    }

    static void operator delete[](void* ptr){
		std::cout<<"delete operator called"<<std::endl;
		numa_free(ptr, 1 * sizeof(Node));
    }
private:
	numa<int,1> data;
	numa<Node*,1> link;
public:
	numa(){
		data=0;
		link = nullptr;
	}
	numa(int initData){
		data = initData;
	}
	numa(int initData, Node* node){
		data= initData;
		link = node;
	}

	Node *getLink(){ return link; }
	void setData(int n){ data = n; }
	void setLink(Node *n) { std::cout<< "hello"<<std::endl; link = n; }
	int getData(){ return data; }
	
	virtual ~numa(){
		std::cout<< "bye"<<std::endl;
		link = NULL;
	}
};



#endif //_NODE_HPP_