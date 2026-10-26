#include "rt.hpp"

persistent<int>* hits;   // external

void fa() {
    hits = new persistent<int>(0);
    bump(hits, "hits");
}
