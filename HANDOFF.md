# Handoff: `persistent<T>` library

This document captures the state of an in-progress design conversation. If you're a fresh Claude session, read this top to bottom and you'll have the context to continue. If you're me-from-the-future, same.

## Project goal

Build a `persistent<T>` template library, mirroring the structure of the existing `numa<T, NodeID>` library at `~/NUMATyping/numaLib/`, that places allocations on persistent memory (NVM) instead of (or in addition to) DRAM. End goal is to extend the existing recursive Clang tool at `~/PersistentTyping/numa-clang-tool/` so that declaring `persistent<Stack> src, aux, dst;` recursively propagates persistence through pointer-typed fields (e.g. `Node*` inside `Stack`) — analogous to how `RecursiveNumaTyper` currently propagates `numa<>` through pointer fields.

Inspiration / prior art:
- **Atlas** (Chakrabarti et al., OOPSLA '14) — lock-inferred failure-atomic sections, undo logging, recovery via replay. Most transparent; closest in spirit to the type-driven philosophy of `numa<T, NodeID>`.
- **Mnemosyne** (Volos et al., ASPLOS '11) — explicit `atomic{}` blocks, redo logging via STM. Clean semantics but every persistent store must be inside a transaction.
- **Clobber-NVM** (Xu et al., ASPLOS '21) — recovery-via-resumption, only logs writes that overwrite transaction inputs. Lowest logging overhead, heaviest compiler work; natural fit for the existing Clang infrastructure.

All three PDFs are available in the project root for re-reading.

## Current status

**Phases 1, 2, library ergonomics (Tier 1 + Tier 2), and the by-hand transformation patterns are all done.** `persistent<T>` snapshots into the active PMDK transaction's undo log on every write (Phase 2 = durability + atomicity in one mechanism). `pmem_ptr<T>` hides OID translation behind `T*` semantics. `pmem_get_or_create<T>(slot, args...)` hides the find-or-create branch. Library expansion stops here — Tier 3 (named registry) was rejected because the Phase 5 typer will emit the same find-or-create boilerplate automatically. Validated end-to-end by three side-by-side `Examples/` (counter, stack, hanoi) plus a Hanoi practice variant (`pracitce/ex3_persist_hanoi.cpp`). **Phase 5 (Clang tool) is now in progress** — `persist-clang-tool/` scaffolding (FrontendAction + ASTConsumer) is up; next session writes the AST-walking transformer for Phase 5.1.

## What's been done so far

**Environment (this machine, `kidus@ecee-bilbo`)**
- Ubuntu 22.04, kernel 6.8, 32 GB DRAM, no real Optane.
- PMDK installed via apt: `libpmem-dev`, `libpmemobj-dev`, `libpmemobj-cpp-dev`.
- Kernel pmem emulation set up via `memmap=4G!4G` (chose over tmpfs because `pmem_is_pmem()` returns true and PMDK uses the real `clflush + sfence` path, not the `msync` fallback). Details in [Docs/EnvironmentSetup.md](Docs/EnvironmentSetup.md).
- Kernel auto-created `namespace0.0` in `fsdax` mode → `/dev/pmem0` (4 GB).
- Formatted ext4 with `-b 4096`, mounted with `-o dax` at `/mnt/pmem-emu`.
- Symlink `/mnt/ram → /mnt/pmem-emu` exists (added for Clobber-NVM compatibility; harmless otherwise).

**Practice with libpmemobj++ (in `pracitce/`)**
- [`ex1_persist.cpp`](pracitce/ex1_persist.cpp) — persistent counter that survives process exit. Demonstrated crash recovery with `fork() + abort()` inside `transaction::run`. Key learning baked in: **`persistent_ptr<int>` does not auto-snapshot the pointed-to int on writes; you must use `p<int>` for scalar fields if you want transactional undo to work.** This is the single most important PMDK C++ gotcha for this project.
- [`ex2_stack.cpp`](pracitce/ex2_stack.cpp) — persistent stack with `push`/`pop`/`crash_push`. Verified that a crash mid-`push` rolls back the new node allocation, the `top` pointer update, and the `size` increment as a unit on the next `pool::open`.

**Phase 1 library + tests**
- [`persistentLib/pmem_allocator.hpp`](persistentLib/pmem_allocator.hpp) — low-level allocator: one global PMDK pool, opened by `__attribute__((constructor))` `pmem_alloc_init`, closed by `pmem_alloc_fini`. Path from env var `PERSISTENT_POOL_PATH`, defaults to `/mnt/pmem-emu/global_persistent_pool`. `pmem_alloc(size, align)` → `pmemobj_alloc` + `pmemobj_direct`. `pmem_free(ptr)` → `pmemobj_oid` + `pmemobj_free`. Now also has `pmem_root<T>()` helper (returns typed pointer to PMDK's pool root) and `pmem_contains(void*)` predicate. Mirrors `umf_numa_allocator.hpp`.
- [`persistentLib/persistenttype.hpp`](persistentLib/persistenttype.hpp) — three parts:
  1. `PersistentAllocator<T>` — STL-compatible allocator (rebind, converting ctor, allocate/deallocate/construct, operator==/!=). Mirrors `NumaAllocator<T, NodeID>` minus `NodeID`. All `PersistentAllocator` instances compare equal (one global pool).
  2. `persistent<T, Alloc, E>` — forward declaration with SFINAE slot.
  3. Two specializations via `std::enable_if`:
     - **Primitive** (`is_fundamental || is_pointer`): contains a `T contents`, has `load()`/`store()`, conversion to `T&`, `operator->` for pointer types, `operator new`/`new[]`/`delete`/`delete[]` routing through `PersistentAllocator`, `operator=`. **Both constructors now route through `store()`** for consistency.
     - **Class**: inherits from `T`, default ctor + perfect-forwarding variadic ctor.
- [`tests/pmem_allocator_test.cpp`](tests/pmem_allocator_test.cpp) — raw allocator: alloc, write a pattern, read it back, free.
- [`tests/persistent_allocator_test.cpp`](tests/persistent_allocator_test.cpp) — STL contract: `std::vector` (allocate/deallocate/construct), `std::list` (rebind + converting ctor), allocator equality.
- [`tests/persistent_test.cpp`](tests/persistent_test.cpp) — `persistent<T>` end-to-end: primitive (`int`, `int*`), class (`Point`), and verification that `new persistent<int>` lands inside the pmem pool. **NOTE: must be run with the increment wrapped in `transaction::run` now that Phase 2 is in — tests need updating.**

All three tests build with `clang++ -std=c++17 -I.. <file>.cpp -o <bin> -lpmemobj` and pass.

**Phase 2 library + first real program (2026-05-13)**
- `persistent<T>::store()` now calls `pmemobj_tx_add_range_direct(&contents, sizeof(T))` before the write. The snapshot goes into the *active* transaction's undo log, giving both durability AND atomicity from the same mechanism. If no transaction is active, the PMDK call returns nonzero and `store()` throws `std::runtime_error("Failed to add range to transaction")`. The "must be inside a transaction" contract is enforced at runtime, loudly, at the first stray write.
- [`pracitce/ex1_persist_lib.cpp`](pracitce/ex1_persist_lib.cpp) — first program written against our library (vs raw PMDK). Counter that persists across runs. Uses our `pmem_root<Root>()`, hand-wraps the OID into `struct Root { PMEMoid counter_oid; }`, allocates `new persistent<int>(0)` on first run, increments inside `pmem::obj::transaction::run(pop, [&]{ ... })`. Verified: counter increments correctly across multiple invocations.

**Tier 1 + Tier 2 ergonomics cleanup (2026-05-14)**
- Tier 1: `pmem_allocator.hpp` now constructs a `pmem::obj::pool_base` alongside `global_pool` at init time and exposes it via `pmem_pool()`. User programs no longer hand-construct a `pool_base` at every transaction site. New include: `<libpmemobj++/pool.hpp>` and `<libpmemobj++/transaction.hpp>`.
- Tier 2: added `pmem_ptr<T>` to `pmem_allocator.hpp` — a 16-byte wrapper around `PMEMoid` that exposes `T*` semantics (`operator*`, `operator->`, `get()`, `explicit operator bool()`, comparison vs `nullptr`). Mutating operations (`operator=` from `T*`, `nullptr_t`, or another `pmem_ptr`) call a private `snapshot_if_pmem()` helper that snapshots into the active tx if the slot itself lives in pmem (so the same class works as a transient DRAM handle *or* as a persistent field). Outside a tx, writes to a pmem-resident `pmem_ptr` throw.
- Tier 2 step 5: added `pmem_get_or_create<T>(pmem_ptr<T>& slot, Args&&...)` — checks the slot, returns existing pointer if set, otherwise allocates a new `T` via `new T(args...)` and assigns into the slot, all inside an internal `transaction::run`. This hides the OID translation *and* the if/else find-or-create branch from user code.
- Decision (2026-05-14): library expansion stops here. Tier 3 (named registry) is NOT being built — the registry would be redundant because the recursive typer (Phase 5) will generate the same find-or-create boilerplate from a `new persistent<T>(...)` declaration, using the existing primitives.

**Crash recovery testing — Phase 2 + 2.5 fully validated end-to-end (2026-05-16)**
- Renamed `pracitce/ex1_persist_lib.cpp` → [`pracitce/ex1_counter.cpp`](pracitce/ex1_counter.cpp). Added a `--crash` mode that calls `std::abort()` inside the transaction after the increment. Test sequence (run-by-run): `before:0 → after:1`, `before:1 → after:2`, `before:2 → crash`, `before:2 → after:3`. The fourth `before:2` (not 3) proves the in-flight increment was rolled back during PMDK's auto-recovery on `pmemobj_open`.
- Added analogous `--crash` mode to [`pracitce/ex2_persist_stack.cpp`](pracitce/ex2_persist_stack.cpp). Stack rolls back across 9 snapshots (3 pushes × 3 fields each) in a single nested transaction. Pre-crash stack size unchanged on next run.
- Phase 2.5 (transactional alloc/free, see Open questions) landed the same day. After fix, the previously-leaked Node allocations from crashed pushes are reclaimed instead of orphaned. Correctness preserved.
- This is the *full* Phase 2 claim landing end-to-end on real data structures: durability (already established) + atomicity across multi-field updates (newly validated) + no resource leaks on abort (via Phase 2.5).

**Design validation: user-facing API ↔ typer-emitted form**
- [`Examples/counter/`](Examples/counter/) (2026-05-14) — the *primitive case* (`persistent<int>`):
  - `user_counter.cpp` — minimal user-exposed source. Compiles and runs but does NOT persist.
  - `transformed_user_counter.cpp` — adds the typer-generated `__pers_root` + `pmem_get_or_create`. Persists across runs.
- [`Examples/stack/`](Examples/stack/) (2026-05-14) — the *class case* (`persistent<Stack>` with `Node*` pointer chain):
  - `user_stack.cpp` — minimal user source with regular Node + Stack classes and `persistent<Stack>* s = new persistent<Stack>()`. Compiles but doesn't persist (falls back to generic class spec without operator new → DRAM allocation).
  - `transformed_user_stack.cpp` — adds (1) `template<> class persistent<Node>` full specialization with pmem operator new + wrapped fields, (2) `template<> class persistent<Stack>` full specialization with transaction-wrapped methods, (3) `__pers_root` + `pmem_get_or_create`. Persists; verified output: stack grows `[30, 20, 10] → [30, 20, 10, 30, 20, 10] → ...` across three runs.
- **What these validate together**: the user-facing source is reachable by the compiler in both the primitive and class cases. The Phase 5 typer's job is now mechanically clear: (a) recursively generate full template specializations for each user-defined T used as `persistent<T>`, with wrapped fields and tx-wrapped method bodies, (b) insert `__pers_root` aggregating top-level persistent declarations, (c) rewrite `new persistent<T>(args)` → `pmem_get_or_create<persistent<T>>(slot, args)`. The stack example is the small-scale dress rehearsal of what the typer will do on bigger programs.

**Documentation written ([Docs/](Docs/))**
- [`OS_knowledge.md`](Docs/OS_knowledge.md) — knowledge base on memory hierarchy, virtual memory, page cache, filesystems, mmap, DAX, pmem, GRUB. Built up as a reference for interviews + this project.
- [`CPP_knowledge.md`](Docs/CPP_knowledge.md) — 15-section reference of C++ features used in the library (class/function templates, variadic + perfect forwarding, SFINAE, type aliases, nested types, special members, lambdas + captures, placement new, operator overloading, `inline` variables, `noexcept`, GCC attributes, header guards).
- [`EnvironmentSetup.md`](Docs/EnvironmentSetup.md) — exactly what was done on this machine to enable pmem, in order, with rationale.
- [`clobber-nvm.md`](Docs/clobber-nvm.md) — walkthrough of the Clobber-NVM repo structure and what each component does.

**Clobber-NVM investigation**
- Repo cloned and extracted to `Clobber-NVM/`.
- Attempted to build with system clang 20 + apt PMDK 1.11 instead of LLVM 7 + PMDK 1.6. Outcome:
  - The C source code (runtime + app code) compiles cleanly on modern tools. PMDK 1.6 → 1.11 broke no APIs.
  - The LLVM passes **do not compile** against LLVM 14+ (uses `llvm/IR/TypeBuilder.h` which was removed in LLVM 9; uses the legacy pass manager).
  - Tried to bypass the passes by building the `benchmark-nolog` variant — it compiles and links, but **segfaults at runtime** in `listCreate` dereferencing a swizzled pointer. The pointer swizzling (`to_absolute_ptr`) is pass-inserted at every persistent-pointer access, not just at logging sites, so the runtime is structurally inseparable from the passes.
  - **Conclusion: Clobber-NVM the artifact requires LLVM 7. There's no "lite" path.**

## Open questions

- **PMEMoid vs raw pointer in `persistent<T>`.** PMDK's `pmemobj_alloc` returns a `PMEMoid` (pool-id + offset), not a `T*`. To make `persistent<T>` look like `numa<T, NodeID>` we need raw pointers in the user-facing API. Two choices:
  - (a) Always convert via `pmemobj_direct(oid)` at use-time. Pointers valid only for current process unless pool maps at the same address.
  - (b) Reserve and reuse the same virtual address for the pool across runs (what Atlas does). More invasive but gives stable raw pointers and matches the long-term direction.
  - **Tentative choice: (b)**, but defer concrete implementation until phase 1 is being written.
- **RESOLVED 2026-05-13 (with advisor): Failure-atomicity model.** Use **PMDK transactions** (`pmemobj_tx_begin` / `commit` / `abort`) for Phase 3. Already validated via `pracitce/ex1_persist.cpp` and `pracitce/ex2_stack.cpp` — PMDK gives undo logging + rollback on `abort` / process death. May revisit later if we want to link against a clobber-logging or Atlas-style runtime as a drop-in alternative; not blocking.
- **RESOLVED 2026-05-13 (with advisor): Clobber-NVM portability.** Not pursuing the LLVM 7 build. With PMDK transactions as the chosen atomicity mechanism, the Clobber-NVM artifact stops being on the critical path. The three resolution paths (A/B/C two-stage pipeline / port passes / reimplement) are no longer pending — they only mattered if Clobber-NVM was the chosen mechanism. If we want clobber-style logging *later*, it's a research project, not a Phase 3 blocker.
- **OBSOLETE: Single-threaded code in Hanoi** — only mattered if we'd picked Atlas for failure atomicity. With PMDK transactions chosen (Phase 2+3 collapse), this is moot — transactions are explicitly delimited by `transaction::run`.

- **NEW 2026-05-13: Compile-time enforcement of "writes must be inside a transaction".** The current runtime check (`store()` throws if `pmemobj_tx_stage() != TX_STAGE_WORK`) catches the bug loudly but only at runtime. A stronger design would make `store()` only callable when a "transaction token" type is in scope, so missing the wrapper fails to compile. Costs: API change — every write becomes `counter.store(5, tx)` instead of `*counter = 5`. The signature change ripples through every user-facing call site. Tradeoff: safer, but heavy. Defer; revisit if the runtime check ever lets a real bug through.

- **RESOLVED 2026-05-16: Phase 2.5 — transactional allocation and deallocation.** `pmem_alloc` and `pmem_free` now branch on `pmemobj_tx_stage()`: inside a transaction (`TX_STAGE_WORK`), they use `pmemobj_tx_alloc` / `pmemobj_tx_free`, otherwise the plain APIs. Closes two symmetric correctness holes: (1) allocations inside a transaction were leaked on abort (Node creation in `Stack::push` left orphans in pmem after a crash); (2) frees inside a transaction happened immediately, so abort would leave dangling pointers (use-after-free in `Stack::pop`). Both now participate in the rollback set — the entire object lifecycle is part of the same transaction. Verified by re-running the ex2_persist_stack recovery test: correctness preserved (rollback still produces consistent state), and the leak is closed. See [PersistentLib.md §6.11](Docs/PersistentLib.md) for full rationale.

- **RESOLVED 2026-05-14: Library ergonomics — cleanup tiers.** Tier 1 (library-managed `pool_base` + `pmem_pool()` getter) and Tier 2 (`pmem_ptr<T>` + `pmem_get_or_create<T>(slot, args...)`) both landed. Tier 3 (named registry) and Tier 4 (declarative globals) explicitly rejected: the Phase 5 typer will generate the same find-or-create boilerplate from a `new persistent<T>(...)` declaration using the existing primitives, making the registry redundant. Verified end-to-end by `Examples/counter/` — see "Design validation" section in What's been done.

- **NEW 2026-05-14: const-correctness of `persistent<T>` accessors.** `persistent<T>::operator T&` and `load()` are non-const-qualified, so calling them on a `const persistent<T>` (e.g. from inside a `const` method that has a `persistent<T>` field) fails to compile. Discovered while writing `Examples/stack/transformed_user_stack.cpp` — print() const can't access `size` directly. Workarounds exist (walk via pmem_ptr<>, or skip the access in const contexts). Fix is 2 lines: add `operator const T&() const` and `T load() const` to the primitive specialization. Deferred. See PersistentLib.md §7.7.
- **RESOLVED 2026-05-13 (with advisor): Where must `persistent<T>` actually live for the type to mean what it says?**

  Framed around four pointer/target cases:
  1. `persistent<Node*> p` — wrapper holds raw `Node*`. (pmem-ptr → DRAM-target if wrapper is in pmem.)
  2. `persistent<persistent<Node>*> p` — typer's natural output inside persistent structures (pmem-ptr → pmem-target).
  3. `persistent<Node>* p` — plain C++ pointer in DRAM, target in pmem. Transient handles, function params, return values.
  4. `Node* p` — plain C++.

  **Resolution:**
  - Case 1 (pmem ptr → DRAM target): not useful, will be blocked. (Phase 5 typer concern.)
  - Case 3 (DRAM ptr → pmem object): fine. Pointer-form locals don't need pmem placement — durability lives in `*p`, not `p`. Leak risk solved at root-reachability, not by banning.
  - Therefore the only enforcement question is for value-form locals (`persistent<int> counter;`). **Decision: the library does NOT enforce.** When we get to the recursive compiler (Phase 5), it will emit a warning/error when a `persistent<T>` declaration is not heap-allocated. Until then, users get the contract honest-by-convention.
  - The smart-wrapper alternative (Option B) is off the table — would have caused double allocation in class-spec fields and surprising RAII destruction of pmem slots.
  - **No library edits needed.** Phase 1 primitive spec stays as-written.
- **RESOLVED 2026-05-13 (with advisor): Inheritance for the class specialization.** Phase 5 typer emits full specializations with the same field/method layout (the numa pattern from BinarySearch.hpp), NOT inheritance from T. Prevents implicit upcast from silently calling T's non-persistent methods. The generic class spec in `persistenttype.hpp` is just a fallback; for any T actually used persistently, Phase 5 emits an explicit `template<> class persistent<T>` that overrides everything. Re-validated 2026-05-14 by re-reading the paper (§3.3–§3.8, Fig. 5) — the typer's *primary* job is generating these specializations recursively.

- **NEW 2026-05-14: Layout compatibility between `persistent<T>` and `T` is broken (unlike numa).** In the numa paper, `numa<Node*,0>` is one machine word (raw pointer, 8 bytes) and matches `Node*`'s layout byte-for-byte, which is what makes `reinterpret_cast<Node*>(new numa<Node,0>())` valid (paper §3.5, line 47 of Fig. 4). For us, `pmem_ptr<persistent<Node>>` is 16 bytes (a `PMEMoid` of `{pool_uuid_lo, offset}`) and does NOT match raw `Node*`'s 8 bytes. So `reinterpret_cast<Stack*>(new persistent<Stack>())` would compile but read the wrong bytes — silent corruption. Implications:
  - The typer-emitted `persistent<Stack>` cannot be reinterpret-cast'd to `Stack*`. Methods must operate on the persistent types directly throughout, never coerce to the regular type.
  - Code paths that pass a `Stack*` to legacy/external code (case where numa would have used `reinterpret_cast`) need a different mechanism for us — possibly an explicit deep-copy through a different code path, or just refusing to allow such crossover. Not blocking Phase 5 design but is a real semantic difference from the numa case.
  - Open: do we need any cross-type casting at all for our use case, or can we live entirely in the persistent-typed universe? For Hanoi the latter seems fine. For "mix this persistent<Stack> with a non-persistent library function that takes Stack*" we'd need a thought-out story.

- **RESOLVED 2026-05-14: Removing `operator new` from the generic class specialization in `persistenttype.hpp`.** User decision. Reasoning: the typer's primary output for any user-defined class T is a *full template specialization* that provides its own `operator new` routing to `pmem_alloc`. The generic class spec is only a fallback for un-specialized types, and a fallback that silently puts data in DRAM is a *less* bad failure than a fallback that silently puts data in pmem (without the typer's other transformations — recursive field wrapping, transaction-wrapped methods — pmem placement is half-correct, which is worse than not pmem at all). So the generic class spec keeps inheritance only; no `operator new`. Tests using `persistent<Point>` stack-locals are unaffected; any future heap-allocated `persistent<T>` without a specialization will get DRAM, which is honest.

## Phased plan

Each phase independently testable. Don't conflate them.

| Phase | Status | Deliverable | Mirrors |
|------:|--------|-------------|---------|
| 1 | ✓ done | `PersistentAllocator<T>` over a single pmemobj pool + minimal `persistent<T>` template (two specializations: fundamental/pointer vs class). Allocator routes `operator new` through the pool. | `~/NUMATyping/numaLib/umf_numa_allocator.hpp` + `numatype.hpp` |
| 2 | ✓ done (2026-05-13) | `store()` snapshots into active PMDK transaction's undo log via `pmemobj_tx_add_range_direct`. Combined with `transaction::run`, this gives durability AND atomicity in one mechanism — **Phase 3 collapsed into Phase 2**. | new |
| 3 | ~~deferred~~ — folded into Phase 2 above | ~~Failure atomicity bake-off~~ | ~~Atlas / Mnemosyne / Clobber-NVM~~ |
| 4 | ~~deferred~~ — PMDK auto-recovers on `pmemobj_open` by replaying any unfinished transaction's undo log. No custom recovery code needed. | ~~Recovery routine — replay logs / resume transactions on restart.~~ | n/a |
| 5 | not started | Extend Clang tool: recursive `persistent<>` specialization, parallel to `RecursiveNumaTyper` (see paper §3.3–§3.8 and Fig. 5). **Primary work**: for each user-defined class T used as `persistent<T>`, generate a *full template specialization* `template<> class persistent<T>` with (a) every field's type recursively wrapped (`Node* root` → `pmem_ptr<persistent<Node>> root`), (b) every method rewritten so internal allocations target `persistent<X>` types and bodies are wrapped in `transaction::run`, (c) `operator new`/`delete` overloaded to route through `pmem_alloc`/`pmem_free`. **Recursion**: if a wrapped field references another user-defined type U, the typer triggers a specialization for `persistent<U>` too (this is how `new numa<Stack,0>()` cascades into `numa<Node,0>` in Fig. 5). **Secondary touches** (specific to persistence, not in numa): generate a per-program `__pers_root` struct aggregating all top-level `persistent<T>*` declarations, fetch via `pmem_root<>()`, rewrite `new persistent<T>(args)` to `pmem_get_or_create<persistent<T>>(slot, args)`. | `numa-clang-tool/src/transformer/RecursiveNumaTyper.{h,cc}` |
| Tiers 1+2 | ✓ done (2026-05-14) | Library ergonomics: `pmem_ptr<T>` hides OID translation; `pmem_get_or_create<T>(slot, args...)` hides find-or-create; library-managed `pool_base` via `pmem_pool()`. Tier 3 (named registry) rejected — Phase 5 typer will generate the same boilerplate. | new |

For Towers of Hanoi (`numa-clang-tool/towers_of_hanoi.cpp`), phases 1–2 alone are enough to make `persistent<Stack> src, aux, dst;` work — phase 2's transactional store makes pushes / pops atomic and durable. Still need Phase 5 (or hand-rewriting Stack/Node) to make it actually run.

## Key design decisions made so far

1. **Mirror the numalib directory structure.** Target layout:
   ```
   ~/PersistentTyping/persistentLib/
     persistenttype.hpp     # mirror of numatype.hpp
     pmem_allocator.hpp     # mirror of umf_numa_allocator.hpp
   ```
2. **One global pmemobj pool**, opened at process start by a `__attribute__((constructor))` init function (mirroring how `umf_alloc_init` works in numalib). Pool path comes from env var, e.g. `PERSISTENT_POOL_PATH`, defaulting to `/mnt/pmem-emu/persistent.pool`.
3. **No transactions in phase 1.** Allocations may leak on crash; that's acceptable for now.
4. **Root pointer:** start with PMDK's built-in `pmemobj_root()` (single root). Decide later whether to add Atlas-style named persistent regions (`find_or_create_pr`).
5. **Use `p<T>` for scalar fields in persistent structs, `persistent_ptr<T>` for links.** Confirmed by ex2_stack. (Applies when writing PMDK transactional code directly. The `persistent<T>` wrapper in `persistentLib/` is a separate abstraction — see decision 7.)
6. **PMDK pointer model — open question, see above.**
7. **`persistent<T>` wrapper uses raw `T contents` (primitive) or inheritance (class), not PMDK's `p<T>`/`persistent_ptr<T>`.** Done this way to mirror numa exactly. Means durability lives in `store()` (phase 2) rather than being inherited from the wrapper type. Trade-off: simpler template surface, but more responsibility on the library to add durability hooks at the right call sites.
8. **`load()` / `store()` are abstraction seams for phase 2.** In phase 1 they're trivial pass-throughs. In phase 2, `store()` will gain `pmem_persist(&contents, sizeof(T))`. `operator T&` deliberately bypasses both (returns reference, not copy) — leaves a hole in the read-barrier story that we'll have to address in phase 2 if we want a read hook.

## Next concrete step

Recovery testing and Phase 2.5 are both done as of 2026-05-16. Phase 2 is fully validated end-to-end (durability + atomicity + no-leak-on-abort) on both a primitive counter and a multi-field stack. Next:

1. **Const-correctness fix in persistent<T>** (open question 7.7). 2-line library change: add `operator const T&() const` and `T load() const` to the primitive specialization. Eliminates the workaround in the stack examples (skipping size print in const methods).

2. **Real-program target: Towers of Hanoi.** Modify `numa-clang-tool/towers_of_hanoi.cpp` to use `persistent<Stack> src, aux, dst;`. By-hand application of what Phase 5 will eventually automate. Forcing function for API gaps.

3. **Read NV-Heaps paper** (Coburn et al.) — informs the case-3 (DRAM ptr → pmem object) safety model before Phase 5 typer rewrites are designed. Standalone reading; lower priority but slottable anywhere.

4. **Phase 5: start work on the recursive typer.** With Phase 2 fully validated and the by-hand transformation patterns demonstrated in `Examples/counter/` and `Examples/stack/`, the typer's job is now mechanically clear: (a) generate full template specializations recursively for each user-defined T used as `persistent<T>`, (b) insert `__pers_root` aggregating top-level persistent declarations, (c) rewrite `new persistent<T>(args)` → `pmem_get_or_create<persistent<T>>(slot, args)`. Mirror `RecursiveNumaTyper` in `~/PersistentTyping/numa-clang-tool/src/transformer/`.

Recommended order: 1 (small, frees up the examples) → 2 (forcing function) → 3 (reading) → 4 (the big lift).

## Environment

**Hardware / OS (this machine)**
- Ubuntu 22.04.5 LTS, kernel 6.8.0-111-generic
- 32 GB DRAM (4 GB reserved for pmem emulation via `memmap=4G!4G` in GRUB)
- No real Optane.
- GCC 11.4, Clang 20.0 (system), Clang 14 (apt opt/llc)

**PMDK install (verified)**
- `libpmem-dev` 1.11.1, `libpmemobj-dev` 1.11.1, `libpmemobj-cpp-dev` 1.13.0 (all apt).
- `pmempool` not installed (only ships with source build; not needed for phase 1).

**Pmem device**
- `/dev/pmem0`, `fsdax` namespace, 4 GB
- ext4 with DAX mounted at `/mnt/pmem-emu`
- Mount does **not** survive reboot — re-mount with `sudo mount -o dax /dev/pmem0 /mnt/pmem-emu` or add to `/etc/fstab`.
- Symlink `/mnt/ram → /mnt/pmem-emu` for Clobber-NVM compatibility.

**For real NVM testing later:** CloudLab `r6525` / `c6525-100g`, or Chameleon Cloud. Both have Optane DC and are free for academic use.

## Reference files

Existing numa library (the structural template we're mirroring):
- `~/NUMATyping/numaLib/numatype.hpp` — the `numa<T, NodeID>` template, two specializations.
- `~/NUMATyping/numaLib/umf_numa_allocator.hpp` — UMF/jemalloc-backed NUMA allocator with constructor-time init.
- `~/NUMATyping/numaLib/numathreads.hpp` — thread-pinning helpers (probably not needed for persistent<T> but worth a glance).

Existing Clang tool (what phase 5 will extend):
- `~/PersistentTyping/numa-clang-tool/src/transformer/RecursiveNumaTyper.{h,cc}` — recursive specialization pass.
- `~/PersistentTyping/numa-clang-tool/src/transformer/CastNumaAlloc.{h,cc}` — allocation-site rewrite.
- `~/PersistentTyping/numa-clang-tool/src/transformer/NumaTargetNumaPointer.{h,cc}` — pointer-target propagation.
- `~/PersistentTyping/numa-clang-tool/src/numafy/new_allocs.{h,cc}` — `new` rewrites.

Test program for phase 1–2:
- `~/PersistentTyping/numa-clang-tool/towers_of_hanoi.cpp` — three-Stack recursive Hanoi solver. Will become `persistent<Stack> src, aux, dst;` once phase 1 is done.

Practice code (working PMDK examples):
- `~/PersistentTyping/pracitce/ex1_persist.cpp` — counter + crash recovery
- `~/PersistentTyping/pracitce/ex2_stack.cpp` — persistent stack + crash mid-push

Clobber-NVM artifact:
- `~/PersistentTyping/Clobber-NVM/` — extracted, build chain investigated, LLVM 7 not yet built. See [Docs/clobber-nvm.md](Docs/clobber-nvm.md).

In-flight prior work (worth checking, may already contain partial scaffolding):
- `~/PersistentTyping/Array_txn/` — earlier transaction-related experiment. Has its own `numatype.hpp` etc.
- `~/PersistentTyping/Array_lkfree/`, `~/PersistentTyping/Array/` — older array experiments.

## Useful one-liners

```bash
# Verify pmem env on this machine
mount | grep pmem                    # should show /dev/pmem0 on /mnt/pmem-emu type ext4 (... dax)
sudo ndctl list -RN                  # confirm region0 + namespace0.0 fsdax mode
ls /mnt/pmem-emu/                    # poke around current pool files

# Re-mount after reboot
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu

# Build & run a libpmemobj++ program
clang++ -std=c++17 -o foo foo.cpp -lpmemobj    # NOT -lpmemobj++ (header-only)

# Check whether real NVM is present (not on this machine)
ls /dev/pmem* 2>/dev/null
sudo daxctl list
```

## Pointers worth re-reading from the source papers

- Atlas §3 (semantics), §4.4 (logging implementation), §5.1 (log elision).
- Mnemosyne §3 (design), §4.4 (RAWL + tornbit logging — clever, may want to steal).
- Clobber-NVM §3 (clobber logging insight), §4.4 (compiler analysis for clobber-write identification — relevant to phase 5).

## How to resume

After pulling this file on another machine:

1. Open Claude Code in `~/PersistentTyping/`.
2. Say something like: *"Read HANDOFF.md and pick up at the next concrete step."*
3. Confirm pmem mount is live (`mount | grep pmem`). If not, re-mount per Useful one-liners.
4. Rebuild and run the three tests in `tests/` as a sanity check that nothing rotted:
   ```bash
   cd tests
   for t in pmem_allocator_test persistent_allocator_test persistent_test; do
     rm -f /mnt/pmem-emu/global_persistent_pool
     clang++ -std=c++17 -I.. ${t}.cpp -o $t -lpmemobj && ./$t
   done
   ```
5. Pick from the Next concrete step list above (discuss open questions / phase 2 / hanoi target).

If the user wants to scroll back through prior conversation transcripts, the auto-memory dir on this machine is at:
```
~/.claude/projects/-home-kidus-PersistentTyping/memory/
```
