#include "rt.hpp"

extern persistent<persistent<int>*> y;
persistent<persistent<int>*> x = new persistent<int>(*y);
persistent<persistent<int>*> y = new persistent<int>(*x);

int main() {}
