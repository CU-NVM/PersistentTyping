#include "types.hpp"
persistent<persistent<int>*>* in_b = nullptr;
int a();
int main() { return a() + (in_b != nullptr); }
