#include "rt.hpp"

namespace app { persistent<persistent<int>*> hits = new persistent<int>(0); }

int main() {
    bump(app::hits, "hits");
}
