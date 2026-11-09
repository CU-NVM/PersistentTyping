# `persistentLib/` — the persistent type library

This document is both a user manual and a design record. The first half explains how to use `persistent<T>`, `pmem_ptr<T>`, and the surrounding helpers. The second half walks through the design decisions behind each choice and why we ended up where we did.

The library lives in `~/PersistentTyping/persistentLib/` and consists of two header files:

- [`pmem_allocator.hpp`](../persistentLib/pmem_allocator.hpp) — low-level pool management and allocation primitives.
- [`persistenttype.hpp`](../persistentLib/persistenttype.hpp) — the `persistent<T>` template and its specializations, the `pmem_ptr<X>` alias (`persistent<X*>`), and `pmem_get_or_create`.

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
- A pointer stored in pmem is `persistent<U*>`, spelled `pmem_ptr<U>` (the same type). It stores a PMEMoid (a stable, restart-surviving reference) under the hood and presents `U*` semantics on top.
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

The core wrapper template. Which definition you get depends on T:

| T | Definition | What it is |
|---|---|---|
| arithmetic (`int`, `double`, `char`, ...) or enum | primitive specialization | the value, logged on every write |
| `U*` | pointer specialization | a pmem pointer: stores a PMEMoid; `pmem_ptr<U>` is the same type |
| a class C | a `template<> class persistent<C>` generated by persist-tool | C's fields, each wrapped (see P below) |
| a class C with no generated specialization | fallback | before the tool runs: inherits C, so user code compiles; in transformed code: a compile error |
| anything else (references, unions, arrays, `void`, functions) | primary template | always a compile error |

**Primitive specialization.**

```cpp
template<typename T, template<typename> class Alloc>
class persistent<T, Alloc, /* arithmetic or enum */> {
public:
    T contents;
    T load() const;
    void store(T data);                    // snapshots into the active tx, then writes
    operator const T&() const;             // read-only conversion
    persistent& operator=(const T& data);  // routes through store()
    // ++, --, +=, -=, ... all route through store()
    static void* operator new(std::size_t sz);     // pmem_alloc
    static void* operator new[](std::size_t sz);
    static void  operator delete(void* p);         // pmem_free
    static void  operator delete[](void* p);
};
```

**Pointer specialization: `persistent<U*>`, spelled `pmem_ptr<U>`.**

```cpp
template<typename U, template<typename> class Alloc>
class persistent<U*, Alloc, void> {
    PMEMoid oid_;                          // never a raw address
public:
    persistent();                          // null
    persistent(std::nullptr_t);
    persistent(U* raw);                    // throws if raw is not in the pool
    U* get() const;
    operator U*() const;                   // reads convert to a raw pointer
    U& operator*() const;
    U* operator->() const;
    persistent& operator=(U* raw);         // logged; throws if raw is not in the pool
    persistent& operator=(const persistent& other);
    // operator new/new[]/delete/delete[] in pmem, so slot arrays live in pmem too
};

template<typename X> using pmem_ptr = persistent<X*>;
```

- **One type, two spellings.** `pmem_ptr<pmem_ptr<persistent<HashNode>>>` and `persistent<persistent<persistent<HashNode>*>*>` are the same type. Generated code uses the `pmem_ptr` spelling because it is easier to read.
- **A slot is one `PMEMoid` and nothing else, with no destructor.** So `new pmem_ptr<persistent<HashNode>>[n]`, a bucket array, has no array cookie.
- **Reads need no rewriting.** Because of the implicit conversion to `U*`, `nd->next = table[k]`, `table[k] = nd`, `if (p)`, `p == q`, `cur = cur->next` and `delete[] table` all compile as the user wrote them.
- **Writes are logged.** `operator=` adds the 16-byte slot to the transaction first, but only when the slot itself is in pmem: a `pmem_ptr` copied into a DRAM local needs no log. Outside a transaction, assigning a slot in pmem throws: the caller provides the transaction (see §6.11).
- **A pmem pointer to DRAM cannot exist.** `pmemobj_oid()` returns null for an address outside the pool. The constructor and `operator=` turn that into an exception instead of storing a silent null.
- **The pointee must itself be `persistent<...>` (rule 2).** `persistent<int*>` (a pmem pointer to a raw `int`) is a compile error before and after the transform; write `persistent<persistent<int>*>`. The `static_assert` fires only where the type is completed, so the tool's check-persistent-types pass reports the other spellings (`persistent<int*>* p`, parameters, aliases).

