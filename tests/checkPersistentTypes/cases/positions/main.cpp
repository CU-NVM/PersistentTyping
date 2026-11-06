#include "persistenttype.hpp"
struct Node { int v; };
template<class T> struct Box { T* t; };
using Slot = persistent<Node*>;
Box<persistent<persistent<int>*>> bx;
persistent<Node*>* g(persistent<int*>* x) { (void)x; return nullptr; }
int main() {
    auto* p = new persistent<persistent<Node>*>[4];
    auto q = static_cast<persistent<int*>*>(nullptr);
    unsigned long s = sizeof(persistent<persistent<int>>);
    (void)p; (void)q; (void)s;
}
