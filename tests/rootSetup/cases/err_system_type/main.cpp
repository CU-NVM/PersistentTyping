#include <string>
#include "persistenttype.hpp"

persistent<persistent<std::string>*> s;

int main() {
    s = new persistent<std::string>();
}
