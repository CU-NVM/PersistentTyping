#include "persistenttype.hpp"

static persistent<int>* x;

void f2() { x = new persistent<int>(2); }
