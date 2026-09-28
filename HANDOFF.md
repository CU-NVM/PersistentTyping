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

**Phase 5 is functionally complete on the four pedagogical benchmarks — counter, stack, hanoi, and DS all persistify, compile, and persist across runs.** `persistent<T>` snapshots into the active PMDK transaction's undo log on every write (Phase 2 = durability + atomicity in one mechanism), and as of 2026-08-11 unlogged writes to scalars are *inexpressible* rather than merely discouraged. `pmem_ptr<T>` hides OID translation; `pmem_string` holds variable-length strings in pmem; `pmem_get_or_create<T>(slot, args...)` hides find-or-create. **Phase 5.1** emits `__pers_root` and rewrites `new persistent<T>(args)`, now handling assignment-form initialisation and filtering out parameters and non-`main` locals. **Phase 5.2** emits a `template<> class persistent<T>` for every user-defined T reachable from `__pers_root`, multi-file safe. **Phase 5.3** (body rewriting) is folded into the recurse pass as visitors over the original AST: new-expr rewriting, LHS rewriting for new-init locals, and `.get()` insertion for pmem_ptr field reads. Method bodies are wrapped in transactions, using an IIFE to preserve non-void return values.

**The current gating item is a real benchmark, not tool features.** Run `./persistify.py <bench>` then `./run.py <bench> --fresh --runs 2`. See the YCSB gap analysis section for what a production workload still needs.

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

**Library hardening for real workloads: write barriers, allocation fix, `pmem_string` (2026-08-11)**
- Triggered by reading [`ycsb/`](ycsb/) to scope what the tool needs for a real benchmark. Full gap analysis is below; these three landed first because they are small, self-contained, and unblock everything else.
- **Unlogged writes closed (see [PersistentLib.md §7.8](Docs/PersistentLib.md)).** `count++` on a `persistent<int>` was silently bypassing the undo log: no member `operator++` existed, so overload resolution converted via `operator T&()` to a raw `int&` and used the built-in `++`, never calling `store()` and therefore never calling `pmemobj_tx_add_range_direct`. Fixed in two steps — added member `++`/`--`/`+=`/`-=`/`*=`/`/=`/`%=`/`&=`/`|=`/`^=`/`<<=`/`>>=` (all `store(load() op v)`), then **deleted the mutable `operator T&()`** leaving only the const one. Now `int& r = *p;` is a compile error. For scalar/pointer `T`, unlogged writes are **inexpressible**, not merely discouraged — a materially stronger crash-consistency claim for the paper. All four benchmarks unaffected.
- **`operator new` over-allocation fixed (§7.9, was the open §7.5).** `operator new`/`new[]` receive **bytes** but forwarded to `PersistentAllocator::allocate(n)`, which takes a **count** and multiplies by `sizeof(T)` again — 4× waste for `int`, 8× for `double`. Now they call `pmem_alloc(sz, alignof(T))` directly, matching what the Clang tool emits into generated specializations. Correct-by-coincidence for `sizeof(T)==1`, which is why it had gone unnoticed.
- **New type: [`persistentLib/pmem_string.hpp`](persistentLib/pmem_string.hpp).** Variable-length null-terminated string with characters in pmem; `PMEMoid` + `size_t` layout, `assign`/`operator=` from `const char*`, `c_str()`, `size()`, `empty()`, comparisons. Needed for YCSB's `HashNode::key`.
  - **Why not just `pmem_ptr<persistent<char>>`** (this was the design question worth resolving): that combination already gets the buffer into pmem via `persistent<char>::operator new[]` and keeps a restart-stable OID — two of the three problems. What it cannot do cheaply is *log* the writes: per-character `store()` means one `add_range` per byte, so a 9-char key costs 9 undo entries with tens of bytes of metadata each. `pmem_string` snapshots its two fields once and bulk-`memcpy`s. General principle recorded in the docs: per-element logging is correct when elements are meaningful units (pointers in a bucket array, counters) and pathological only for bulk byte copies — so **arrays of pointers need no special type**, `pmem_ptr` composition handles them.
  - Four bugs found by testing and fixed before landing: missing `operator new`/`delete` (standalone `new pmem_string` went to DRAM, `pmemobj_oid` returned `OID_NULL`, string silently read back empty every run — a *silent* failure); self-assignment use-after-free in `assign()` (released the old buffer before copying from it; survived testing only because the allocator handed back the same block); unchecked `pmem_alloc` return; and a destructor that cleared `data_`/`size_` with unlogged pmem writes, which on abort would restore an object pointing at nothing while its payload leaked.
  - Known limitations, all documented: no small-string optimisation (every non-empty string costs one allocation — an inline buffer for <~23 bytes would eliminate it for YCSB-length keys, but correctness first and the optimisation should be profile-driven), copy ctor/assign deleted, whole-string replacement only.
