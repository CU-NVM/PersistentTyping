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

**Phase 1 is done.** `persistent<T>` library is written, tested end-to-end. Two new architectural questions surfaced during phase 1 that are deferred for later discussion (see Open questions).

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
- [`persistentLib/pmem_allocator.hpp`](persistentLib/pmem_allocator.hpp) — low-level allocator: one global PMDK pool, opened by `__attribute__((constructor))` `pmem_alloc_init`, closed by `pmem_alloc_fini`. Path from env var `PERSISTENT_POOL_PATH`, defaults to `/mnt/pmem-emu/global_persistent_pool`. `pmem_alloc(size, align)` → `pmemobj_alloc` + `pmemobj_direct`. `pmem_free(ptr)` → `pmemobj_oid` + `pmemobj_free`. Mirrors `umf_numa_allocator.hpp`.
- [`persistentLib/persistenttype.hpp`](persistentLib/persistenttype.hpp) — three parts:
  1. `PersistentAllocator<T>` — STL-compatible allocator (rebind, converting ctor, allocate/deallocate/construct, operator==/!=). Mirrors `NumaAllocator<T, NodeID>` minus `NodeID`. All `PersistentAllocator` instances compare equal (one global pool).
  2. `persistent<T, Alloc, E>` — forward declaration with SFINAE slot.
  3. Two specializations via `std::enable_if`:
     - **Primitive** (`is_fundamental || is_pointer`): contains a `T contents`, has `load()`/`store()`, conversion to `T&`, `operator->` for pointer types, `operator new`/`new[]`/`delete`/`delete[]` routing through `PersistentAllocator`, `operator=`.
     - **Class**: inherits from `T`, default ctor + perfect-forwarding variadic ctor.
- [`tests/pmem_allocator_test.cpp`](tests/pmem_allocator_test.cpp) — raw allocator: alloc, write a pattern, read it back, free.
- [`tests/persistent_allocator_test.cpp`](tests/persistent_allocator_test.cpp) — STL contract: `std::vector` (allocate/deallocate/construct), `std::list` (rebind + converting ctor), allocator equality.
- [`tests/persistent_test.cpp`](tests/persistent_test.cpp) — `persistent<T>` end-to-end: primitive (`int`, `int*`), class (`Point`), and verification that `new persistent<int>` lands inside the pmem pool.

All three tests build with `clang++ -std=c++17 -I.. <file>.cpp -o <bin> -lpmemobj` and pass.

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
- **Failure-atomicity model** (Atlas / Mnemosyne / Clobber-NVM). Deferred to start of phase 3, per the original plan.
- **NEW: Clobber-NVM integration if it wins the phase-3 bake-off.** The artifact is locked to LLVM 7; the persistent-clang-tool is LLVM 20 / C++20. Three resolution paths if/when we pick Clobber-NVM:
  - **(A) Two-stage pipeline.** Your tool emits LLVM-7-compatible source; Clobber-NVM's clang 7 + passes compile it. Cost: restricts persistent code to C++14/17 subset (no concepts, no `consteval`, etc.) and requires maintaining LLVM 7 forever.
  - **(B) Port their passes to LLVM 20.** ~3 pass files, mostly mechanical (legacy → new pass manager, `TypeBuilder.h` → direct `FunctionType::get`). Estimated 1–2 weeks. End state: one ecosystem.
  - **(C) Reimplement clobber logging natively in `persistent-clang-tool/`.** Use the paper §4.4 as the spec. Estimated 3–4 weeks. End state: clobber logging is a first-class part of your tool, not an external dependency.
  - **Tentative lean: (B) if Clobber-NVM wins, (C) if there's appetite for a more academic contribution.** Defer until phase 3 decision is made.
- **Single-threaded code in Hanoi** — Atlas FASE inference relies on outermost critical sections; single-threaded code needs either explicit `txbegin/txend` or a thread-local lock-as-marker. If we go Atlas, we'll have to address this.
- **NEW: Is `operator new` the only path that puts `contents` into pmem?** As written, `persistent<T>` is just a wrapper struct: `T contents` lives wherever the enclosing `persistent<T>` object lives. Stack-local `persistent<int> a;` puts `contents` on the stack — *no pmem involved despite the type name*. Only `new persistent<T>(...)` (which invokes `operator new` → `pmem_alloc`) actually lands data in pmem. This works for the TOH plan (stack-local `persistent<Stack> src;` is a handle whose pointer fields, after the recursive-typer pass, will be `persistent<Node>*` allocated via `operator new` → pmem). But it leaves a footgun: someone writing application code who doesn't know this will assume the type name guarantees persistence. **Open question for next session: is heap-only the right pmem path, or should we add another mechanism (e.g., a `persistent_region` placement allocator, or a `persistent<T>::make_persistent()` factory) that's harder to misuse?**
- **NEW: Inheritance for the class specialization.** Current design has `persistent<Stack> : public Stack`. Advisor previously raised concerns about this for the numa library and preferred a virtual-function-dispatched alternative there. We haven't applied that concern to `persistent<T>` yet. Two questions to resolve: (1) does the advisor's reasoning carry over from numa to persistent, given the different semantics, and (2) if yes, what does the non-inheritance design look like here? Defer until next session — phase 1 is done either way and phase 2 doesn't depend on the class specialization shape.

## Phased plan

Each phase independently testable. Don't conflate them.

| Phase | Deliverable | Mirrors |
|------:|-------------|---------|
| 1 | `PersistentAllocator<T>` over a single pmemobj pool + minimal `persistent<T>` template (two specializations: fundamental/pointer vs class). Allocator routes `operator new` through the pool. **No durability guarantees beyond what PMDK gives for free.** | `~/NUMATyping/numaLib/umf_numa_allocator.hpp` + `numatype.hpp` |
| 2 | Persistence primitives on stores: `store + flush + fence` (à la Mnemosyne §4.1). Make individual writes durable. | new |
| 3 | Failure atomicity. **Model deferred.** Re-read the three papers and pick at this point. | Atlas / Mnemosyne / Clobber-NVM |
| 4 | Recovery routine — replay logs / resume transactions on restart. | new |
| 5 | Extend Clang tool: recursive `persistent<>` specialization, parallel to existing `RecursiveNumaTyper`. | `numa-clang-tool/src/transformer/RecursiveNumaTyper.{h,cc}` |

For Towers of Hanoi (`numa-clang-tool/towers_of_hanoi.cpp`), phases 1–2 alone are enough to make `persistent<Stack> src, aux, dst;` allocate from pmem.

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

Three things pending; pick whichever order makes sense:

1. **Discuss the two new open questions** (pmem-via-heap-only? + inheritance for class specialization). Both are architectural and worth nailing down before phase 2 touches the same code.
2. **Phase 2: persistence primitives.** Add `pmem_persist(&contents, sizeof(T))` inside `store()` so every write becomes durable via CLWB + SFENCE. `load()` is symmetrically positioned if reads ever need a barrier. Also consider whether `operator T&` (which bypasses `load()`) needs to be revisited under Phase 2 semantics — see the discussion in CPP_knowledge.md / chat transcript.
3. **Real-program target: Towers of Hanoi.** Modify `numa-clang-tool/towers_of_hanoi.cpp` to use `persistent<Stack> src, aux, dst;` and verify it builds with phase 1's library. This is a forcing function — will surface anything we missed in the API (e.g., whether `persistent<Stack>::push` correctly routes new-Node allocations into pmem after the recursive typer rewrites the Node* fields).

Recommended order: 1 → 2 → 3.

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
