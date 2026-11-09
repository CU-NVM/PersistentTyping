#include "types.hpp"
#include "persistenttype.hpp"

using namespace app;

persistent<persistent<Node>*>         node = new persistent<Node>();
persistent<persistent<detail::Leaf>*> leaf = new persistent<detail::Leaf>();
persistent<persistent<lib::Item>*>    item = new persistent<lib::Item>();

int main() {
}
