
#ifndef _NODE_HPP_
#define _NODE_HPP_

class Node {
public:
    int   value;
    Node* next;
    Node(int v) : value(v), next(nullptr) {}
};


#endif //_NODE_HPP_