**`PERSIST_TRANSFORMED`.** persist-tool puts `#define PERSIST_TRANSFORMED` (inside `#ifndef`) at the top of every file it finishes transforming. It turns on the checks that only make sense once the tool has run:
- the class fallback becomes `static_assert("persistent<C> was never specialised by persist-tool ...")`, because an object of such a class would otherwise silently live in DRAM;
- `persistent<U*>` requires a persistent pointee.

Untransformed code never sees these checks, because the tool has to parse it first.

**Where a pointer lives decides its form.** Roots and fields of a `persistent<C>` live in pmem, so their pointers are `pmem_ptr`s (OIDs). Locals, parameters, return values and `this` live in DRAM, so they stay raw pointers, e.g. `persistent<HashNode>* nd`. A raw pointer in DRAM that points into pmem is fine. The rule that matters is the one enforced where a pointer *enters* pmem: it is converted to an OID, and it must point into the pool.

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

The workhorse for binding persistent objects to a root slot. `slot` is a `persistent<T*>&`, i.e. a `pmem_ptr<T>`. If the slot is already set, returns the existing pointer; otherwise allocates a fresh `T(args...)` and writes its OID into the slot, all inside an internal transaction. persist-tool's root-setup emits it for every root init site, including each element of a root array: `pmem_get_or_create<persistent<T>>(__pers_get_root()->r[i], args)`.

```cpp
struct Root { pmem_ptr<persistent<Stack>> stack; };
Root* root = pmem_root<Root>();
persistent<Stack>* s = pmem_get_or_create<persistent<Stack>>(root->stack);   // no-arg ctor
```

### C strings: `char*` fields

`pmem_string` is gone (removed 2026-10-27). A `char*` field is no longer a special type. By the ordinary P rule it becomes `pmem_ptr<persistent<char>>`, a pmem pointer to an array of `persistent<char>`. A `char tag[12]` field becomes `persistent<char> tag[12]`. The user writes ordinary C:

```cpp
name = new char[std::strlen(n) + 1];   // -> new persistent<char>[...], in pmem
std::strcpy(name, n);
std::cout << name << " " << std::strlen(name);
name[0] = 'B';                         // persistent<char>::store, logged
delete[] name;                         // pmem_free
```

The C library takes `char*`, so persist-tool wraps each argument that points into pmem in one of two helpers from `persist_detail`. Users never write these:

| Call | Becomes |
|---|---|
| a read: `strlen(name)`, `strcmp(name, s)`, `std::cout << name`, `printf("%s", name)` | `strlen(persist_detail::chars(name))` |
| a write: `strcpy(name, s)` | `strcpy(persist_detail::writable(name, std::strlen(s) + 1), s)` |

- **`chars(p)`** is a `const char*` view. It is read-only, so nothing can be written through it around the undo log. It relies on `persistent<char>` being laid out exactly like `char`, which the library checks with a static_assert.
- **`writable(p, n)`** adds the `n` bytes at `p` to the transaction (one undo entry for the whole range), then hands back `char*`. The tool derives `n` from the function:
  - `strcpy`: `strlen(src) + 1`
  - `strncpy`, `memcpy`, `memmove`, `memset`: the count argument
  - `strcat`: `strlen(dst) + strlen(src) + 1`
  - `strncat`: `strlen(dst) + n + 1`
