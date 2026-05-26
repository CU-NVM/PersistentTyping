// user_hanoi.cpp
//
// THE USER-EXPOSED MINIMAL FORM for a persistent Towers of Hanoi solver.
//
// The user writes:
//   - regular Node and Stack classes (DRAM-typed fields and methods)
//   - a recursive moveDisks helper that operates on plain Stack*
//   - persistent<Stack>* s1, s2, s3 = new persistent<Stack>(); at the top level
//   - one transaction wrapping each disk-move (pop + push) at the call site —
//     option B: methods are bare; the caller composes atomicity
//   - a "seed once" guard so re-runs don't re-seed onto an already-solved state
//
// The user does NOT write:
//   - full template specializations for persistent<Stack> or persistent<Node>
//   - a Root struct aggregating the three stack slots
//   - pmem_get_or_create calls or any PMEMoid / pmemobj_direct plumbing
//   - a moveDisks signature taking persistent<Stack>* instead of Stack*
//
// All of those will be inserted by the recursive typer (Phase 5).
//
// This file COMPILES AND RUNS by itself, but does NOT persist across runs.
// Reason: without a full specialization for persistent<Stack>, the generic
// class spec in persistenttype.hpp is used, which inherits from Stack but has
// no operator new — so allocation falls back to global ::operator new (DRAM).
// The transaction wraps are no-ops in this configuration since no writes go
// through any persistent-aware path.
//
// Build:
//   clang++ -std=c++17 -I../../persistentLib user_hanoi.cpp -o user_hanoi -lpmemobj
//
// Run:
//   ./user_hanoi
//   ./user_hanoi
//   # Both invocations print the same output. No persistence.
//
// Compare with: ./transformed_user_hanoi (which DOES persist).

#include "persistenttype.hpp"
#include <iostream>

// ---------- The user's data types (regular C++) ----------

class Node {
public:
    int   value;
    Node* next;
    Node(int v) : value(v), next(nullptr) {}
};

class Stack {
    Node* top  = nullptr;
    int   size = 0;
public:
    void push(int v) {
        Node* n = new Node(v);
        n->next = top;
        top = n;
        size++;
    }
    int pop() {
        if (!top) return 0;
        Node* old = top;
        int v = old->value;
        top = old->next;
        size--;
        delete old;
        return v;
    }
    bool isEmpty() const { return size == 0; }
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

// ---------- The user's algorithm ----------
//
// Classic recursive Hanoi. Each physical disk-move is wrapped in a transaction
// so that the pop + push pair is atomic — a crash mid-move rolls back the pop
// rather than leaving the disk in limbo.

void moveDisks(int n, Stack* from, Stack* to, Stack* aux) {
    if (n <= 0) return;
    moveDisks(n - 1, from, aux, to);
    pmem::obj::transaction::run(pmem_pool(), [&]{
        int disk = from->pop();
        to->push(disk);
    });
    moveDisks(n - 1, aux, to, from);
}

// ---------- The user's program ----------

int main() {
    persistent<Stack>* s1 = new persistent<Stack>();
    persistent<Stack>* s2 = new persistent<Stack>();
    persistent<Stack>* s3 = new persistent<Stack>();

    // Seed s1 only on the first run (when everything is empty). On later runs
    // the persisted state is already solved, so we should not re-seed.
    bool freshly_seeded = false;
    pmem::obj::transaction::run(pmem_pool(), [&]{
        if (s1->isEmpty() && s2->isEmpty() && s3->isEmpty()) {
            s1->push(3); s1->push(2); s1->push(1);
            freshly_seeded = true;
        }
    });

    if (freshly_seeded) moveDisks(3, s1, s2, s3);

    s1->print();
    s2->print();
    s3->print();
    return 0;
}