- **Automatic `char*` / `std::string` → `pmem_string` mapping (same session).** The user should never have to write `pmem_string` themselves — they declare an ordinary `char*` or `std::string` field and the tool infers it, exactly as it already infers `pmem_ptr<persistent<U>>` from `U*`. Three parts:
  - `utils::isStringLikeType` (matches `char*`, `const char*`, `std::basic_string<char,...>`) and `utils::isPersistenceAwareType` (passthrough for `pmem_string`/`pmem_ptr`/`persistent`, which also makes `wrapFieldType` idempotent at the field level). Both checked at the top of `wrapFieldType`, string-like *before* the pointer branch since `char*` is a pointer.
  - **Read sites need no rewriting**, via `pmem_string::operator const char*() const`. `strcmp`, ostream insertion, and any `const char*` parameter work unchanged. Safe where the deleted `operator T&()` was not — it is const, so nothing can be written through it. Verified non-ambiguous against `operator==`, `<<`, and the C string functions.
  - **`CStringIdiomRewriter`** (visitors.{h,cc}) rewrites the three write idioms. The implicit conversion is what makes this **mandatory rather than cosmetic**: `key = new char[n]` and `delete[] key` both *compile* against a `pmem_string` — the first copying from an uninitialised DRAM buffer and leaking it, the second handing the pmem payload to the DRAM `delete[]`. Only `strcpy` fails loudly. So two of the three would be silent corruption without the pass. The visitor keys off the field's *original* declared type (it runs against the unmodified AST), so it needs no coordination with `wrapFieldType`. `strncpy`/`strcat`/`sprintf`/`key[i]=c` are deliberately unhandled and fail loudly.
- **New benchmark [`kvnode/`](kvnode/)** — a `HashNode`-shaped class (`char* key`, `int count`, `Node* next`) plus a `Bucket` chain, exercising all three C-string idioms, `strcmp` reads, `count++` on a wrapped scalar, and a linked list of persistent nodes. Persistifies, compiles, and accumulates across runs (`alpha=2` then `alpha=4`). This is the regression test for the string path and the smallest thing resembling YCSB's data structure.

**Repo restructure + top-level workflow scripts (2026-06-24)**
- Benchmarks moved out of `Examples/` and `DataStructureTests/` to top-level `hanoi/`, `stack/`, `counter/`, `DS/` directories. Hand-transformed reference files (the `transformed_user_*.cpp` set) moved to `concept/<bench>/`. `Output/<bench>/` is the new home for fully-persistified output.
- **Top-level [`persistify.py`](persistify.py).** Takes a benchmark name, copies the source into `persist-clang-tool/input/<bench>/` and `persist-clang-tool/output/<bench>/`, runs root-setup, stages → `output_recurse/<bench>/`, runs recurse-persistent, then copies the final tree to `Output/<bench>/`. Replaces the older `persist-clang-tool/run.py` (deleted).
- **Top-level [`run.py`](run.py).** Compiles and runs a persistified benchmark from `Output/<bench>/`. `--fresh` wipes `/mnt/pmem-emu/global_persistent_pool` before running (test first-run behavior). `--runs N` runs the binary N times in succession (test persistence by watching the state grow). Handles single-file benchmarks (hanoi/stack/counter) via direct `clang++` invocation and the multi-file DS benchmark via its existing Makefile.
- **End-to-end verified** on hanoi, stack, DS — all three persistify and run via the new scripts cleanly. Counter still blocked by Phase 5.1 cleanup (assignment-form decls).