- **Any other function that takes a non-const `char*` into a pmem string is a tool error.** Its writes could not be logged.
- **So is pointing a pmem pointer at a string literal** (`name = "x"`, or `const char* tag = "x";` as a field initializer). A literal lives in DRAM.

This keeps the one thing `pmem_string` was right about: **bulk writes are logged as one range, not byte by byte.** Per-element logging is right when the elements are meaningful units, such as a `pmem_ptr` in a bucket array or a `persistent<int>` counter. It becomes pathological when the element is one byte of a bulk copy, which is why `writable()` logs the whole range at once. Element-wise writes (`name[i] = c`) still log per element, as any `persistent<char>` write does.

### Arrays, and the size of an array across runs

- A field `int x[5]` becomes `persistent<int> x[5]`; `C* slots[3]` becomes `pmem_ptr<persistent<C>> slots[3]`.
- `new T[n]` inside a persistent class becomes `new P(T)[n]`, allocated in pmem through each specialization's `operator new[]`.
- Root arrays (`persistent<T>* r[N]`, `persistent<T>** r`) are covered in the root-setup section of [clang-tool.md](clang-tool.md).

**Keeping an array's size the same across runs is the program's responsibility.** That covers the `n` of `new T*[n]`, a root array's `N`, and a bucket count. It is the same rule as for any other configuration the persistent data depends on (YCSB's `--hash`, for example). The library stores no length and checks none: the program keeps its own `n` or `nbuckets`, as C++ already requires. **Opening a pool with a different `n` than the one it was built with is undefined, like reading past the end of any C++ array.** A size check could not be implemented reliably anyway: `pmemobj_alloc_usable_size` rounds up to the allocation class, so a 4-slot array accepted `n = 6` in testing.

### Low-level primitives (`pmem_alloc`, `pmem_free`, `pmem_contains`)

These are used internally by `persistent<T>::operator new` and `pmem_ptr<T>`. You shouldn't normally call them directly, but they're available:

- `void* pmem_alloc(size_t size, size_t align)` — `pmemobj_tx_alloc` + `pmemobj_direct`; needs an active transaction.
- `void  pmem_free(void* ptr)` — `pmemobj_oid` + `pmemobj_tx_free`; needs an active transaction.
- `void  pmem_require_tx(const char* what)` — throws "<what> outside a transaction" unless one is active. Every write, allocation and delete in the pool calls it first.
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

**What we did instead**: runtime check. `store()`, `pmem_ptr::operator=`, `new` and `delete` throw `runtime_error` on the first call outside a transaction, through `pmem_require_tx`. (Before 2026-11-08, `store()` relied on `pmemobj_tx_add_range_direct` returning an error, but PMDK aborts the process there instead, and `pmem_ptr::operator=` opened its own transaction.) Loud, immediate, catches the bug at the right place.

Open question for the long term — see §7.

### 6.11 Transactional allocation and deallocation (Phase 2.5)

`pmem_alloc` and `pmem_free` are *tx-aware* — they detect an active transaction via `pmemobj_tx_stage() == TX_STAGE_WORK` and route through the transactional PMDK APIs when one is present:

| Caller context | `pmem_alloc` uses | `pmem_free` uses |
|----------------|-------------------|-------------------|
| Inside a tx (`TX_STAGE_WORK`) | `pmemobj_tx_alloc` | `pmemobj_tx_free` |
| Outside any tx | throws (since 2026-11-08) | throws; inside `operator delete`, which is `noexcept`, this ends in `std::terminate` |

