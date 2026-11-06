#pragma once
#include "persistenttype.hpp"
struct U { int i; };
inline persistent<U*>* bad() { return nullptr; }
