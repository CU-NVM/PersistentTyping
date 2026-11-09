#include "counter.hpp"

persistent<persistent<int>*> counter = new persistent<int>(0);   // defined and created here, used in main.cpp
