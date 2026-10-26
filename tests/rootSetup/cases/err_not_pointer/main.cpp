#include "persistenttype.hpp"

persistent<int> value;
persistent<int>* ok;

int main() { ok = new persistent<int>(0); }
