#include "table.hpp"
#include <iostream>

persistent<persistent<HashTable>*> ht = new persistent<HashTable>(8);

int main() {
    for (uint64_t k = 0; k < 20; k++) ht->insert(k, 4);
    ht->touch(3);
    std::cout << "get " << ht->get(3, 4) << " miss " << ht->get(99, 4) << " longest " << ht->longest()
              << " hist " << ht->histogram() << "\n";
}
