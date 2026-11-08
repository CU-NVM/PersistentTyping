#include "persistenttype.hpp"

persistent<int> value;
persistent<persistent<int>*> ok;

int main() { ok = new persistent<int>(0); }