**Phase 5.3 — Clang tool: body rewriting via in-pass visitors (2026-05-30, in progress)**
- **Architectural pivot.** Initially scaffolded a separate `PersistentHeapTyper` pass that would parse the recurse-pass output and do body rewrites. Quickly discovered the flaw: recurse-pass output doesn't compile (spec bodies still reference raw types), so any downstream pass sees a degraded AST with stacked errors. The next pass's edits compound the breakage, and Clang's parser progressively loses ground. **Deleted that pass entirely** (`actions/persistentHeap*`, `consumer/persistentHeap*`, `transformer/persistentHeapTyper*`, plus `run.py` staging for it) and folded all body-level rewrites into `recursePersistentTyper`, operating on the original user source's clean AST via per-method **scratch Rewriters**. The main rewriter handles emission + signature propagation; scratches handle the body text we splice into specs.
- All visitors live in `transformer/visitors.{h,cc}` (header for visibility, .cc for member impls). Convention preserved: visitor scaffolding stays in the recursePersistentTyper layer; utility helpers in `utils/`.
- **Visitor 1 — `NewExprRewriter` (done).** Visits `CXXNewExpr`, filters to `getAsCXXRecordDecl()` to skip `new int` etc., rewrites the allocated-type token via `getAllocatedTypeSourceInfo()->getTypeLoc().getSourceRange()` → `persistent<T>`. Side effect: calls `typer->specializeType(name)`, which is the public reentrant entry point — a `new Foo()` in a method body for a Foo not reached via fields automatically queues Foo for spec emission this run.
- **Visitor 2 — `LocalLHSRewriter` (done).** Visits `VarDecl`. Skips `ParmVarDecl`. Filter: only rewrites when initializer (after `IgnoreParenImpCasts()`) is a `CXXNewExpr` of a user class. Pairs exactly with Visitor 1: same local that just got a `new persistent<T>()` RHS gets its LHS rewritten to `persistent<T>*` via `getTypeSourceInfo()->getTypeLoc().getAs<PointerTypeLoc>().getPointeeLoc().getSourceRange()`. Crucially does NOT touch `T* var = field;` or `T* var = some_func();` — those are different cases.
- **Visitor 3 — `PmemPtrInterfaceRewriter` (done).** Two methods, one visitor:
  - `VisitImplicitCastExpr` filters to `CK_LValueToRValue` whose sub-expr is a `MemberExpr` accessing a `FieldDecl` of type `T*` (user class) — that's exactly "pmem_ptr field is being read in raw-pointer context". Appends `.get()` after the field token via `Lexer::getLocForEndOfToken`. The LValueToRValue gate naturally skips LHS-of-assignment cases (no read happens there). Handles `Node* old = top;`, `if (!top)`, `top = old->next;`, `cur = cur->next;` all uniformly.
  - `VisitVarDecl` paired with the above: when a local's initializer is a `MemberExpr` reading a user-class-pointer field, rewrites the local's LHS to `persistent<T>*`. Pairs cleanly with the `.get()` insertion above.
- **Visitor 4 — `SignaturePropagator` (NOT YET IMPLEMENTED).** Designed but not written. Will visit `FunctionDecl`s in the TU (skipping methods, which are handled inside specs), and for each `ParmVarDecl` of type `T*` where T is in `seen` or `existingSpecs`, rewrite the pointee to `persistent<T>` via the **main rewriter** (not scratch, because this edits real source). Needs `RecursePersistentTyper::isPersistentType(name)` helper added. Wires in to `start()` after BFS drain, before spec inserts. This is the only thing standing between Hanoi compiling end-to-end.
- **Utility additions.** `utils::isUserClassPointerType(QualType)` — refactored out of an earlier per-visitor static method. Used by visitors 3, 4 to detect "wrap-able pointer field/local/param".
- **End-to-end status (Hanoi):** spec bodies compile cleanly after visitors 1–3 — `push`, `pop`, `print` all produce valid persistent code (`persistent<Node>* n = new persistent<Node>(v); n->next = top.get(); top = n; size++;` etc.). Single remaining compile error is the `moveDisks(Stack*, ...)` signature mismatch at the call site — exactly what Visitor 4 fixes.
- **FUNDAMENTAL FLAW (documented; needs future "Approach 3" work).** Local-pointer reassignment from a non-persistent source breaks our model:
  ```cpp
  Node* p = new Node();           // V1+V2 → persistent<Node>* p = new persistent<Node>();
  p = some_dram_func();           // FAILS: Node* not convertible to persistent<Node>*
  ```
  Because `persistent<T>*` and `T*` are unrelated types under Path A (no inheritance), no single LHS type can accept both initializers. **Our current stance (Approach 2): rewrite anyway, let it fail to compile, programmer fixes their code.** A robust solution (**Approach 3**) would generate `persistent<T>*` variants of free functions and pick the right one per call site — essentially whole-program monomorphization. Out of scope for the four current benchmarks, none of which hit this pattern. Inline comment marker is in `LocalLHSRewriter`'s implementation. Revisit when a benchmark needs it.
