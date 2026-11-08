#include "persistenttype.hpp"

persistent<persistent<int>*> x;

int main() { x = new persistent<int>(0); }
