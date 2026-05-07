# Handoff: `persistent<T>` library

This document captures the state of an in-progress design conversation. If you're a fresh Claude session, read this top to bottom and you'll have the context to continue. If you're me-from-the-future, same.

## Project goal

Build a `persistent<T>` template library, mirroring the structure of the existing `numa<T, NodeID>` library at `~/NUMATyping/numaLib/`, that places allocations on persistent memory (NVM) instead of (or in addition to) DRAM. End goal is to extend the existing recursive Clang tool at `~/PersistentTyping/numa-clang-tool/` so that declaring `persistent<Stack> src, aux, dst;` recursively propagates persistence through pointer-typed fields (e.g. `Node*` inside `Stack`) — analogous to how `RecursiveNumaTyper` currently propagates `numa<>` through pointer fields.

Inspiration / prior art (PDFs read in the original conversation):
- **Atlas** (Chakrabarti et al., OOPSLA '14) — lock-inferred failure-atomic sections, undo logging, recovery via replay. Most transparent; closest in spirit to the type-driven philosophy of `numa<T, NodeID>`.
- **Mnemosyne** (Volos et al., ASPLOS '11) — explicit `atomic{}` blocks, redo logging via STM. Clean semantics but every persistent store must be inside a transaction.
- **Clobber-NVM** (Xu et al., ASPLOS '21) — recovery-via-resumption, only logs writes that overwrite transaction inputs. Lowest logging overhead, heaviest compiler work; natural fit for the existing Clang infrastructure.

## Current status

Design phase. **No code has been written yet.** We had agreed to start with phase 1 (allocator + template, no failure atomicity) and were about to set up the development environment.

The user's most recent decisions, in order:
1. Use the existing PMDK install on the current machine (no real NVM available; emulate via tmpfs).
2. **Defer the failure-atomicity model decision** until phase 1 is complete.
3. Skip the PMDK smoke test for now.
4. Stop here so the conversation can resume on another machine.

## Next concrete step

Before writing any code, the user should pick one of:
- **Read PMDK first** — get a quick tour of `pmemobj_create`, `pmemobj_open`, `pmemobj_alloc`, `pmemobj_root`, transactions — so the allocator design isn't a black box. Recommended.
- **Set up tmpfs emulation** (Step 3A below) so there's a place to run code when we get there.
- **Jump straight to designing phase 1** on paper.

The user previously said "3 then 1 then 2" sounded right but we paused before doing any of them.

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
2. **One global pmemobj pool**, opened at process start by a `__attribute__((constructor))` init function (mirroring how `umf_alloc_init` works in numalib). Pool path comes from env var, e.g. `PERSISTENT_POOL_PATH`, defaulting to something under `/tmp/` or `/mnt/pmem-emu/`.
3. **No transactions in phase 1.** Allocations may leak on crash; that's acceptable for now.
4. **Root pointer:** start with PMDK's built-in `pmemobj_root()` (single root). Decide later whether to add Atlas-style named persistent regions (`find_or_create_pr`).
5. **PMDK pointer model — open question, see below.**

## Open questions

- **PMEMoid vs raw pointer in `persistent<T>`.** PMDK's `pmemobj_alloc` returns a `PMEMoid` (pool-id + offset), not a `T*`. To make `persistent<T>` look like `numa<T, NodeID>` we need raw pointers in the user-facing API. Two choices:
  - (a) Always convert via `pmemobj_direct(oid)` at use-time. Pointers valid only for current process unless pool maps at the same address.
  - (b) Reserve and reuse the same virtual address for the pool across runs (what Atlas does). More invasive but gives stable raw pointers and matches the long-term direction.
  - **Tentative choice: (b)**, but defer concrete implementation until phase 1 is being written.
- **Failure-atomicity model** (Atlas / Mnemosyne / Clobber-NVM). Deferred to start of phase 3.
- **Single-threaded code in Hanoi** — Atlas FASE inference relies on outermost critical sections; single-threaded code needs either explicit `txbegin/txend` or a thread-local lock-as-marker. If we go Atlas, we'll have to address this.

## Environment

Verified on the development machine (`kiwo9430@<host>`):

**Hardware / OS**
- Linux 6.11.5cxleak, x86_64
- 192 GB DRAM
- `/dev/dax0.0` and `/dev/dax1.0` exist but are in `system-ram` mode (NUMA target nodes 2 and 3, 64 GB each, hotplugged as regular RAM). **Not usable as persistent memory.**
- No real Optane DIMMs.

**PMDK install (verified)**
- `libpmem-dev`, `libpmemobj-dev` installed (apt).
- `pmempool` from a source build at `/usr/local/bin/pmempool` (the apt package by that name doesn't exist — it ships *inside* PMDK).
- Status of `libpmemobj-cpp-dev` (the C++ bindings): **not yet checked**. Worth confirming on the new machine. If missing:
  ```bash
  sudo apt install libpmemobj-cpp-dev
  ```

**No emulation set up yet.** When we need it (Step 3A from the original plan):
```bash
sudo mkdir -p /mnt/pmem-emu
sudo mount -t tmpfs -o size=4G tmpfs /mnt/pmem-emu
sudo chown $USER /mnt/pmem-emu
```
PMDK detects this isn't real pmem (`pmem_is_pmem()` returns false) and falls back to `msync`. Functionally identical from C code's perspective.

**For real NVM testing later:** CloudLab `r6525` / `c6525-100g` nodes, or Chameleon Cloud — both have Optane DC and are free for academic use.

## Reference files (paths on the dev machine, should be the same after pull)

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

In-flight prior work (worth checking, may already contain partial scaffolding):
- `~/PersistentTyping/Array_txn/` — earlier transaction-related experiment. Has its own `numatype.hpp` etc.
- `~/PersistentTyping/Array_lkfree/`, `~/PersistentTyping/Array/` — older array experiments.

## Useful one-liners

```bash
# Check PMDK install on a new machine
dpkg -l | grep -E "libpmem|libpmemobj"
ls /usr/include/libpmemobj.h /usr/include/libpmem.h
ls /usr/include/libpmemobj++/ 2>/dev/null | head
which pmempool && pmempool --version

# Check whether real NVM is present
ls /dev/pmem* 2>/dev/null
sudo ndctl list -N
sudo daxctl list

# tmpfs emulation (run once)
sudo mkdir -p /mnt/pmem-emu
sudo mount -t tmpfs -o size=4G tmpfs /mnt/pmem-emu
sudo chown $USER /mnt/pmem-emu
```

## Pointers worth re-reading from the source papers

- Atlas §3 (semantics), §4.4 (logging implementation), §5.1 (log elision).
- Mnemosyne §3 (design), §4.4 (RAWL + tornbit logging — clever, may want to steal).
- Clobber-NVM §3 (clobber logging insight), §4.4 (compiler analysis for clobber-write identification — relevant to phase 5).

## How to resume

On the other machine, after pulling this file:

1. Open Claude Code in `~/PersistentTyping/` (or `~/PersistentTyping/numa-clang-tool/` — pick one and stay consistent so the auto-memory dir matches).
2. Say something like: *"Read HANDOFF.md and let's continue from the next concrete step."*
3. Decide between the three "next concrete step" options at the top of this file.

If the user wants to scroll back through the literal prior conversation, the JSONL transcript on the previous machine is at:
```
~/.claude/projects/-home-kiwo9430-PersistentTyping-numa-clang-tool/
```
(or the parent project dir, depending which one the prior session was opened in). Not needed for continuity — this document captures the decisions.
