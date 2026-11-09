#include "box.hpp"
#include "shape.hpp"
#include "persistenttype.hpp"

struct Here { int v; };   // defined in the .cpp itself

persistent<persistent<Box>*> box = new persistent<Box>();
persistent<persistent<Shape>*> shape = new persistent<Shape>();
persistent<persistent<Here>*> here = new persistent<Here>();

int main() {
}
