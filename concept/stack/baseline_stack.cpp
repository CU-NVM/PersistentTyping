// baseline_stack.cpp
//
// WHAT THE USER WOULD WRITE WITH RAW PMDK — no persistent<T> type, no
// pmem_ptr, no persistify tool.
//
// Compare against:
//   - stack/user_stack.cpp — the minimal user-facing form (our typing
//     system's input; requires the tool).
//   - Output/stack/user_stack.cpp — what our tool produces from the above.
//
// The persistent-vs-DRAM distinction is spelled out for every field, every
// pointer, every allocation site. Every write must be inside an active
// transaction — the responsibility is on the programmer.
//
// This file COMPILES, RUNS, AND PERSISTS across process restarts.
//
// Build:
//   clang++ -std=c++17 baseline_stack.cpp -o baseline_stack -lpmemobj
//
// Run (after `rm -f /mnt/pmem-emu/baseline_stack.pool` to start fresh):
//   ./baseline_stack     # stack: 30 20 10
//   ./baseline_stack     # stack: 30 20 10 30 20 10   (accumulates)

#include <iostream>
#include <filesystem>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/transaction.hpp>

using namespace pmem::obj;

constexpr const char* POOL_PATH = "/mnt/pmem-emu/baseline_stack.pool";
constexpr const char* POOL_LAYOUT = "baseline_stack";
constexpr size_t POOL_SIZE = 1024 * 1024 * 8;

// User's data types — but persistent from the ground up. `p<int>` for
// scalars (transactional store hooks); `persistent_ptr<T>` for links (stable
// across process restarts).
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

    // Every mutating method must run inside a transaction. Here we wrap
    // internally so the caller doesn't have to remember. Nested transactions
    // are flattened by PMDK, so wrapping is safe under caller-wrapped
    // callsites too.
    void push(pool_base& pop, int v) {
        transaction::run(pop, [&]{
            persistent_ptr<Node> n = make_persistent<Node>(v);
            n->next = top;
            top = n;
            size = size + 1;
        });
    }

    int pop(pool_base& pop_) {
        int result = 0;
        transaction::run(pop_, [&]{
            if (top == nullptr) { result = 0; return; }
            persistent_ptr<Node> old = top;
            result = old->value;
            top = old->next;
            size = size - 1;
            delete_persistent<Node>(old);
        });
        return result;
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
    persistent_ptr<Stack> s;
};

int main() {
    pool<root> pop;
    if (std::filesystem::exists(POOL_PATH)) {
        pop = pool<root>::open(POOL_PATH, POOL_LAYOUT);
    } else {
        pop = pool<root>::create(POOL_PATH, POOL_LAYOUT, POOL_SIZE, 0666);
    }

    auto r = pop.root();

    // Manual find-or-create for the Stack itself.
    transaction::run(pop, [&]{
        if (r->s == nullptr) {
            r->s = make_persistent<Stack>();
        }
    });

    r->s->push(pop, 10);
    r->s->push(pop, 20);
    r->s->push(pop, 30);

    r->s->print();

    pop.close();
    return 0;
}
