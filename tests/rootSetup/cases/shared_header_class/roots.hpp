#pragma once
#include "rt.hpp"

struct Counter { int hits; Counter() : hits(0) {} };

extern persistent<persistent<Counter>*> visits;   // defined in main.cpp
int hits_in_b();
