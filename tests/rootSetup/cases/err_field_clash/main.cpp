#include "persistenttype.hpp"

int main() {
    persistent<int>* counter = new persistent<int>(1);   // main-local: also field `counter`
    (void)counter;
}
