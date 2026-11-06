#include "persistenttype.hpp"
struct Node { int v; Node* next; };
using PNode = persistent<Node>;
void f() {
    persistent<persistent<Node>*>* b = nullptr;
    pmem_ptr<persistent<Node>>* c = nullptr;
    persistent< persistent< Node > * >* d = nullptr;
    persistent<PNode*>* e = nullptr;
    persistent<persistent<int>*>* i = nullptr;
    persistent<Node>* g = nullptr;
    persistent<persistent<persistent<Node>*>*>* h = nullptr;
    persistent<const persistent<char>*>* s = nullptr;
    (void)b; (void)c; (void)d; (void)e; (void)i; (void)g; (void)h; (void)s;
}
int main() { f(); }
