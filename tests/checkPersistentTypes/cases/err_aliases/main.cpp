#include "persistenttype.hpp"
struct Node { int v; };
using NodePtr = Node*;
using PNode = persistent<Node>;
template<class T> using P = persistent<T>;
template<class T> using Ptr = persistent<T*>;
persistent<NodePtr>* a = nullptr;
persistent<PNode*>* b = nullptr;
P<Node*>* c = nullptr;
Ptr<Node>* d = nullptr;
P<P<int>>* e = nullptr;
Ptr<PNode>* f = nullptr;
int main() {}
