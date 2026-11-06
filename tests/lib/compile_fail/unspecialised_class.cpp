// A class reachable in transformed code with no generated specialisation.
// expect: was never specialised by persist-tool
#include "persistenttype.hpp"
struct C { int x; };
int main() { persistent<C>* c = new persistent<C>(); return c != nullptr; }
