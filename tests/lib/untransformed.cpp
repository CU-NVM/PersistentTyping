// untransformed.cpp
//
// Compile-only, WITHOUT PERSIST_TRANSFORMED: user code as written before the
// tool runs must still compile, since the tool has to parse it. persistent<C>
// for an unspecialised class falls back to inheriting C.

#include "persistenttype.hpp"

struct Point {
    int x, y;
    Point(int x, int y) : x(x), y(y) {}
    int sum() const { return x + y; }
};

struct HashNode { int key; HashNode* next; };

persistent<Point>* origin;
persistent<HashNode>** table;

int main() {
    persistent<Point> p(3, 4);
    return p.sum() == 7 ? 0 : 1;
}
