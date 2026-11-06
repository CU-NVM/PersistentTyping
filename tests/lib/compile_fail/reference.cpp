// expect: T must be an arithmetic type, an enum, a pointer or a class
#include "persistenttype.hpp"
int main() { persistent<int&>* p = nullptr; return sizeof(*p); }
