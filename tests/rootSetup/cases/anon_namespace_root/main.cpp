#include "rt.hpp"

namespace { persistent<persistent<int>*> hits = new persistent<int>(0); }

int main() {
    bump(hits, "hits");
}
