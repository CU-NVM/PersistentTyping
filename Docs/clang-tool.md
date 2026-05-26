# Phase 5 Clang Tool — recursive `persistent<T>` typer

Planning doc for the source-to-source compiler that takes a user-facing program
using `persistent<T>` (like `Examples/hanoi/user_hanoi.cpp`) and emits the
transformed form (like `Examples/hanoi/transformed_user_hanoi.cpp`) — that is,
inserts the boilerplate that makes persistence actually work.

The tool extends `~/PersistentTyping/numa-clang-tool/`, mirroring the structure
of the existing `RecursiveNumaTyper` pass (paper §3.3–§3.8, Fig. 5).

---

## What the tool transforms

**Input** (user-written, naive C++):
- Regular `Node`/`Stack` classes (DRAM-typed fields and methods).
- A function-local declaration `persistent<Stack>* s = new persistent<Stack>();`.
- Transaction-wrapped writes the user composed themselves.

**Output** (mechanically transformed):
- The original classes, unchanged.
- A full template specialization `template<> class persistent<Node>` with
  every field's type recursively wrapped and `operator new`/`delete` routing
  through `pmem_alloc` / `pmem_free`.
- A full template specialization `template<> class persistent<Stack>` likewise.
- A `__pers_root` struct aggregating all top-level `persistent<T>*` decls.
- `__pers_root* __root = pmem_root<__pers_root>();` at the top of `main`.
- Each `new persistent<T>(args)` rewritten to
  `pmem_get_or_create<persistent<T>>(__root->slot, args)`.
- Functions originally taking `T*` parameters rewritten to take
  `persistent<T>*` when called with persistent values (signature propagation).

See `Examples/hanoi/user_hanoi.cpp` vs `Examples/hanoi/transformed_user_hanoi.cpp`
for a concrete worked example.

---

## Phased plan

The tool decomposes into independently-testable passes.

| Phase | Deliverable | Mirrors |
|------:|-------------|---------|
| 5.1 | **Discovery + Root insertion.** Find top-level `persistent<T>*` decls; emit `__pers_root` + `pmem_root` call + rewrite initializers to `pmem_get_or_create`. | new (no numa analogue — numa has no root) |
| 5.2 | **Type specialization.** For each user-defined class `T` referenced as `persistent<T>`, recursively generate a full `template<> class persistent<T>` with wrapped fields, rewritten methods, and `operator new`/`delete`. | `RecursiveNumaTyper` (paper §3.3–§3.8) |
| 5.3 | **Signature propagation.** For functions called with `persistent<T>` arguments, rewrite parameter types and propagate transitively. | `NumaTargetNumaPointer` |
| 5.4 | **Method-body rewrite (inside specializations).** Allocations `new U(args)` inside method bodies become `new persistent<U>(args)`; field reads/writes go through the wrapped types. | `RecursiveNumaTyper` body rewriting |
| 5.5 | **Output writer.** Emit transformed copies of all input source files (.hpp + .cpp). | numa tool's output pipeline |

5.1 is the easiest and has no numa counterpart, so it makes the cleanest entry
point. 5.2 is the heavy lift and is what the numa paper is mostly about. 5.3
and 5.4 are mechanical once 5.2's machinery is in place.

---

## Phase 5.1 — Discovery + Root insertion (first deliverable)

### What it does

1. **Discovery pass.** Visit the entire translation unit and collect every
   VarDecl that:
   - Has type `persistent<T>*` (pointer to persistent, i.e. heap-allocated form),
   - Has an initializer of the form `new persistent<T>(args...)`,
   - Is **not** inside a lambda body, **not** a class field, **not** a function
     parameter. Anywhere else in the TU is fair game — function-locals
     (typically in `main`) *and* file-scope globals both count.

   For each match, record `(T_name, var_name, init_args)` in a discovery table.

2. **Schema generation.** From the discovery table, generate the
   `__pers_root` struct:
   ```cpp
   struct __pers_root {
       pmem_ptr<persistent<T1>> v1;
       pmem_ptr<persistent<T2>> v2;
       ...
   };
   ```
   Field names match user variable names (collisions are an open question —
   see below).

3. **Global root insertion.** Immediately after the `__pers_root` struct
   definition, emit:
   ```cpp
   __pers_root* __root = pmem_root<__pers_root>();
   ```
   This is a **global variable**, not a local inside main. The ordering chain
   that makes it correct:

   ```
   program start
     → pmem_alloc_init() runs                          [constructor attribute]
     → struct __pers_root { ... };                     [type only, no runtime work]
     → __pers_root* __root = pmem_root<__pers_root>(); [first global init using pool]
     → user globals: x = pmem_get_or_create(...);      [reference __root, legal]
     → main() runs
   ```

   The pool exists by the time `__root`'s initializer runs because
   `pmem_alloc_init` carries `__attribute__((constructor))`, which the linker
   runs before C++ static-storage initialization.

4. **Initializer rewrite.** For each VarDecl in the discovery table, replace
   `new persistent<T>(args...)` with
   `pmem_get_or_create<persistent<T>>(__root->v_name, args...)`. The variadic
   args carry through unchanged — they are the first-run initial values.
   Function-local decls and global decls are rewritten identically; both
   reference the same global `__root`.

