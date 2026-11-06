#include "types.hpp"
persistent<persistent<int>*>* in_a = nullptr;
int a() { return in_a != nullptr; }
