// expect: T must be an arithmetic type, an enum, a pointer or a class
#include "persistenttype.hpp"
union U { int i; float f; };
int main() { persistent<U> u; return 0; }
