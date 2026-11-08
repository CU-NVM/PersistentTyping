#include "rt.hpp"

persistent<persistent<int>*> slots[3];

int main() {
    for (int i = 0; i < 3; i++) slots[i] = new persistent<int>(i * 10);
    for (int i = 0; i < 3; i++) bump(slots[i], "slot");
    std::cout << "objs " << object_count() << "\n";
}
