#include "rt.hpp"

persistent<persistent<int>*> hits = new persistent<int>(0);   // external

void fa() {
    bump(hits, "hits");
}
