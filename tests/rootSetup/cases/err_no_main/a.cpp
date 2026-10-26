#include "persistenttype.hpp"

persistent<int>* x;

void init() { x = new persistent<int>(0); }
