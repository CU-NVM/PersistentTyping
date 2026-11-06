// persistent<int*> would point at a raw int; P(int*) is pmem_ptr<persistent<int>>.
// Rule 2 holds before the tool runs too, so this is compiled untransformed.
// untransformed
// expect: a pmem pointer's target must be persistent<...> too
#include "persistenttype.hpp"
int main() { persistent<int*> p; return p.get() != nullptr; }
