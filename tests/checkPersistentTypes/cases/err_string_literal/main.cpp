#include "persistenttype.hpp"

struct Label {
    const char* tag = "default";
    const char* name;
    Label() { name = "fixed"; }
};

persistent<Label>* label;

int main() { label = new persistent<Label>(); }
