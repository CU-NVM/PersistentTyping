#include "box.hpp"
#include "shape.hpp"
#include "persistenttype.hpp"

struct Here { int v; };   // defined in the .cpp itself

persistent<Box>*   box;
persistent<Shape>* shape;
persistent<Here>*  here;

int main() {
    box   = new persistent<Box>();
    shape = new persistent<Shape>();
    here  = new persistent<Here>();
}
