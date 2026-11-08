#include "rt.hpp"

namespace { persistent<persistent<int>*> hits; }

int main() {
    hits = new persistent<int>(0);
    bump(hits, "hits");
}
