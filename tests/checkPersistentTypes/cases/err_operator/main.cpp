#include "persistenttype.hpp"

struct Point {
    int x, y;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

persistent<Point>* p;

int main() { p = new persistent<Point>(); }
