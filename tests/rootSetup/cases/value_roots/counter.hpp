#pragma once
#include "rt.hpp"

struct Counter {
    int start, hits;
    Counter(int s) : start(s), hits(0) {}
};
