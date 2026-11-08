#include "persistenttype.hpp"

int main() {
    static persistent<persistent<int>*> counter = new persistent<int>(1);   // static local: also field `counter`
    (void)counter;
}
