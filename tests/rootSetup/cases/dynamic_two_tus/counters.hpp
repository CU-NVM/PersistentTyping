#pragma once
#include "persistenttype.hpp"

extern persistent<int>** counters;
void bump_all(int n);
