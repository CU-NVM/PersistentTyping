#include "rt.hpp"

persistent<int> hits[4];                 // an array of persistent objects by value
persistent<int> runs;                    // a by-value root: fine
persistent<persistent<int>*> ok = new persistent<int>(0);

int main() {}
