#pragma once
#include "persistenttype.hpp"

class Counter {
    int value;
public:
    Counter() : value(0) {}
    int read() const { return value; }      // read-only: runs without a transaction
    void bump() { value++; }                // writes: its own transaction
};
