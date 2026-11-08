#pragma once
#include "persistenttype.hpp"

extern persistent<persistent<persistent<int>*>*> counters;
void bump_all(int n);