- **Phase 5.3 sub-passes that were planned and merged.** The original plan had separate passes `PmemPtrInterfaceTyper` (5.3b) and `SignaturePropagator` (5.4). Both are now visitors in the same recursePersistentTyper Clang invocation — no separate parses, no AST degradation between passes. The "phase" abstraction lives in code organization (different visitor classes), not in tool invocations.

**Phase 5.2 — Clang tool: recursive template specialization (2026-05-27)**
- New transformer at [`src/transformer/recursePersistentTyper.{h,cc}`](persist-clang-tool/src/transformer/) plus its frontend action + consumer. The pass reads the root-setup-rewritten source, discovers user-defined types reachable from `__pers_root`, and emits a full `template<> class persistent<T>` for each one. Recursion is BFS via a worklist seeded from root + grown by `utils::wrapFieldType` (which queues newly-seen user-class field types).
- Public re-entrant API: `specializeType(name)` + `getSpecs()`. Phase 5.3 will call back into this when it discovers a `new<U>` in a body for a U that hasn't been specialized.
- Idempotence is **AST-based** via `utils::findExistingPersistentSpecs(context, policy)` — walks the TU for `ClassTemplateSpecializationDecl` (excluding partial specs and forward decls) whose template name is `persistent`, returns the set of T-names to skip.
- Each spec contains, in this order: a `public:` section with operator new/delete (boilerplate, `alignof(T)` derived from the class name) and all public methods, then `protected:` and `private:` sections. Members are bucketed by original access so users with private fields stay encapsulated. Fields-before-methods within each bucket.
- Constructors are rewritten: name → `persistent`, **init lists converted to body assignments** prepended inside the original body (`Node(int v) : value(v), next(nullptr) {}` → `persistent(int v) { value = v; next = nullptr; ... }`). This works because `pmem_ptr::operator=(std::nullptr_t)` and `persistent<int>::operator=(const T&)` exist; the conversion runs inside `pmem_get_or_create`'s transaction so the per-field store hooks fire correctly. Filters: implicit ctors skipped, overloaded operators skipped, base/delegating inits skipped (no inheritance yet).
- Method bodies are kept **verbatim** via `Lexer::getSourceText` on `method->getBody()->getSourceRange()`. No semantic rewriting in this pass — that's Phase 5.3. Static / const / `= default` / `= delete` / no-body are all carried through correctly.
- Spec insertion via `Rewriter.InsertTextAfterToken` at the location after each class's trailing `;` (found via `Lexer::findNextToken` from `rd->getSourceRange().getEnd()`). `utils::writeAllRewrites` then iterates every modified FileID's buffer and flushes — so multi-file benchmarks (DS) get specs landed into the right header (`Stack.hpp`, `Node.hpp`), not the main TU.
- Auto-include: `utils::fileIncludes(SM, fid, "persistenttype.hpp")` scans the file's text for the include; if absent, prepend it. Required for DS where the headers don't pull in `persistenttype.hpp` themselves.
- `run.py` now stages a separate `output_recurse/<bench>/` tree before running the recurse pass — keeps root-setup output (`output/`) and recurse output (`output_recurse/`) cleanly separated. `--pass=all` does root-setup → stage → recurse. `--pass=recurse-persistent` standalone stages from `output/` first.
- Utility helpers added to `utils.{h,cc}`: `findExistingPersistentSpecs`, `findCXXRecordDeclByName` (TU walk; nested classes, namespaces, extern-"C"; first-match; deferred system-header filter), `findCXXRecordDeclsByName` (internal helper), `buildMethodSignature` (renames ctor/dtor to `persistent`/`~persistent`), `getMethodBodyText`, `getCtorInitAssignments`, `fileIncludes`.
- **Output is structurally correct but does NOT compile yet.** Method bodies inside each spec still reference raw types (`Node*`, `top`, `Node* old = top;`, `cur->next`, etc.) — Phase 5.3's job. Verified on Hanoi (spec text right, `moveDisks` signature still raw), Stack (same), DS (multi-file specs land in `Stack.hpp`/`Node.hpp` with auto-added include), Counter (no user-class to specialize → noop, which is correct).
- Known gaps left in `processMethod` (commented in code): default-args, variadic methods, templated members, ref-qualifiers, `explicit`/`constexpr`/`noexcept` specifiers, in-class field default initializers, copy/move ctor semantics for persistent types. None block the four current benchmarks.

