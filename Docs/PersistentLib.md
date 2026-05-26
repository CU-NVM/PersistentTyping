# `persistentLib/` — the persistent type library

This document is both a user manual and a design record. The first half explains how to use `persistent<T>`, `pmem_ptr<T>`, and the surrounding helpers. The second half walks through the design decisions behind each choice and why we ended up where we did.

The library lives in `~/PersistentTyping/persistentLib/` and consists of two header files:

- [`pmem_allocator.hpp`](../persistentLib/pmem_allocator.hpp) — low-level pool management, allocation primitives, `pmem_ptr<T>`, and helpers.
- [`persistenttype.hpp`](../persistentLib/persistenttype.hpp) — the `persistent<T>` template and its two specializations.

Both are header-only. Programs that use the library include `persistenttype.hpp` (which transitively pulls in `pmem_allocator.hpp`) plus standard C++ headers, and link against `-lpmemobj`.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick start](#2-quick-start)
3. [Type reference](#3-type-reference)
4. [Helper reference](#4-helper-reference)
5. [Usage patterns](#5-usage-patterns)
6. [Design decisions](#6-design-decisions)
7. [Known limitations and open questions](#7-known-limitations-and-open-questions)
8. [Where the Phase 5 typer fits in](#8-where-the-phase-5-typer-fits-in)

---

## 1. Overview

The library lets you mark data as *persistent* — meaning the bytes live in non-volatile memory (NVM, aka pmem) and survive process exit and machine crashes. The basic model:

- You wrap your data type in `persistent<T>`. The wrapper places `T`'s bytes inside a PMDK pool.
- You wrap "pointer to a persistent object" slots in `pmem_ptr<T>`. The wrapper stores a PMEMoid (a stable, restart-surviving reference) under the hood and presents `T*` semantics on top.
- Every write to a `persistent<T>` field happens inside a `transaction::run` block. The library snapshots the prior value into the transaction's undo log so that crashes or aborts cleanly roll back to a consistent state.

What that gets you:

- **Durability** — every committed transaction's writes are guaranteed to reach the pool's underlying storage before the commit returns.
- **Failure atomicity** — if the process dies (or you explicitly abort), the next `pmemobj_open` replays the undo log and brings the pool back to its last committed state.
- **Discoverability across runs** — your top-level persistent objects live at known locations (the pool root), so the next process invocation can find and resume them.

The library is built on top of PMDK (libpmemobj 1.11 + libpmemobj-cpp 1.13) but is designed to be the *primary* abstraction the user sees — PMEMoid, `pmemobj_persist`, and friends are deliberately hidden.

---

## 2. Quick start

### Prerequisites

- A pmem mount at `/mnt/pmem-emu` (or set `PERSISTENT_POOL_PATH` to point elsewhere). On this machine we set this up via `memmap=4G!4G` kernel boot param — see [`EnvironmentSetup.md`](EnvironmentSetup.md).
- `clang++` 17+ (we use system clang 20).
- `libpmemobj-dev` and `libpmemobj-cpp-dev` installed (apt packages).

### Minimum program: a persistent counter

```cpp
#include "persistentLib/persistenttype.hpp"
#include <iostream>

// 1. Declare a root struct holding a slot for each top-level persistent object.
struct Root {
    pmem_ptr<persistent<int>> counter;
};

int main() {
    // 2. Fetch the pool root.
    Root* root = pmem_root<Root>();

    // 3. Find-or-create the counter object. Allocates and initializes on first run;
    //    returns the existing pointer on subsequent runs.
    persistent<int>* counter =
        pmem_get_or_create<persistent<int>>(root->counter, 0);

    // 4. All writes must be inside a transaction.
    pmem::obj::transaction::run(pmem_pool(), [&]{
        *counter = *counter + 1;
    });

    std::cout << "counter = " << *counter << "\n";
    return 0;
}
```

### Build and run

```bash
clang++ -std=c++17 -Ipath/to/persistentLib counter.cpp -o counter -lpmemobj

# First run — pool gets created.
./counter   # counter = 1

# Subsequent runs — pool gets re-opened, counter is found and incremented.
./counter   # counter = 2
./counter   # counter = 3
```

That program demonstrates the core flow: declare a root, fetch it, find-or-create the persistent value, mutate inside a transaction.

A fuller example (a persistent stack with `push`/`pop` methods, demonstrating the class-specialization pattern) is in [`pracitce/ex2_persist_stack.cpp`](../pracitce/ex2_persist_stack.cpp). The user-vs-typer pattern is shown side by side in [`Examples/counter/`](../Examples/counter/).

---

## 3. Type reference

### `persistent<T>`

The core wrapper template. Has two specializations driven by SFINAE:

**Primitive specialization** — selected when `T` is fundamental (`int`, `double`, `char`, ...) or a pointer type.

```cpp
template<typename T, template<typename> class Alloc, ...>
class persistent<T, Alloc, /* primitive case */> {
    T contents;
public:
    T load();
    void store(T data);    // snapshots into active tx, then writes
    operator T&();         // implicit conversion for reads
    persistent& operator=(const T& data);   // routes through store()
    T operator->();        // only valid for pointer T (static_assert)
    static void* operator new(std::size_t sz);     // routes through pmem_alloc
    static void  operator delete(void* p);          // routes through pmem_free
    static void* operator new[](std::size_t sz);
    static void  operator delete[](void* p);
};
```

Usage:
```cpp
persistent<int>* counter = new persistent<int>(0);  // pmem-allocated
*counter = 5;                                        // tx-required write
int x = *counter;                                    // read (no tx needed)
```

**Class specialization** — selected when `T` is a user-defined class type (neither fundamental nor pointer).

```cpp
template<typename T, template<typename> class Alloc, ...>
class persistent<T, Alloc, /* class case */> : public T {
public:
    persistent();
    template<typename... Args>
    explicit persistent(Args&&... args);   // perfect-forwarding to T
};
```

This is the **fallback** — inherits from T directly. For any T actually used persistently in production code, you (or eventually the Phase 5 typer) should provide a full explicit specialization (see §5 below). The generic class spec is intentionally minimal; it has no `operator new`, so a heap-allocated `persistent<T>` falls back to global `::operator new` (DRAM). This is by design — it forces the user to provide a real specialization for any class that actually needs persistence.

### `pmem_ptr<T>`

A 16-byte handle to a pmem-allocated object. Stores a `PMEMoid` internally, exposes `T*` semantics.

```cpp
template<typename T>
class pmem_ptr {
    PMEMoid oid_;
    void snapshot_if_pmem();   // snapshots only when the slot lives in pmem
public:
    pmem_ptr();                              // OID_NULL
    explicit pmem_ptr(T* raw);               // wraps a raw pmem pointer
    pmem_ptr(const pmem_ptr&) = default;     // POD copy
    pmem_ptr(pmem_ptr&&)      = default;

    T* get() const;
    T& operator*() const;
    T* operator->() const;
    explicit operator bool() const;          // not implicit — prevents accidents

    pmem_ptr& operator=(T* raw);             // snapshot + assign
    pmem_ptr& operator=(std::nullptr_t);
    pmem_ptr& operator=(const pmem_ptr& other);

    bool operator==(const pmem_ptr& other) const;
    bool operator!=(const pmem_ptr& other) const;
    bool operator==(std::nullptr_t) const;
    bool operator!=(std::nullptr_t) const;
};
```

The key idea is the `snapshot_if_pmem` helper: when you assign to a `pmem_ptr`, the implementation checks whether the slot itself lives in pmem (via `pmem_contains(&oid_)`). If it does, the slot is part of some persistent object's state, so the write must be undo-logged. If the slot is in DRAM (a transient local variable used as a handle), no snapshot is needed.

Usage:
```cpp
// As a persistent field in a struct that will live in pmem:
struct Root {
    pmem_ptr<persistent<int>> counter;
};

// As a transient handle in a local:
pmem_ptr<persistent<Node>> p = some_field;   // copy ctor, no snapshot (p is in DRAM)
while (p) {
    std::cout << p->value << "\n";
    p = p->next;                              // copy assign on a DRAM pmem_ptr — no snapshot
}
```

---

## 4. Helper reference

### `pmem_pool()`

Returns a reference to the library's `pmem::obj::pool_base`, constructed at pool open time. Pass it to `transaction::run`:

```cpp
pmem::obj::transaction::run(pmem_pool(), [&]{
    /* writes here are part of one atomic transaction */
});
```

### `pmem_root<T>()`

Returns a typed pointer to the pool's root slot. PMDK gives each pool exactly one root, identified by `pmemobj_root`; this template typecasts it.

```cpp
struct Root { /* your schema */ };
Root* r = pmem_root<Root>();
```

Caveat: the root is *one* slot per pool. If two parts of your program call `pmem_root<A>()` and `pmem_root<B>()` with different types, they share storage and overwrite each other. In practice each program has one fixed `Root` schema.

### `pmem_get_or_create<T>(slot, args...)`

The workhorse for binding persistent objects to a root slot. If the slot is already set, returns the existing pointer; otherwise allocates a fresh `T(args...)` and writes its OID into the slot, all inside an internal transaction.

```cpp
struct Root { pmem_ptr<persistent<Stack>> stack; };
Root* root = pmem_root<Root>();
persistent<Stack>* s = pmem_get_or_create<persistent<Stack>>(root->stack);   // no-arg ctor
```

### Low-level primitives (`pmem_alloc`, `pmem_free`, `pmem_contains`)

These are used internally by `persistent<T>::operator new` and `pmem_ptr<T>`. You shouldn't normally call them directly, but they're available:

- `void* pmem_alloc(size_t size, size_t align)` — `pmemobj_alloc` + `pmemobj_direct`.
- `void  pmem_free(void* ptr)` — `pmemobj_oid` + `pmemobj_free`.
- `bool  pmem_contains(const void* ptr)` — checks whether the pointer is in the open pool.

---

## 5. Usage patterns

### Patterns when writing programs by hand

These will eventually be auto-generated by the Phase 5 typer. Until then, you write them yourself.

#### Pattern A: one persistent value (primitive case)

Pure scalar — int, float, pointer, etc.

```cpp
struct Root { pmem_ptr<persistent<int>> x; };

int main() {
    Root* root = pmem_root<Root>();
    auto* x = pmem_get_or_create<persistent<int>>(root->x, 0);

    pmem::obj::transaction::run(pmem_pool(), [&]{
        *x = *x + 1;
    });
}
```

#### Pattern B: one persistent class (full template specialization)

For a user-defined class, write *two* parallel definitions: the regular DRAM class (Stack), and a full template specialization `template<> class persistent<Stack>` whose fields and methods are wrapped versions of the DRAM ones.

```cpp
// Regular DRAM version
class Stack {
public:
    Node* top  = nullptr;
    int   size = 0;
    void push(int v) {
        Node* n = new Node(v);   // DRAM
        n->next = top;
        top = n;
        size++;
    }
    // ... pop, print using raw Node*
};

// Full specialization — what the Phase 5 typer would generate
template<>
class persistent<Stack> {
    pmem_ptr<persistent<Node>> top;
    persistent<int>            size;
public:
    persistent<Stack>() : size(0) {}

    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Stack)); }
    static void  operator delete(void* p)     { pmem_free(p); }

    void push(int v) {
        pmem::obj::transaction::run(pmem_pool(), [&]{
            persistent<Node>* n = new persistent<Node>(v);
            n->next = top;
            top = n;
            size = size + 1;
        });
    }
    // ... pop, print using pmem_ptr<persistent<Node>>
};
```

Then in `main`:

```cpp
struct Root { pmem_ptr<persistent<Stack>> stack; };
auto* s = pmem_get_or_create<persistent<Stack>>(root->stack);
s->push(42);
```

The `persistent<Stack>` specialization wins over the generic class spec (`: public T`) via C++'s specialization-precedence rule. Note source order: any specialization that references `persistent<X>` from inside a method body needs `persistent<X>`'s specialization to be declared *first* in the file.

#### Pattern C: nested types — `persistent<Stack>` contains a `persistent<Node>` link

When a persistent class's fields point to other persistent types, you need to specialize *both*. The Phase 5 typer would emit them recursively; by hand, you write `template<> class persistent<Node>` first, then `template<> class persistent<Stack>` (which uses `persistent<Node>` in its method bodies).

This is exactly the pattern in [`pracitce/ex2_persist_stack.cpp`](../pracitce/ex2_persist_stack.cpp).

### Transaction discipline

Every write to a `persistent<T>` field or `pmem_ptr<T>` slot must be inside `transaction::run`. The library enforces this at runtime: `store()` and `pmem_ptr::operator=` call `pmemobj_tx_add_range_direct`, which returns nonzero if no transaction is active, at which point the library throws `runtime_error`.

Reads (`operator T&`, `pmem_ptr::get()`, `pmem_ptr::operator*`) do *not* require a transaction.

Transactions can be nested arbitrarily — PMDK flattens them, with only the outermost commit being durable. So `Stack::push` can open its own transaction even if it's called from a function that already has one open.

### Why every write needs a transaction

The library's `store()` method does:
```cpp
inline void store(T data) {
    int rc = pmemobj_tx_add_range_direct(&contents, sizeof(contents));
    if (rc != 0) throw std::runtime_error("Failed to add range to transaction");
    contents = data;
}
```

`pmemobj_tx_add_range_direct` snapshots the *current* bytes into the active transaction's undo log before they're overwritten. On commit the log is discarded; on abort or crash, the log replays and restores the prior value. This is what gives both durability and atomicity in one mechanism.

If you want a single durable write without atomicity, you'd want to use `pmemobj_persist` directly — but in practice you almost always want both, and wrapping every operation in `transaction::run` is the right idiom.

---

## 6. Design decisions

This section walks through *why* the library looks the way it does. Each subsection is one decision we made, the alternatives we considered, and the reasoning.

### 6.1 Why a single global PMDK pool

The library opens *one* PMDK pool at process start, named via the `PERSISTENT_POOL_PATH` environment variable (defaulting to `/mnt/pmem-emu/global_persistent_pool`). All `pmem_alloc` calls go to this pool.

**Alternative**: per-region pools (Atlas's "named persistent regions"), multiple pools, etc.

**Why one**: keeps the API surface small. The numa library (`~/NUMATyping/numaLib/`) takes the same approach — one allocator state per process. If users need multiple pools later we can add it, but every program we've written so far fits in one pool comfortably.

### 6.2 Wrapper type vs raw allocator

The library exposes both a *wrapper type* (`persistent<T>`) and lower-level *allocators* (`pmem_alloc`, `PersistentAllocator`). Users mostly interact with the wrapper.

**Why a wrapper**: the type *itself* carries the intent. Reading `persistent<int>` tells you immediately that the int is durable; reading `int*` does not. This is the entire point of the introspective-typing approach borrowed from the numa paper.

**The wrapper does more than allocate**: it intercepts every write through `store()` to snapshot into the active transaction. A raw allocator can't do that.

### 6.3 Two specializations: primitive vs class

`persistent<T>` is split via SFINAE into:
- A primitive specialization for `is_fundamental || is_pointer` types (int, double, T*, etc.).
- A class specialization for user-defined types (everything else).

**Why split**: their semantics differ.
- For primitives, `persistent<int>` is a *box* containing an int. It has `load()`/`store()` methods to read/write the contents.
- For classes, `persistent<Stack>` *is* a Stack (via inheritance in the generic fallback, or via full specialization in production code). Methods are inherited or re-declared, fields are member variables.

Trying to use one specialization for both would have made the wrapper either too dumb for classes (no method dispatch) or too clunky for primitives (a `load()` method to read an int is overkill).

### 6.4 `store()` routes through the active PMDK transaction

The decision that collapsed Phase 2 and Phase 3 into one mechanism.

**Originally planned**: Phase 2 was supposed to be Mnemosyne-style — `store()` would call `pmemobj_persist` (just CLWB + SFENCE, no atomicity). Then Phase 3 would layer atomicity on top using some failure-atomicity model to be selected later (Atlas, Mnemosyne, or Clobber-NVM).

**What we changed (2026-05-13)**: instead of separating the two concerns, have `store()` call `pmemobj_tx_add_range_direct` — which snapshots into the *active* PMDK transaction's undo log. This gives both durability *and* atomicity from the same mechanism.

**Cost**: every write requires an active transaction. Outside one, `tx_add_range_direct` returns nonzero and `store()` throws. The user has to wrap operations in `transaction::run`.

**Benefit**: no separate Phase 3. PMDK transactions are well-tested, fast, and we get crash recovery for free.

### 6.5 Our own `pmem_ptr<T>` (rather than PMDK's `persistent_ptr<T>`)

`pmem::obj::persistent_ptr<T>` from libpmemobj-cpp does essentially what our `pmem_ptr<T>` does — wraps a PMEMoid and exposes `T*` semantics. Why didn't we just use theirs?

**Reasoning**: we want `persistent<T>` to be *the* persistence type marker. If we used PMDK's `persistent_ptr<T>` for our pointer slots, we'd have two competing wrappers in the same codebase, both meaning "persistent." Our own `pmem_ptr<T>` is small (~30 lines), specific to what we need, and consistent with the type-system philosophy of the library.

**Carve-out**: we *do* use `pmem::obj::transaction::run` (the C++ transaction wrapper). That's because it's a control-flow utility, not a data-abstraction wrapper — it doesn't compete with `persistent<T>` for what types should look like.

### 6.6 `snapshot_if_pmem` — one class handles both pmem and DRAM slots

`pmem_ptr<T>` works correctly whether the slot itself lives in pmem (as a field of a persistent struct) or in DRAM (as a local stack variable used as a transient handle). The trick: every mutator calls a private `snapshot_if_pmem` helper that uses `pmem_contains(&oid_)` to decide whether to snapshot.

**Why this matters**: we don't need two separate classes ("pmem-resident pmem_ptr" vs "DRAM-resident handle"). The same class works in both contexts. A DRAM-local `pmem_ptr<T>` for iterating a list just skips the snapshot; a field-of-persistent-struct `pmem_ptr<T>` does the snapshot correctly.

### 6.7 `pmem_get_or_create` instead of magic in `new`

Users would love `new persistent<int>(0)` to magically be a find-or-create that survives across runs. But C++ semantics rule this out: `new T(args)` always runs the constructor on the returned memory, with no way for `operator new` to signal "skip construction, this object already exists."

**Workarounds we considered**: a magic-number-in-constructor trick (ugly, polluting); a two-phase construction via thread-local flags (fragile, racy). None work cleanly.

**What we did**: a separate function template, `pmem_get_or_create<T>(slot, args...)`. It can branch: if the slot is set, return the existing pointer (no construction); else `new T(args...)` and write the OID. This is the only way to express conditional construction within standard C++.

The recursive typer (Phase 5) will rewrite `persistent<int>* x = new persistent<int>(0)` in user code into a call to `pmem_get_or_create<persistent<int>>(slot, 0)` with the appropriate root binding. The user keeps the natural-looking source; the function-call form is generated by the compiler.

### 6.8 Full template specialization (not inheritance) for class types

The library *ships* an inheritance-based generic class spec (`class persistent<T> : public T`) as a fallback. But for any T that actually needs persistence in production, you (or the typer) should provide a full template specialization, `template<> class persistent<T> { ... }`, with:

- Fields recursively wrapped (`Node* next` → `pmem_ptr<persistent<Node>> next`).
- Methods rewritten to allocate `persistent<X>` types and wrap bodies in `transaction::run`.
- `operator new`/`delete` routing through `pmem_alloc`/`pmem_free`.

**Why not just inheritance**: inheritance creates an implicit upcast (`Stack* q = persistent_stack_ptr;`) that silently routes calls to the non-persistent method. For numa that's a locality bug; for persistent it's a correctness bug (the new Node goes to DRAM, vanishes on crash). Requiring explicit `reinterpret_cast` makes mis-use deliberate.

This mirrors the pattern in the numa paper (§3.5, Fig. 5(b)) — `numa<Stack,0>` is a full specialization, not a child of `Stack`. See [`Output/DataStructureTests/include/BinarySearch.hpp`](../Output/DataStructureTests/include/BinarySearch.hpp) for the typer's actual output on a real data structure.

### 6.9 Layout incompatibility (different from numa)

The numa paper uses `reinterpret_cast<Stack*>(new numa<Stack,0>())` to coerce between the qualified and unqualified types, exploiting the fact that `numa<T*,0>` and `T*` have the *same byte layout* (both are 8-byte pointers).

For us, this doesn't work. `pmem_ptr<persistent<Node>>` is 16 bytes (a PMEMoid), not 8 bytes. So `Stack` and `persistent<Stack>` have *different* field layouts and aren't reinterpret-cast-compatible.

**Implication**: methods of `persistent<Stack>` cannot transparently delegate to `Stack`. They have to use the persistent types directly throughout their bodies. No cast trick is available.

**Is this a problem?** For programs that live entirely in the persistent-typed universe (like Hanoi), no. For programs that need to interoperate with non-persistent libraries that take a `Stack*`, yes — we'd need an explicit deep-copy or a different escape hatch. Open question (see §7).

### 6.10 Why no compile-time enforcement of "writes inside a transaction"

A stronger design would make `store()` only callable when a "transaction token" type is in scope, so missing the wrapper *fails to compile*. The C++ pattern: every method that writes takes a `TxToken&` parameter that can only be obtained from `TX_BEGIN`-equivalent.

**Why we didn't**: it would change every call site. `*counter = 5` becomes `counter.store(5, tx)`. The API surface ripples through every user-facing line. The cost of "stronger" guarantees was too high for Phase 2.

**What we did instead**: runtime check. `store()` and `pmem_ptr::operator=` throw `runtime_error` on the first call outside a transaction. Loud, immediate, catches the bug at the right place.

Open question for the long term — see §7.

### 6.11 Transactional allocation and deallocation (Phase 2.5)

`pmem_alloc` and `pmem_free` are *tx-aware* — they detect an active transaction via `pmemobj_tx_stage() == TX_STAGE_WORK` and route through the transactional PMDK APIs when one is present:

| Caller context | `pmem_alloc` uses | `pmem_free` uses |
|----------------|-------------------|-------------------|
| Inside a tx (`TX_STAGE_WORK`) | `pmemobj_tx_alloc` | `pmemobj_tx_free` |
| Outside any tx | `pmemobj_alloc` | `pmemobj_free` |

**Why both alloc and free have to be tx-aware**: it's about keeping the whole object lifecycle in the same rollback set.

For *allocation*: before this fix, `new persistent<Node>(v)` inside a transaction allocated via the non-tx `pmemobj_alloc`. The writes to the new Node's fields *were* snapshotted (via `store()` and `pmem_ptr::operator=`), but the allocation itself was outside PMDK's transaction system. So on abort, the writes rolled back, but the freshly-allocated chunk remained — orphaned in the pool. Each crashed `push` leaked one Node-sized region. Demonstrated empirically by running the crash test in a loop: pool space monotonically decreased.

After the fix, `pmemobj_tx_alloc` registers the allocation with the active transaction. On abort, the alloc gets reclaimed automatically. Same correctness, no leak.

For *deallocation*: the symmetric case. Inside `Stack::pop` we do `delete old;` (which routes to `pmem_free`). Without `pmemobj_tx_free`, this would free `old` immediately. If the transaction subsequently aborts, the `top` pointer rolls back to `old` — but `old` has already been freed. *Use-after-free on abort*. With `pmemobj_tx_free`, the free is deferred until commit; on abort, it's canceled.

So the unified rule is: **inside a transaction, the entire object lifecycle (alloc and free) must be transactional, so abort uniformly cancels everything that happened.** Half-tracked semantics produce either leaks (alloc not rolled back) or use-after-free (free not deferred).

The detection is just `pmemobj_tx_stage()` — a simple state query, no extra plumbing. The library's allocators automatically do the right thing in either context, so users never have to think about which PMDK API to call.

---

## 7. Known limitations and open questions

### 7.1 Compile-time transaction enforcement

The current "writes must be in a transaction" rule is enforced at runtime. A stronger compile-time scheme would catch missed wrappers before the program ever runs. Cost is significant API churn (every write becomes `counter.store(5, tx)`). Deferred indefinitely.

### 7.2 ~~Transactional allocation (Phase 2.5)~~ — RESOLVED 2026-05-16

`pmem_alloc` and `pmem_free` now detect an active transaction via `pmemobj_tx_stage()` and route through `pmemobj_tx_alloc` / `pmemobj_tx_free` when one is in progress. See §6.11 for the full design rationale. Verified by re-running the [`pracitce/ex2_persist_stack.cpp`](../pracitce/ex2_persist_stack.cpp) recovery test after the fix — correctness preserved (rollback still works), and allocations inside the crashed transaction are now reclaimed instead of leaked.

### 7.3 Layout casts (cross-type interop)

`persistent<Stack>` cannot be `reinterpret_cast`'d to `Stack*` due to the 16-byte `pmem_ptr` vs 8-byte raw pointer mismatch. For code that needs to pass a persistent-managed object to legacy code expecting a non-persistent type, we don't yet have a clean answer.

Options: explicit deep-copy through a different code path; refuse such crossover at the type level; build a wrapper that exposes a non-persistent view.

### 7.4 Case 3 (DRAM pointer to pmem object) safety model

We allow `persistent<T>*` as a transient DRAM handle to a pmem object. NV-Heaps (Coburn et al., ASPLOS '11) discusses safety properties for this pattern — things like garbage collection or restricted pointer types. We haven't read the paper deeply yet; might or might not adopt their model.

### 7.5 Allocator's `allocate(n)` semantics for primitive `operator new`

The primitive specialization's `operator new` does:
```cpp
return alloc.allocate(sz);   // where sz is sizeof(persistent<T>)
```
but `PersistentAllocator::allocate(n)` returns `n * sizeof(T)` bytes. So this overallocates by a factor of `sizeof(T)`. Not a correctness bug given how the bytes are used (we only write to the first `sizeof(T)` bytes), but it's wasteful and the code reads wrong. Pending cleanup.

### 7.6 Read barriers

`operator T&` returns a reference, which lets the caller modify the contents bypassing `store()`. We don't currently protect against this. A read barrier would catch the case but at considerable API cost. For now, document the constraint and trust the typer not to emit such code.

### 7.7 Const-correctness of `persistent<T>` accessors

`persistent<T>::operator T&` and `persistent<T>::load()` (primitive specialization) are *not* const-qualified. So they can't be invoked on a `const persistent<T>` instance — which is exactly the situation inside a `const` method that has a `persistent<T>` field. For example:

```cpp
template<>
class persistent<Stack> {
    persistent<int> size;
public:
    void print() const {
        std::cout << size << "\n";   // FAILS — operator T& isn't const
    }
};
```

Walking through a `pmem_ptr<>` (whose `operator->` *is* const-correct) sidesteps this because `pmem_ptr<T>::operator->()` returns a non-const `T*`, even when called on a const `pmem_ptr`. But direct member access from a const method bumps into the missing overload.

The fix is a 2-line library change — add `operator const T&() const` and `T load() const` to the primitive specialization. Discovered during [`Examples/stack/transformed_user_stack.cpp`](../Examples/stack/transformed_user_stack.cpp); both examples currently work around it by not printing the size field from inside a const method.

---

## 8. Where the Phase 5 typer fits in

The library is designed to be the *runtime* for a Clang-based source-to-source transformer (in `~/PersistentTyping/numa-clang-tool/`). The transformer is Phase 5 of the project; not yet implemented for `persistent<>`, but the existing `RecursiveNumaTyper` is a working precedent for the analogous numa case.

**The typer's primary work** (per the numa paper §3.3–§3.8, Fig. 5):

For every user-defined class T used as `persistent<T>` in the input, the typer generates a *full template specialization* `template<> class persistent<T>` with:
1. Every field's type recursively wrapped (`Node* next` → `pmem_ptr<persistent<Node>> next`).
2. Every method body rewritten so internal allocations go through `new persistent<X>(...)` and the method is wrapped in `transaction::run`.
3. `operator new`/`delete` overloaded to route through `pmem_alloc`/`pmem_free`.

If wrapping a field's type T introduces a reference to another user-defined type U, the typer triggers a specialization for `persistent<U>` as well — recursive triggering.

**Secondary, persistence-specific touches** (not in the numa case):
- Aggregate all top-level `persistent<T>*` declarations in the program into a single typer-generated `__pers_root` struct.
- Insert a `pmem_root<__pers_root>()` fetch at the top of `main`.
- Rewrite each top-level `persistent<T>* x = new persistent<T>(args)` into `persistent<T>* x = pmem_get_or_create<persistent<T>>(__root->x, args)`.

**What the user writes (typer input)**:
```cpp
persistent<Stack>* s = new persistent<Stack>();
pmem::obj::transaction::run(pmem_pool(), [&]{
    s->push(42);
});
```

**What the typer emits** (paraphrasing):
```cpp
// Generated specializations for persistent<Node>, persistent<Stack>:
template<> class persistent<Node> { /* ... wrapped fields, operator new, methods ... */ };
template<> class persistent<Stack> { /* ... ditto ... */ };

// Generated Root:
struct __pers_root { pmem_ptr<persistent<Stack>> s; };

int main() {
    __pers_root* __root = pmem_root<__pers_root>();
    persistent<Stack>* s = pmem_get_or_create<persistent<Stack>>(__root->s);
    pmem::obj::transaction::run(pmem_pool(), [&]{
        s->push(42);
    });
}
```

The library provides all the primitives the typer needs. The typer's job is composition: take a minimal user-facing form and emit the boilerplate that makes it actually persist.

See [`Examples/counter/`](../Examples/counter/) for a hand-written before/after pair on the primitive case, and [`pracitce/ex2_persist_stack.cpp`](../pracitce/ex2_persist_stack.cpp) for a hand-written specialization pair on a real data structure.
