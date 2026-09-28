# Persistent Typing

A C++ template library + Clang source-to-source transformation tool that takes
ordinary user-written code and automatically rewrites it into a fully
crash-consistent, PMDK-backed persistent program. Inspired by the recursive
`numa<T, NodeID>` typing system; targets persistent memory (Intel Optane,
kernel-emulated pmem) via `libpmemobj`.

The high-level workflow:

```
   user-written .cpp                   automatically transformed .cpp
   ─────────────────                   ──────────────────────────────
   persistent<Stack>* s =       ──>    pmem_get_or_create<persistent<Stack>>(__root->s);
       new persistent<Stack>();
                                       template<> class persistent<Stack> {
   class Stack {                          pmem_ptr<persistent<Node>> top;
       Node* top;                         persistent<int> size;
       ...                                ...
   };                                     void push(int v) {
                                              pmem::obj::transaction::run(pmem_pool(), [&]{
                                                  persistent<Node>* n = new persistent<Node>(v);
                                                  n->next = top.get();
                                                  ...
                                              });
                                          }
                                       };
```

The result is a binary that:
- Allocates user objects in persistent memory.
- Wraps every write in a PMDK transaction so partial failures roll back.
- Persists state across process invocations (and across crashes).

## Repository layout

```
~/PersistentTyping/
  persistentLib/         The C++ template library:
                           persistenttype.hpp  persistent<T> template
                           pmem_allocator.hpp  pmem_ptr<T>, pmem_get_or_create,
                                               pmem_root, pmem_alloc/free.

  persist-clang-tool/    The Clang tool (LibTooling) that does the rewriting.
                           src/actions, src/consumer, src/transformer
                           src/transformer/recursePersistentTyper.{h,cc}
                           src/transformer/rootSetup.{h,cc}
                           src/transformer/visitors.{h,cc}
                           src/utils/utils.{h,cc}
                         Built via CMake.

  counter/  stack/       Source benchmarks (user-written form).
  hanoi/    DS/          DS is the multi-file data-structure test suite.

  concept/               Hand-transformed reference files for each benchmark.
                         These are how the persistified output is expected to
                         look; useful for comparing tool output against.

  Output/                Where the persistified versions of each benchmark
                         land after running ./persistify.py.

  persistify.py          Top-level driver: runs the Clang tool on a benchmark
                         and produces Output/<benchmark>/.
  run.py                 Top-level driver: compiles and runs a benchmark from
                         Output/<benchmark>/.

  pracitce/  tests/      Library-level practice programs and unit tests.

  Docs/                  Design notes, OS/C++ knowledge bases, environment
                         setup, Clang-tool design doc. Read these for context.

  HANDOFF.md             Living state-of-the-project document. Read this for
                         the latest design decisions, open questions, what
                         was done when, and what's next.
```

## Prerequisites

- Linux + Clang 20 (LLVM development headers).
- PMDK userspace libraries: `libpmem-dev`, `libpmemobj-dev`, `libpmemobj-cpp-dev`
  (`apt install` on Ubuntu).
- Persistent memory device. For development this can be the kernel's pmem
  emulation:
  - Reserve memory via GRUB: `memmap=4G!4G` (4 GB at offset 4 GB).
  - Format `/dev/pmem0` as ext4 with DAX, mount at `/mnt/pmem-emu`.
  - See [`Docs/EnvironmentSetup.md`](Docs/EnvironmentSetup.md) for the exact
    sequence used on this machine.
- The Clang tool expects the pool at `/mnt/pmem-emu/global_persistent_pool`
  (overridable via `PERSISTENT_POOL_PATH` env var; see `persistentLib/pmem_allocator.hpp`).

## Quick start

### 1. Build the Clang tool

```shell
cd persist-clang-tool
mkdir -p build && cd build
cmake ..
cmake --build .
cd ../..
```

### 2. Persistify a benchmark

This runs both passes (`root-setup` then `recurse-persistent`) and drops the
fully-rewritten source into `Output/<benchmark>/`.

```shell
./persistify.py hanoi
./persistify.py stack
./persistify.py DS
```

Available benchmarks: `counter`, `stack`, `hanoi`, `DS`.

### 3. Compile and run

`run.py` compiles the persistified output and runs the binary. Use `--fresh`
to wipe the pmem pool first (test first-run behavior); use `--runs N` to run
the binary N times in a row (test persistence by watching state grow).

```shell
./run.py hanoi --fresh --runs 2   # solve Towers of Hanoi, run twice
./run.py stack --fresh --runs 3   # push three rounds, watch stack grow
./run.py DS    --fresh --runs 2   # data-structure test suite
```

## Status

- `persistentLib/` — `persistent<T>` library, `pmem_ptr<T>` smart pointer,
  `pmem_get_or_create<T>(slot, args...)` find-or-create. Stable.
- `persist-clang-tool/` — Phase 5.1 (discovery + root insertion), 5.2
  (recursive specialization), 5.3 (body rewriting via visitors: new-expr →
  `new persistent<T>`, LHS rewrite, `.get()` insertion, transaction
  wrapping). Stable for the four current benchmarks except as below.
- **Counter is still broken** by an assignment-form declaration pattern
  (`persistent<int>* counter; counter = new persistent<int>(0);`) — the
  root-setup visitor expects an inline initializer. Phase 5.1 cleanup.
- `concept/` files are the hand-written reference for what each benchmark
  *should* look like after persistification.

Up-to-date design decisions and "next concrete step" live in
[`HANDOFF.md`](HANDOFF.md).

## Reading order for a fresh visitor

1. [`HANDOFF.md`](HANDOFF.md) — current state, open questions, design decisions.
2. [`Docs/EnvironmentSetup.md`](Docs/EnvironmentSetup.md) — pmem setup.
3. [`Docs/clang-tool.md`](Docs/clang-tool.md) — Phase 5 design doc.
4. `concept/<benchmark>/transformed_*.cpp` next to `<benchmark>/user_*.cpp`
   — see what the tool is supposed to produce.
