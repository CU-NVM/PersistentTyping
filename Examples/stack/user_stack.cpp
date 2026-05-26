// user_stack.cpp
//
// THE USER-EXPOSED MINIMAL FORM for a persistent stack.
//
// The human writes:
//   - regular Node and Stack classes (DRAM-typed fields and methods)
//   - persistent<Stack>* s = new persistent<Stack>(); at the top level
//   - transaction-wrapped writes
//
// The user does NOT write:
//   - a full template specialization for persistent<Stack> or persistent<Node>
//   - a Root struct
//   - a pmem_get_or_create call
//   - any PMEMoid / pmemobj_direct / pmemobj_oid plumbing
//
// All of those will be inserted by the recursive typer (Phase 5).
//
// This file COMPILES AND RUNS by itself, but it does NOT persist across runs.
// Reason: without a full specialization for persistent<Stack>, the generic
// class spec in persistenttype.hpp is used, which inherits from Stack but has
// no operator new — so allocation falls back to global ::operator new (DRAM).
// Stack's inherited push() then allocates regular Nodes on the heap (also
// DRAM). The transaction wrap is a no-op in this configuration since no
// writes go through any persistent-aware path.
//
// Build:
//   clang++ -std=c++17 -I../../persistentLib user_stack.cpp -o user_stack -lpmemobj
//
// Run:
//   ./user_stack
//   ./user_stack
//   # Both invocations print the same output. No persistence.
//
// Compare with: ./transformed_user_stack (which DOES persist).

#include "../../persistentLib/persistenttype.hpp"
#include <iostream>

// ---------- The user's data types (regular C++) ----------

class Node {
public:
    int   value;
    Node* next;
    Node(int v) : value(v), next(nullptr) {}
};

class Stack {
public:
    Node* top  = nullptr;
    int   size = 0;

    void push(int v) {
        Node* n = new Node(v);
        n->next = top;
        top = n;
        size++;
    }

    void print() const {
        Node* cur = top;
        std::cout << "stack: ";
        while (cur) {
            std::cout << cur->value << " ";
            cur = cur->next;
        }
        std::cout << "\n";
    }
};

// ---------- The user's program ----------

int main() {
    persistent<Stack>* s = new persistent<Stack>();

    pmem::obj::transaction::run(pmem_pool(), [&]{
        s->push(10);
        s->push(20);
        s->push(30);
    });

    s->print();
    return 0;
}
