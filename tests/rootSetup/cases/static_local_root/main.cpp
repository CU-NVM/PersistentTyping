#include "rt.hpp"

persistent<int>* calls() {
    static persistent<int>* c = new persistent<int>(0);
    return c;
}

int main() {
    bump(calls(), "calls");
}
