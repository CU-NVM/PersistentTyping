#include "persistenttype.hpp"
struct Node { int v; Node* next; };
persistent<Node*> head;
int main() {
    auto* p = new persistent<int*>[4];
    unsigned long s = sizeof(persistent<Node*>);
    (void)p; (void)s;
}
