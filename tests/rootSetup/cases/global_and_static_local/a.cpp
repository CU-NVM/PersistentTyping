#include "persistenttype.hpp"

persistent<persistent<int>*> counter;   // external: field `counter`

void init() { counter = new persistent<int>(0); }
