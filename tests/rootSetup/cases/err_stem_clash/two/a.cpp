#include "persistenttype.hpp"

static persistent<persistent<int>*> x;

void f2() { x = new persistent<int>(2); }