**Since 2026-11-08 there is no outside-a-transaction path** (closure doc to-dos #1 and #2). The non-transactional fallback committed an allocation before the program linked it anywhere, so a crash in between leaked it. The same went for the pmem pointer's old habit of opening a transaction for a lone assignment: each write was atomic, but not the operation it belonged to. The caller provides the transaction (a method's own, `pmem_get_or_create`'s, or the user's `transaction::run`), and the library only checks for it.

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

### 7.5 ~~Allocator's `allocate(n)` semantics for primitive `operator new`~~ — RESOLVED 2026-08-11, see §7.9

The primitive specialization's `operator new` does:
```cpp
return alloc.allocate(sz);   // where sz is sizeof(persistent<T>)
```
but `PersistentAllocator::allocate(n)` returns `n * sizeof(T)` bytes. So this overallocates by a factor of `sizeof(T)`. Not a correctness bug given how the bytes are used (we only write to the first `sizeof(T)` bytes), but it's wasteful and the code reads wrong. Pending cleanup.

### 7.6 ~~Read barriers~~ — write side RESOLVED 2026-08-11, see §7.8

The mutable `operator T&()` described here no longer exists, so the caller can no longer modify contents through the returned reference. What remains open is a genuine *read* barrier: `operator const T&() const` hands back a reference for reading, and we have no hook on the read path. That matters only if a future design needs one (e.g. read-set tracking for a different concurrency model); the crash-consistency story does not depend on it.

### 7.7 Const-correctness of `persistent<T>` accessors — partly RESOLVED 2026-08-11, see §7.8

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

### 7.8 ~~Unlogged writes through the mutable `operator T&()`~~ — RESOLVED 2026-08-11

Writes to a `persistent<T>` scalar used to be able to escape the undo log entirely. `count++` on a `persistent<int>` field found no member `operator++`, so overload resolution fell back to converting via `operator T&()` — handing out a raw `int&` straight at `contents` — and applied the built-in `++` to that reference. `store()` never ran, so `pmemobj_tx_add_range_direct` never ran, so PMDK had no backup and the write could not be rolled back on abort. The same held for `+=`, `--`, and every other compound assignment.

This was benign-looking in the small benchmarks (Stack's `size++`) but would have made the crash-consistency claim false on any real workload: YCSB does `count++` on every insert and update.

Two changes fixed it, both in the primitive specialization:

1. **Member operators added** — `operator++`/`--` (prefix and postfix), and `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`, each implemented as `store(load() op v)`. C++ overload resolution prefers a member operator over conversion-then-builtin, so `count++` now calls the logged path. Defining the full set unconditionally is safe even for pointer and floating-point `T`: member functions of a class template instantiate lazily, so `persistent<Node*>::operator%=` is never compiled unless someone actually writes `ptr %= x`.

2. **The mutable `operator T&()` was removed**, leaving only `operator const T&() const`. This closes the escape hatch rather than merely routing around it — `int& r = *p;` is now a compile error (*no viable conversion from `persistent<int>` to `int`*), and every mutation must go through `operator=`, a compound operator, or `++`/`--`. All four benchmarks still compile and persist unchanged, which suggests the mutable conversion was not carrying its weight.

The resulting property is worth stating precisely: for scalar and pointer `T`, unlogged writes to a `persistent<T>` are now **inexpressible**, not merely discouraged.

Note this also resolves the read-side half of §7.7 — `operator const T&() const` means reads from `const` methods work — while making the write-side moot, since there is no longer a mutable conversion to be missing.

### 7.9 ~~`operator new` passed bytes where a count was expected~~ — RESOLVED 2026-08-11

This is §7.5, found independently and fixed. `persistent<T>::operator new` and `operator new[]` receive a size in **bytes**, but forwarded it to `PersistentAllocator::allocate(n)`, which expects a **count** and multiplies by `sizeof(T)` internally. The byte count was therefore scaled a second time: `new persistent<int>` requested 4 bytes and allocated 16; every `double` allocation wasted 8×. Over-allocation rather than corruption, which is why nothing had broken — and it was exactly correct for `sizeof(T) == 1`, which is why the `persistent<char>` path we were considering for strings would have looked fine.

All four now call `pmem_alloc(sz, alignof(T))` / `pmem_free(ptr)` directly, which also makes the library consistent with the `operator new`/`delete` pair the Clang tool emits into generated specializations.

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
