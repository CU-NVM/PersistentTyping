#include "tree.hpp"
#include "outer.hpp"
#include "persistenttype.hpp"

persistent<Tree<int>>*     tree;
persistent<Outer::Inner>*  inner;

int main() {
    tree  = new persistent<Tree<int>>();
    inner = new persistent<Outer::Inner>();
}
