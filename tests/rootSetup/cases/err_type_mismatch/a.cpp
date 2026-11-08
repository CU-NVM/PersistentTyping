#include "persistenttype.hpp"

persistent<persistent<int>*> x;

void init() { x = new persistent<int>(1); }
