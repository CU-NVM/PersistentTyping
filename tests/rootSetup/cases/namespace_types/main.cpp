#include "types.hpp"
#include "persistenttype.hpp"

using namespace app;

persistent<persistent<Node>*>         node;
persistent<persistent<detail::Leaf>*> leaf;
persistent<persistent<lib::Item>*>    item;

int main() {
    node = new persistent<Node>();
    leaf = new persistent<detail::Leaf>();
    item = new persistent<lib::Item>();
}
