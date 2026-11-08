#include "rt.hpp"

int main() {
    persistent<persistent<int>*> counter = new persistent<int>(0);
    persistent<int>* cursor = counter;   // a DRAM pointer: fine
    (void)cursor;
}