**Phase 5.1 — Clang tool: discovery + root insertion (2026-05-26)**
- New tool at [`persist-clang-tool/`](persist-clang-tool/). Mirrors the structure of `numa-clang-tool/` (frontend action → consumer → transformer).
- [`src/actions/rootSetupFrontendAction.{h,cc}`](persist-clang-tool/src/actions/) — Clang `ASTFrontendAction`, creates one consumer per source file.
- [`src/consumer/rootSetupConsumer.{h,cc}`](persist-clang-tool/src/consumer/) — `ASTConsumer` that owns a `Rewriter` and dispatches to the transformer in `HandleTranslationUnit`.
- [`src/transformer/rootSetup.{h,cc}`](persist-clang-tool/src/transformer/) — the actual pass. `PersistentDeclVisitor` (anonymous-namespace `RecursiveASTVisitor`) walks every `VarDecl`, filters to types matching `persistent<T>*` (pointer-strip → `getAsCXXRecordDecl` → `ClassTemplateSpecializationDecl` → name == "persistent"), captures source range of the `new` expression, and extracts constructor args text via `CXXNewExpr::getDirectInitRange` + `Lexer::getSourceText`. `RootSetup::start()` then calls `applyRewrites()` (insert struct + global, replace each `new` with `pmem_get_or_create`) and `writeOutput()` (writes the rewritten buffer back to the same path).
- [`main.cc`](persist-clang-tool/src/main.cc) supports `--pass=root-setup` (and `--pass=cast` placeholder). Built executable: `build/bin/persist-tool`.
- [`run.py`](persist-clang-tool/run.py) — Python driver. Catalogs each benchmark (`src` + literal `cmd` string with `{pass}` substitution). On invocation: copies `<src>` to `input/<name>/` and `output/<name>/` (skips if exists, so iterative work isn't wiped), then `subprocess.run`s the command. The tool reads + writes the `output/<name>/` copy, keeping `input/<name>/` and the original source pristine.
- Resource-dir handled at CMake time: `execute_process(COMMAND clang -print-resource-dir)` captures it to `build/bin/builtInInclude.path`; the `cmd` strings reference it.
- Verified end-to-end on Hanoi (3 stacks → 3-slot `__pers_root`), Stack (1 slot, with `(0)` arg-capture), and DataStructureTests (1 slot, multi-file via header). Generated source compiles. Hanoi/DS persist correctly with hand-added specializations.
- Cleanup along the way: rewrote `Examples/{counter,stack,hanoi}/user_*.cpp`, `Examples/*/transformed_*.cpp`, and `DataStructureTests/src/main.cpp` to use `#include "persistenttype.hpp"` (plain) instead of `"../../persistentLib/persistenttype.hpp"` (relative). The relative form broke whenever a file was moved to a different depth — fragile and unidiomatic. `-I` resolves the plain form uniformly.

**Pedagogical aids written this session**
- [`Docs/clang-tool.md`](Docs/clang-tool.md) — Phase 5 design doc. Lays out passes 5.1–5.5, each with its numa-tool analogue. Phase 5.1 fully specified (discovery rules, schema generation, root insertion strategy, initializer rewrite). 5.2–5.5 sketched.
- [`Docs/CPP_knowledge.md`](Docs/CPP_knowledge.md) Chapter 18 — `const`-correctness and const overloading. Written after the `operator T&` / `operator const T&() const` dual-overload pattern came up while fixing `Examples/stack/transformed_user_stack.cpp:print()`.
- Practice [`pracitce/ex3_persist_hanoi.cpp`](pracitce/ex3_persist_hanoi.cpp) — option-B persistent Hanoi: bare methods + caller-wraps for per-move atomicity. Includes `--crash` mode that validates per-move rollback.
- [`Examples/hanoi/`](Examples/hanoi/) — `user_hanoi.cpp` + `transformed_user_hanoi.cpp` side-by-side. The first multi-slot example (3 stacks); validates `__pers_root` schema generation and signature propagation for `moveDisks`. Transformed files updated to put `__root` at file scope (global) instead of inside main, per the spec.

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

- **NEW 2026-05-30 (FUNDAMENTAL FLAW; FUTURE WORK): Local pointer reassignment from non-persistent source.** Under our current Phase 5.3 visitors (Approach 2), a local that is initialized with `new T()` and later reassigned from a non-persistent source fails to compile:
  ```cpp
  Node* p = new Node();          // V1+V2 → persistent<Node>* p = new persistent<Node>();
  p = some_dram_func();          // FAILS — Node* not convertible to persistent<Node>*
  ```
  Because `persistent<T>*` and `T*` are unrelated types under Path A (no inheritance), no single LHS type accepts both initializers. The post-rewrite code surfaces a compile error and the programmer must split the variable. The four current benchmarks (Counter, Stack, Hanoi, DS) don't hit this pattern, so Approach 2 is the active choice.

  **Approach 3 (deferred):** generate `persistent<T>*` variants of every free function `T* func(...)` and pick the right variant per call site. This is essentially whole-program monomorphization — requires source for every callable function, body cloning with `new T → new persistent<T>` substitutions, and call-graph-aware variant selection. Out of scope for now. The fundamental flaw needs to be documented here so it isn't forgotten when scaling beyond the current benchmarks. Inline comment marker is in `LocalLHSRewriter`'s implementation. Likely a separate phase (5.6?) when we get there.

- **RESOLVED 2026-05-14: Removing `operator new` from the generic class specialization in `persistenttype.hpp`.** User decision. Reasoning: the typer's primary output for any user-defined class T is a *full template specialization* that provides its own `operator new` routing to `pmem_alloc`. The generic class spec is only a fallback for un-specialized types, and a fallback that silently puts data in DRAM is a *less* bad failure than a fallback that silently puts data in pmem (without the typer's other transformations — recursive field wrapping, transaction-wrapped methods — pmem placement is half-correct, which is worse than not pmem at all). So the generic class spec keeps inheritance only; no `operator new`. Tests using `persistent<Point>` stack-locals are unaffected; any future heap-allocated `persistent<T>` without a specialization will get DRAM, which is honest.

