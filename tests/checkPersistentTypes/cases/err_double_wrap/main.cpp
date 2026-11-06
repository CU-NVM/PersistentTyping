#include "persistenttype.hpp"
struct Node { int v; Node* next; };
persistent<persistent<int>>* a = nullptr;
persistent<persistent<Node*>>* b = nullptr;
persistent<persistent<persistent<Node>>*>* c = nullptr;
int main() {}
