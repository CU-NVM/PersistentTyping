#include "persistenttype.hpp"

static persistent<int>* x;

void f1() { x = new persistent<int>(1); }
