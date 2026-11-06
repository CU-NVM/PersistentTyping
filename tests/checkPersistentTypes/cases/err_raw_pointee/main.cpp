#include "persistenttype.hpp"
struct Node { int v; Node* next; };
template<class T> struct Box { T* t; };
persistent<Node*>* a = nullptr;
persistent<int*>* b = nullptr;
persistent<Node**>* c = nullptr;
persistent<const char*>* d = nullptr;
persistent<persistent<Node*>*>* e = nullptr;
using Slot = persistent<Node*>;
Box<persistent<int*>> bx;
persistent<Node*>* g(persistent<int*>* x) { (void)x; return nullptr; }
persistent<persistent<Node>*>* ok = nullptr;
int main() {}
