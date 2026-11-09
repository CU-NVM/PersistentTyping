#include "rt.hpp"

void show_a();
persistent<persistent<int>*> from_main = new persistent<int>(1);
persistent<persistent<int>*> unset;      // no initializer: stays null, nothing to create

int main() {
    show_a();
    std::cout << "main " << *from_main << " unset " << (unset ? 1 : 0) << "\n";
}