### What it does NOT do (yet)

- **Value-form decls** (`persistent<int> counter;` with no `new`): flagged as
  unsupported. Per HANDOFF.md's 2026-05-13 resolution, the library doesn't
  enforce placement; Phase 5 should emit a diagnostic rather than silently
  promote.
- **File-scope globals**: handled the same as function-locals. The discovery
  pass walks the entire TU and the rewrite references the same global
  `__root`. Initialization ordering is safe because `__root` is itself a global
  declared right after `__pers_root`, and the pool itself is set up by a
  `__attribute__((constructor))` function that the linker runs before any C++
  static-storage init.
- **Lambda-local decls**: ignored. A `persistent<T>*` declared inside a lambda
  body is a transient local — its lifetime ends when the lambda returns, so
  registering it in the root is nonsensical. (Decls in *enclosing* scopes that
  are captured into a lambda are fine — those are still found in step 1 because
  they sit in a function body, not in the lambda.)
- **Specializations** themselves (`template<> class persistent<T>`): that's
  Phase 5.2. 5.1 generates a program that *would* persist if specializations
  existed, but without 5.2 the generic class spec still applies and pmem
  allocation never happens. 5.1 + 5.2 together is what produces a persisting
  program.

### Naming conventions

- `__pers_root` — the struct type. Double-leading-underscore is reserved for
  the implementation, so we will not collide with user-defined names.
- `__root` — the local variable holding the root pointer in `main`.
- Field names inside `__pers_root` — match user variable names verbatim.

### Open questions for 5.1

1. **Naming collisions across functions.** If two functions both declare
   `persistent<Stack>* s = new persistent<Stack>();`, both want the field name
   `s` in `__pers_root`. Options:
   - Mangle by enclosing function: `main_s`, `helper_s`.
   - Use the VarDecl's source location as a unique key.
   - Reject (error out) and require unique names.
   
   First implementation should error out; mangling can come later.

2. **Multiple `new` calls bound to the same variable.** `persistent<Stack>* s =
   new persistent<Stack>(); ...; s = new persistent<Stack>();` — should both
   `new`s be rewritten to the same slot? Should the second be rejected? Easiest:
   reject (only one `new persistent<T>` per VarDecl supported).

3. **`delete s;` on a persistent pointer.** Undefined for now. Document and
   ignore; the user examples never do this.

### Test fixtures for 5.1

- `Examples/counter/user_counter.cpp` — single primitive-form decl. Smallest
  case.
- `Examples/stack/user_stack.cpp` — single class-form decl.
- `Examples/hanoi/user_hanoi.cpp` — three decls, multi-slot `__pers_root`.
- `DataStructureTests/src/main.cpp` — header-separated layout (Stack.hpp +
  main.cpp), exercises multi-file output.

The expected outputs are the corresponding `transformed_user_*.cpp` files.

---

## Phase 5.2 onward — sketched briefly

The remaining phases follow the numa paper structurally; details to be expanded
when 5.1 is working.

- **5.2 Specialization**: for each user-defined `T` encountered, recursively
  generate `template<> class persistent<T>`. Field wrapping rule: `U* field`
  becomes `pmem_ptr<persistent<U>> field`; fundamental/pointer types become
  `persistent<F> field`. Triggers recursive specialization for each `U`.
  Specializations must be emitted in topological order (dependencies first).

- **5.3 Signature propagation**: for each function called with
  `persistent<T>*` arguments, rewrite the corresponding parameter to
  `persistent<T>*`. Propagate transitively (rewritten functions may call other
  functions with the new types, requiring further rewrites).

- **5.4 Method body rewrite (within specializations)**: inside a specialization's
  method bodies, every `new U(args)` becomes `new persistent<U>(args)`; every
  field reference resolves through the wrapped type. The library's snapshot
  hooks (`store()`, `snapshot_if_pmem()`) do the rest at runtime.

- **5.5 Output writer**: emit transformed copies of every input file. For
  multi-file inputs (e.g. `DataStructureTests/`), preserve directory structure
  under an output root. Existing numa tool already has this scaffolding.

---

## Architectural note: where this tool lives

The Clang infrastructure already exists in `~/PersistentTyping/numa-clang-tool/`.
Phase 5 extends that codebase rather than starting fresh. Key existing files
(to mirror, not modify directly):

- `src/transformer/RecursiveNumaTyper.{h,cc}` — the numa specialization pass.
  The persistent specialization pass should be a sibling (`RecursivePersistentTyper`)
  with the same overall shape but emitting `persistent<T>` instead of
  `numa<T,N>`.
- `src/transformer/CastNumaAlloc.{h,cc}` — allocation-site rewrite. Persistent
  analogue rewrites `new persistent<T>(args)` to `pmem_get_or_create<...>`.
- `src/numafy/new_allocs.{h,cc}` — `new`-expression rewriting machinery.

The Root-insertion machinery (5.1) is **new** — numa has no analogue because
NUMA-typed data lives in arbitrary memory, not at a recoverable root.
