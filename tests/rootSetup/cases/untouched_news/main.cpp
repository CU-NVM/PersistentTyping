#include "rt.hpp"

struct Holder { persistent<int>* p; };

persistent<int>* root;

// Never called: these allocations sit outside a transaction. They are here
// only to check root-setup leaves them alone.
void never_called(persistent<int>* param) {
    persistent<int>* local = new persistent<int>(1);
    param = new persistent<int>(2);
    Holder h;
    h.p = new persistent<int>(3);
    int* plain = new int(4);
    (void)local; (void)param; (void)plain;
}

int main() {
    root = new persistent<int>(0);
    bump(root, "root");
}
