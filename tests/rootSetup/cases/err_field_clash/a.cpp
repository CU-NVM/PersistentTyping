#include "persistenttype.hpp"

persistent<int>* counter;   // external: field `counter`

void init() { counter = new persistent<int>(0); }
