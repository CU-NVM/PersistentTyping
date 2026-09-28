// baseline_hanoi.cpp
//
// WHAT THE USER WOULD WRITE WITH RAW PMDK — no persistent<T> type, no
// pmem_ptr, no persistify tool.
//
// Compare against:
//   - hanoi/user_hanoi.cpp — the minimal user-facing form (our typing
//     system's input; requires the tool).
//   - Output/hanoi/user_hanoi.cpp — what our tool produces from the above.
//
// Three stacks (from/to/aux) live in the pool root. Each disk-move is one
// transaction (pop + push atomic per move). Signature propagation to
// moveDisks has to be done by hand — the params take persistent_ptr<Stack>
// because that's what the persistent stacks are.
//
// This file COMPILES, RUNS, AND PERSISTS. The first run solves the puzzle;
// subsequent runs see s2 already populated and just reprint the state.
//
// Build:
//   clang++ -std=c++17 baseline_hanoi.cpp -o baseline_hanoi -lpmemobj
//
// Run (after `rm -f /mnt/pmem-emu/baseline_hanoi.pool` to start fresh):
//   ./baseline_hanoi
//   ./baseline_hanoi

#include <iostream>
#include <filesystem>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/transaction.hpp>

using namespace pmem::obj;

constexpr const char* POOL_PATH = "/mnt/pmem-emu/baseline_hanoi.pool";
constexpr const char* POOL_LAYOUT = "baseline_hanoi";
constexpr size_t POOL_SIZE = 1024 * 1024 * 8;

struct Node {
    p<int>                  value;
    persistent_ptr<Node>    next;

    Node(int v) : value(v), next(nullptr) {}
};

class Stack {
    persistent_ptr<Node>    top;
    p<int>                  size;
public:
    Stack() : top(nullptr), size(0) {}

    // Bare methods (no internal txn wrap). Caller composes atomicity — this
    // is "option B" from the original design: one transaction per
    // pop+push disk move, not per method call. Matches the numa-style
    // Hanoi where the outer wrapper defines the atomicity granularity.
    void push(int v) {
        persistent_ptr<Node> n = make_persistent<Node>(v);
        n->next = top;
        top = n;
        size = size + 1;
    }

    int pop() {
        if (top == nullptr) return 0;
        persistent_ptr<Node> old = top;
        int v = old->value;
        top = old->next;
        size = size - 1;
        delete_persistent<Node>(old);
        return v;
    }

    bool isEmpty() const { return size == 0; }

    void print() const {
        persistent_ptr<Node> cur = top;
        std::cout << "stack: ";
        while (cur != nullptr) {
            std::cout << cur->value << " ";
            cur = cur->next;
        }
        std::cout << "\n";
    }
};

struct root {
    persistent_ptr<Stack> s1;
    persistent_ptr<Stack> s2;
    persistent_ptr<Stack> s3;
};

// Signature propagation done by hand: params take persistent_ptr<Stack>,
// not raw Stack*. The one-transaction-per-disk-move wrapping is the
// caller's responsibility.
void moveDisks(pool_base& pop, int n,
               persistent_ptr<Stack> from,
               persistent_ptr<Stack> to,
               persistent_ptr<Stack> aux) {
    if (n <= 0) return;
    moveDisks(pop, n - 1, from, aux, to);
    transaction::run(pop, [&]{
        int disk = from->pop();
        to->push(disk);
    });
    moveDisks(pop, n - 1, aux, to, from);
}

int main() {
    pool<root> pop;
    if (std::filesystem::exists(POOL_PATH)) {
        pop = pool<root>::open(POOL_PATH, POOL_LAYOUT);
    } else {
        pop = pool<root>::create(POOL_PATH, POOL_LAYOUT, POOL_SIZE, 0666);
    }

    auto r = pop.root();

    // Manual find-or-create for each of the three stacks.
    transaction::run(pop, [&]{
        if (r->s1 == nullptr) r->s1 = make_persistent<Stack>();
        if (r->s2 == nullptr) r->s2 = make_persistent<Stack>();
        if (r->s3 == nullptr) r->s3 = make_persistent<Stack>();
    });

    // Seed s1 only on the first run (when everything is empty).
    bool freshly_seeded = false;
    transaction::run(pop, [&]{
        if (r->s1->isEmpty() && r->s2->isEmpty() && r->s3->isEmpty()) {
            r->s1->push(3); r->s1->push(2); r->s1->push(1);
            freshly_seeded = true;
        }
    });

    if (freshly_seeded) moveDisks(pop, 3, r->s1, r->s2, r->s3);

    r->s1->print();
    r->s2->print();
    r->s3->print();

    pop.close();
    return 0;
}
