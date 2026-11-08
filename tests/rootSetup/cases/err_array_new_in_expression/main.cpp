#include "rt.hpp"

persistent<persistent<persistent<int>*>*> d;

int main() {
    if ((d = new persistent<persistent<int>*>[2])) {}
}
