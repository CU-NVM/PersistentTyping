// expect: a pmem pointer must have a pointee type
#include "persistenttype.hpp"
int main() { persistent<void*> p; return 0; }
