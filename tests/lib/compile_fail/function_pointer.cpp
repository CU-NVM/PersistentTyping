// expect: function pointers cannot live in pmem
#include "persistenttype.hpp"
int main() { persistent<void (*)()> p; return 0; }
