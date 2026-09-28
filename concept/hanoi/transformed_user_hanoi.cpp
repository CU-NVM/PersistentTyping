// transformed_user_hanoi.cpp
//
// WHAT THE COMPILER PRODUCES FROM user_hanoi.cpp.
//
// The recursive typer's job (see paper §3.3–§3.8, Fig. 5):
//
//   PRIMARY WORK — for each user-defined class T used as persistent<T>,
//   generate a full template specialization `template<> class persistent<T>`
//   with:
//     (a) every field's type recursively wrapped (Node* -> pmem_ptr<persistent<Node>>),
//     (b) every method body rewritten so internal allocations go through
//         persistent<X> types,
//     (c) operator new / delete overloaded to route through pmem_alloc / pmem_free.
//
//   RECURSION — the wrapped field `top` of persistent<Stack> references
//   persistent<Node>, so the typer also emits a full specialization for that.
//
//   SIGNATURE PROPAGATION — moveDisks originally took `Stack*` parameters.
//   Because it is called with `persistent<Stack>*` arguments, the typer rewrites
//   the parameter types throughout the function. This is the same mechanism
//   numa uses to propagate `numa<T,N>` through pointer-typed parameters.
//
//   SECONDARY TOUCHES (persistence-specific, not in the numa case):
//     - aggregate all top-level persistent<T>* declarations into a __pers_root struct,
//     - fetch the root via pmem_root<__pers_root>() at the top of main,
//     - rewrite each `new persistent<T>(args)` into
//       pmem_get_or_create<persistent<T>>(__root->slot, args).
//
// Note: the per-move `transaction::run` in moveDisks is carried through verbatim
// from user code. The typer does not add or remove transaction wrappers; it
// only rewrites types and inserts the Root/find-or-create machinery. The user's
// chosen atomicity granularity (one txn per pop+push pair = "option B") survives
// the transform unchanged.
//
// This file COMPILES, RUNS, AND PERSISTS. The first invocation solves the
// puzzle; subsequent invocations see s2 already non-empty (puzzle done) and
// just reprint the final state.
//
// Build:
//   clang++ -std=c++17 -I../../persistentLib transformed_user_hanoi.cpp -o transformed_user_hanoi -lpmemobj
//
// Run (after `rm -f /mnt/pmem-emu/global_persistent_pool` to start fresh):
//   ./transformed_user_hanoi    # solves: s2 = [1 2 3]
//   ./transformed_user_hanoi    # same state — puzzle is done, no re-solve

#include "persistenttype.hpp"
#include <iostream>

// ---------- Original user types (carried through unchanged) ----------
// Still in the output for two reasons: (a) the typer doesn't remove them —
// they remain valid C++ definitions, just unused by the generated main; (b)
// they document what the user originally wrote.

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

// ---------- TYPER-GENERATED: full specialization for persistent<Node> ----------
// Triggered recursively because persistent<Stack>::top has type
// pmem_ptr<persistent<Node>>. Same shape as Node but with each field wrapped.
// Must appear *before* persistent<Stack> in source order.

template<>
class persistent<Node> {
public:
    persistent<int>             value;
    pmem_ptr<persistent<Node>>  next;

    persistent(int v) : value(v) {}

    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Node)); }
    static void  operator delete(void* p)     { pmem_free(p); }
};

// ---------- TYPER-GENERATED: full specialization for persistent<Stack> ----------
// Triggered by the user's `new persistent<Stack>()` calls in main.
// Fields wrapped, methods rewritten to use persistent types. No internal
// transaction wraps — the user wrote bare-method / caller-wraps (option B),
// and the typer respects that placement.

template<>
class persistent<Stack> {
    pmem_ptr<persistent<Node>> top;
    persistent<int>            size;

public:
    persistent() : size(0) {}

    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Stack)); }
    static void  operator delete(void* p)     { pmem_free(p); }

    void push(int v) {
        persistent<Node>* n = new persistent<Node>(v);
        n->next = top;
        top = n;
        size = size + 1;
    }

    int pop() {
        if (!top) return 0;
        persistent<Node>* old = top.get();
        int v = old->value;
        top = old->next;
        size = size - 1;
        delete old;
        return v;
    }

    bool isEmpty() const { return size == 0; }

    void print() const {
        auto cur = top;
        std::cout << "stack: ";
        while (cur) {
            std::cout << cur->value << " ";
            cur = cur->next;
        }
        std::cout << "\n";
    }
};

// ---------- TYPER-REWRITTEN: moveDisks signature ----------
// Parameter types Stack* -> persistent<Stack>* (propagated from the call site,
// which now passes persistent<Stack>* values). Method dispatch resolves to the
// generated specialization above. Body is otherwise unchanged.

void moveDisks(int n, persistent<Stack>* from, persistent<Stack>* to, persistent<Stack>* aux) {
    if (n <= 0) return;
    moveDisks(n - 1, from, aux, to);
    pmem::obj::transaction::run(pmem_pool(), [&]{
        int disk = from->pop();
        to->push(disk);
    });
    moveDisks(n - 1, aux, to, from);
}

// ---------- TYPER-GENERATED: __pers_root aggregating top-level persistent slots ----------
// One field per top-level `persistent<T>*` declaration in main. Lives at PMDK's
// pool root slot (pmemobj_root), the only location reliably findable across runs.

struct __pers_root {
    pmem_ptr<persistent<Stack>> s1;
    pmem_ptr<persistent<Stack>> s2;
    pmem_ptr<persistent<Stack>> s3;
};

// TYPER-GENERATED: global root pointer, initialized at program startup. Safe
// at file scope because pmem_alloc_init() (constructor-attribute function in
// the library) runs before any C++ static-storage initialization, so the pool
// is already open by the time pmem_root<>() is called here.
__pers_root* __root = pmem_root<__pers_root>();

int main() {
    // TYPER-REWRITTEN: each `new persistent<Stack>()` becomes a find-or-create
    // through the corresponding __pers_root slot. References the global __root
    // above.
    persistent<Stack>* s1 = pmem_get_or_create<persistent<Stack>>(__root->s1);
    persistent<Stack>* s2 = pmem_get_or_create<persistent<Stack>>(__root->s2);
    persistent<Stack>* s3 = pmem_get_or_create<persistent<Stack>>(__root->s3);

    // UNCHANGED FROM USER CODE — the seed-once guard, the call to moveDisks,
    // and the three prints all carry through verbatim. The persistent
    // specializations above ensure isEmpty(), push(), pop(), and print()
    // all operate on pmem-resident state.
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