## Phased plan

Each phase independently testable. Don't conflate them.

| Phase | Status | Deliverable | Mirrors |
|------:|--------|-------------|---------|
| 1 | ✓ done | `PersistentAllocator<T>` over a single pmemobj pool + minimal `persistent<T>` template (two specializations: fundamental/pointer vs class). Allocator routes `operator new` through the pool. | `~/NUMATyping/numaLib/umf_numa_allocator.hpp` + `numatype.hpp` |
| 2 | ✓ done (2026-05-13) | `store()` snapshots into active PMDK transaction's undo log via `pmemobj_tx_add_range_direct`. Combined with `transaction::run`, this gives durability AND atomicity in one mechanism — **Phase 3 collapsed into Phase 2**. | new |
| 3 | ~~deferred~~ — folded into Phase 2 above | ~~Failure atomicity bake-off~~ | ~~Atlas / Mnemosyne / Clobber-NVM~~ |
| 4 | ~~deferred~~ — PMDK auto-recovers on `pmemobj_open` by replaying any unfinished transaction's undo log. No custom recovery code needed. | ~~Recovery routine — replay logs / resume transactions on restart.~~ | n/a |
| 5.1 | ✓ done (2026-05-26) | **Discovery + Root insertion.** New Clang tool at `persist-clang-tool/`. `RootSetupFrontendAction` + `RootSetupConsumer` + `RootSetup` transformer that walks the TU, finds every `persistent<T>* x = new persistent<T>(args)`, emits a `__pers_root` struct + global `__root`, and rewrites each `new` into `pmem_get_or_create<persistent<T>>(__root->name, args)`. End-to-end verified on Hanoi, Stack, DS benchmarks via `./run.py`. | new — no numa analogue |
| 5.2 | ✓ done (2026-05-27) | **Recursive template specialization (text-only).** For each user-defined T reachable from `__pers_root`, emit `template<> class persistent<T>` with fields wrapped (`U* f` → `pmem_ptr<persistent<U>> f`, value class → `persistent<C>`, fundamental → `persistent<F>`), operator new/delete routed through pmem_alloc/free, ctor init lists converted to body assignments. **Method bodies kept verbatim** at this layer — body rewriting happens via visitors in the same Clang invocation. Multi-file safe; auto-includes `persistenttype.hpp` in headers that lack it. AST-based idempotence skips already-specialized T's. Output produced into `output_recurse/<bench>/` via run.py staging. | `numa-clang-tool/src/transformer/RecursiveNumaTyper.{h,cc}` |
| 5.3 | ~3/4 done (2026-05-30) | **Body rewriting + signature propagation, folded into recursePersistentTyper as visitors.** Visitor 1 (`new T(...)` → `new persistent<T>(...)`) ✓, Visitor 2 (LHS rewrite for `T* var = new T(...)`) ✓, Visitor 3 (`.get()` on pmem_ptr field reads + LHS rewrite for `T* var = pmem_field`) ✓. **Visitor 4 (free-function signature propagation for `void f(T*, ...)` → `void f(persistent<T>*, ...)` when T is specialized) not yet implemented.** Visitors 1–3 use per-method scratch Rewriter; Visitor 4 uses main rewriter. Known limitation: local reassignment from non-persistent source (see fundamental flaw / Approach 3 in Open questions). | `numa-clang-tool/src/transformer/RecursiveNumaTyper.{h,cc}` + `NumaTargetNumaPointer.{h,cc}` |
| 5.4 | merged into 5.3 | ~~Method-body rewrite (separate)~~ — folded into 5.3 via visitors in same Clang invocation. | — |
| 5.5 | not started | **Output writer** — landed early as part of 5.1's `writeOutput()`; will need to grow for multi-file inputs. | numa tool's output pipeline |
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
8. **`load()` / `store()` are abstraction seams for phase 2.** In phase 1 they're trivial pass-throughs. In phase 2, `store()` gained the `pmemobj_tx_add_range_direct` snapshot. **Updated 2026-08-11:** the mutable `operator T&` that used to bypass both is now deleted (see [PersistentLib.md §7.8](Docs/PersistentLib.md)) — only `operator const T&() const` remains, so every write goes through `store()` via `operator=`, a compound-assignment operator, or `++`/`--`. The read-barrier hole is still open, but it is now genuinely read-only.

