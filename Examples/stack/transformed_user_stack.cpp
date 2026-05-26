// transformed_user_stack.cpp
//
// WHAT THE COMPILER PRODUCES FROM user_stack.cpp.
//
// The recursive typer's job (see paper §3.3–§3.8, Fig. 5):
//
//   PRIMARY WORK — for each user-defined class T used as persistent<T>,
//   generate a full template specialization `template<> class persistent<T>`
//   with:
//     (a) every field's type recursively wrapped (Node* -> pmem_ptr<persistent<Node>>),
//     (b) every method body rewritten so internal allocations go through
//         persistent<X> types and the method is wrapped in transaction::run,
//     (c) operator new / delete overloaded to route through pmem_alloc / pmem_free.
//
//   RECURSION — if a wrapped field references another user-defined type U
//   (e.g. Stack's `next` field is now pmem_ptr<persistent<Node>>), the typer
//   triggers a specialization for persistent<U> too.
//
//   SECONDARY TOUCHES (persistence-specific, not in the numa case):
//     (d) aggregate all top-level persistent<T>* declarations into a __pers_root struct,
//     (e) fetch the root via pmem_root<__pers_root>() at the top of main,
//     (f) rewrite each `new persistent<T>(args)` into
//         pmem_get_or_create<persistent<T>>(__root->slot, args).
//
// This file COMPILES, RUNS, AND PERSISTS. Each invocation pushes 10, 20, 30
// onto the SAME stack that was found in pmem from the previous run.
//
// Build:
//   clang++ -std=c++17 -I../../persistentLib transformed_user_stack.cpp -o transformed_user_stack -lpmemobj
//
// Run (after `rm -f /mnt/pmem-emu/global_persistent_pool` to start fresh):
//   ./transformed_user_stack    # stack: 30 20 10 (size=3)
//   ./transformed_user_stack    # stack: 30 20 10 30 20 10 (size=6)
//   ./transformed_user_stack    # stack: 30 20 10 30 20 10 30 20 10 (size=9)

#include "../../persistentLib/persistenttype.hpp"
#include <iostream>

// ---------- Original user types (carried through unchanged) ----------
// These are still in the output for two reasons: (a) the typer doesn't
// remove them — they remain valid C++ definitions, just unused by the
// generated `main`; (b) they document what the user originally wrote.

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

// ---------- TYPER-GENERATED: full specialization for persistent<Node> ----------
// Triggered recursively because persistent<Stack>::top is pmem_ptr<persistent<Node>>.
// Same shape as Node but with each field wrapped: int -> persistent<int>,
// Node* -> pmem_ptr<persistent<Node>>. Note: must appear *before*
// persistent<Stack> in source order so the latter can use it in method bodies.

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
// Triggered by the user's `new persistent<Stack>()` in main.
// Fields wrapped, methods rewritten to use persistent types and transactions.

template<>
class persistent<Stack> {
    pmem_ptr<persistent<Node>> top;
    persistent<int>            size;

public:
    persistent() : size(0) {}

    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Stack)); }
    static void  operator delete(void* p)     { pmem_free(p); }

    // Method body rewritten: `new Node(v)` -> `new persistent<Node>(v)`,
    // and the whole body wrapped in transaction::run for atomicity + durability.
    void push(int v) {
        pmem::obj::transaction::run(pmem_pool(), [&]{
            persistent<Node>* n = new persistent<Node>(v);
            n->next = top;
            top = n;
            size = size + 1;
        });
    }

    // Read-only — no transaction needed. Walk uses pmem_ptr instead of raw Node*.
    // Note: we deliberately don't print `size` inside this const method.
    // `persistent<int>::operator T&` isn't const-qualified, so accessing `size`
    // (a const member from inside a const method) can't perform the implicit
    // conversion to int&. Walking through `pmem_ptr<>` works because
    // pmem_ptr's operator-> returns a non-const T*. Logged as an open question
    // for the library — see PersistentLib.md §7.
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

// ---------- TYPER-GENERATED: __pers_root aggregating top-level persistent slots ----------
// One field per top-level `persistent<T>*` declaration in the program. Lives at
// PMDK's pool root slot (`pmemobj_root`), which is the only place reliably findable
// across runs.

struct __pers_root {
    pmem_ptr<persistent<Stack>> s;
};

// TYPER-GENERATED: global root pointer, initialized at program startup. The
// pool itself is already open here because pmem_alloc_init() runs as a
// constructor-attribute function before any C++ static-storage init.
__pers_root* __root = pmem_root<__pers_root>();

int main() {
    // TYPER-REWRITTEN: `persistent<Stack>* s = new persistent<Stack>()`
    // becomes a find-or-create through the __pers_root::s slot. References the
    // global __root above.
    persistent<Stack>* s = pmem_get_or_create<persistent<Stack>>(__root->s);

    // UNCHANGED FROM USER CODE — the transaction body carries through verbatim,
    // except that `s->push(N)` now resolves to persistent<Stack>::push (the
    // generated specialization above), which is what does the pmem allocation
    // and the snapshot-into-undo-log dance.
    pmem::obj::transaction::run(pmem_pool(), [&]{
        s->push(10);
        s->push(20);
        s->push(30);
    });

    s->print();
    return 0;
}
