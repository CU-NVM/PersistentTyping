#include "rt.hpp"

namespace app { persistent<persistent<int>*> hits; }

int main() {
    app::hits = new persistent<int>(0);
    bump(app::hits, "hits");
}