## YCSB gap analysis (2026-08-11)

Read [`ycsb/`](ycsb/) — `HashTable` (bucket array of `HashNode` chains), `HashNode` (`char* key`, `int count`, `HashNode* next`), plus a multi-threaded NUMA harness — to scope what the tool needs for a real benchmark. The data structure itself is exactly the shape the tool already handles (Stack/Node generalised). The surrounding code is where the gaps are.

**Already fixed this session:** compound-assignment logging (`count++` appears on every insert and update path), `operator new` over-allocation, and `pmem_string` for `HashNode::key`.

**Still needed, roughly in dependency order:**

1. **`wrapFieldType` passthrough** for `pmem_string`/`pmem_ptr`/`persistent` — small, blocks using `pmem_string` in a persistified class at all.
2. **Array support.** `HashTable::table` is `HashNode**` allocated with `new HashNode*[bucket_count]`. Three sub-problems: `wrapFieldType` has no array case (currently yields `persistent<HashNode**>` — a raw pointer in pmem, stale on run 2); `NewExprRewriter` skips `new[]` entirely since the element type isn't a user class; and `AssignmentInitVisitor` only matches `DeclRefExpr = new ...`, not `table[i] = new HashTable(...)` (an `ArraySubscriptExpr` LHS). Per the `pmem_string` analysis, the *right* mapping for an array of pointers is nested `pmem_ptr` — `pmem_ptr<pmem_ptr<persistent<HashNode>>>` — not a new array type, since per-element logging is correct when each element is a real 16-byte OID update. The element count lives in a sibling field (`bucket_count`), which the tool cannot associate automatically; the user supplies it.
3. **Thread-safe `pmem_get_or_create`.** YCSB initialises tables from multiple threads behind barriers. The current check-then-create has no synchronisation, so two threads racing on one slot can double-allocate and leak. Either make the slot update a transactional compare-and-set, or document a single-threaded-init contract.
4. **Multi-TU support.** YCSB is `main.cpp` + `ycsb_benchmark.cpp`, with the persistent globals (`ht_node0`, `ht_node1`) in the latter and `main()` in the former. Root-setup inserts `__pers_root` "before main", which is the wrong TU. Needs the root split into a declaration in a generated shared header plus one definition, and the tool driven over a compilation database. Dodge for a first result: build a single-TU YCSB variant.
5. **Benchmark porting.** The numa path uses `reinterpret_cast<HashTable*>(new numa<HashTable, 0>(...))`, the layout-compatible cast trick from the numa paper. That is **illegal for us** — `pmem_ptr` is a 16-byte PMEMoid against an 8-byte raw pointer (see the layout-incompatibility note in Open questions). The persistent path has to be written in user-form (`persistent<HashTable>*`), not transformed from the numa path.

