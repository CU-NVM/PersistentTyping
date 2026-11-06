#include "persistenttype.hpp"
struct Node { int v; };
persistent<void*>* a = nullptr;
persistent<void (*)(int)>* b = nullptr;
persistent<persistent<const void*>*>* c = nullptr;
int main() {}
