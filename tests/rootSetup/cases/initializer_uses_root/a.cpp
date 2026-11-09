#include "roots.hpp"

// Uses b.cpp's roots. a.cpp is given first, so file order alone would create
// `table` while `config` is still null.
persistent<persistent<int>*> table = new persistent<int>(*config * scale);

int main() {
    std::cout << "table " << *table << "\n";
}
