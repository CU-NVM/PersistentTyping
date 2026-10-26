#include "persistenttype.hpp"

template<class K> struct Local { K key; };

persistent<Local<int>>* local;

int main() {
    local = new persistent<Local<int>>();
}
