#pragma once
#include "persistenttype.hpp"
struct Node { int v; };
inline persistent<persistent<Node>*>* first(persistent<persistent<Node>*>* slots) { return slots; }
