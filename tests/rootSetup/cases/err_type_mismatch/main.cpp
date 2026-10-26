#include "persistenttype.hpp"

extern persistent<long>* x;

int main() { x = new persistent<long>(2); }
