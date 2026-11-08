#include "persistenttype.hpp"

extern persistent<persistent<long>*> x;

int main() { x = new persistent<long>(2); }
