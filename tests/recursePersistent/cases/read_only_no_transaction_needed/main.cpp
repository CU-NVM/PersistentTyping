#include "counter.hpp"
#include <iostream>

persistent<persistent<Counter>*> c = new persistent<Counter>();

int main() {
    c->bump();                              // no user transaction: the method has its own
    std::cout << "value " << c->read() << "\n";
}
