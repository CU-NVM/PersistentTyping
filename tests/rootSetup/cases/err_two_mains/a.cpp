#include "persistenttype.hpp"

persistent<int>* x;

int main() { x = new persistent<int>(0); }
