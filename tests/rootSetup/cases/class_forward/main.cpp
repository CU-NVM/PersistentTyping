#include "box.hpp"
#include "shape.hpp"
#include "persistenttype.hpp"

struct Here { int v; };   // defined in the .cpp itself

persistent<persistent<Box>*>   box;
persistent<persistent<Shape>*> shape;
persistent<persistent<Here>*>  here;

int main() {
    box   = new persistent<Box>();
    shape = new persistent<Shape>();
    here  = new persistent<Here>();
}