**Suggested first milestone:** a cut-down single-TU, single-thread, one-table YCSB with `pmem_string` keys, persistified end-to-end. Gets a paper-grade workload incrementally rather than all-or-nothing, and each gap above gets exercised by something real before being generalised.

**Verified as already working** (no action needed): out-of-line method definitions (`HashTable::insert` defined after the class body), method overloads (`insert(const char*)` vs `insert(const char*, int)`), transient STL in method bodies (`std::vector<char*> getAllKeys()` — DRAM vector returned by value, and the IIFE return-wrapping handles it), and globals assigned inside helper functions rather than `main` (the `AssignmentInitVisitor` filters on the *target variable*, not the assignment's location, so this falls out for free).

## Next concrete step

**Phase 5 is functionally complete on the pedagogical benchmarks.** All four — counter, stack, hanoi, DS — persistify, compile, and persist across runs via `./persistify.py <bench>` then `./run.py <bench> --fresh --runs 2`. Visitor 4 (signature propagation) was **designed and then deliberately dropped**: rewriting a free function's parameters destructively is the same flaw as the local-reassignment one (it locks the function into persistent-only and breaks any DRAM call site), so the contract is instead that **the programmer types free-function parameters explicitly** with `persistent<T>*`. `hanoi/user_hanoi.cpp`'s `moveDisks` was updated accordingly. A proper fix needs function-variant generation — see Approach 3 in Open questions.

The work has therefore shifted from "make the tool work" to "make the tool work on a real benchmark", which is the gating item for a paper.

1. ~~**`wrapFieldType` passthrough**~~ — done, along with automatic `char*`/`std::string` → `pmem_string` mapping and the `CStringIdiomRewriter`. See the library-hardening entry above, and [`kvnode/`](kvnode/) for the regression test.

2. **Array support in the tool** — now the gating item, and the last structural gap before a hash table works. `HashTable::table` is `HashNode**` allocated with `new HashNode*[bucket_count]`. Three sub-parts: `wrapFieldType` needs an array case (currently yields `persistent<HashNode**>`, a raw pointer in pmem that is stale on run 2 — the right mapping is nested `pmem_ptr`, per the reasoning in the `pmem_string` section of PersistentLib.md); `NewExprRewriter` needs to handle `new[]` for user-class element types; and `AssignmentInitVisitor` needs to accept an `ArraySubscriptExpr` LHS for `table[i] = new HashTable(...)`. Extend [`kvnode/`](kvnode/) with a bucket array to drive this — it is already the right shape.

4. **Scale up to real YCSB** — thread-safe `pmem_get_or_create`, multi-TU root emission, and the user-form port. See the YCSB gap analysis section for detail on each.

5. **Hardware access in parallel** (start now, it has lead time). CloudLab `r6525` or Chameleon both have Optane DC and are free for academic use. Emulated pmem is fine for correctness but a paper needs real-hardware numbers.

6. **Then evaluation and writing.** Throughput vs DRAM baseline, vs hand-written PMDK (the `concept/baseline_*.cpp` files exist for exactly this comparison), recovery time, crash-consistency validation. Target NVMW as a first venue; CGO/PLDI for a fuller version.

**Still-deferred tool gaps** (none blocking, each documented where it lives): Approach 3 function-variant generation; alias-only root filter (`persistent<Stack>* st2; st2 = st;` gets a slot it never uses); namespace-aware `findCXXRecordDeclByName`; system-header filtering; `processMethod` gaps (default args, variadic, templated members, ref-qualifiers, `explicit`/`constexpr`/`noexcept`, in-class field defaults, copy/move semantics); topological ordering of spec emission.

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
