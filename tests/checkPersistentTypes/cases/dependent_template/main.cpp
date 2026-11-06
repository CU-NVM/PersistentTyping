#include "persistenttype.hpp"
template<class T> struct Holder { persistent<T>* p = nullptr; };
int main() { Holder<int> h; return h.p != nullptr; }
