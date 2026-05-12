# Clobber-NVM

A reference for what's in [`Clobber-NVM/`](../Clobber-NVM/) and how the system fits together. Source: Xu, Izraelevitz, and Swanson, *"Clobber-NVM: Log Less, Re-execute More"*, ASPLOS '21.

---

## What problem does it solve

NVM gives you durable load/store memory, but the CPU cache is volatile and may evict cache lines in any order. A crash mid-transaction can leave persistent state inconsistent. Failure-atomicity libraries (PMDK, Atlas, Mnemosyne) ensure all-or-nothing visibility for code regions, but at high cost — most use undo logging, which writes the old value of every store to a persistent log before the actual store, plus a fence after each log entry.

Clobber-NVM's claim: **most of those log writes are unnecessary if you recover by re-executing the transaction instead of rolling it back.** The compiler identifies which writes actually need to be logged (only the ones that overwrite transaction inputs — "clobber writes"), and the runtime logs only those. Recovery restores the clobbered inputs, then re-runs the transaction from the beginning.

Reported result: 1.1×–42.6× less log data, 2.4×–4.7× fewer ordering fences, and up to 2.6× faster than PMDK on YCSB workloads.

---

## The core idea: clobber logging

A **transaction input** is a value read inside the transaction that was written before it. A **transaction output** is a value the transaction writes that's read after it. A write is a **clobber write** if it overwrites a transaction input.

Three logging strategies, in order of overhead:

| Strategy | What it logs | Recovery |
|---|---|---|
| Undo logging (PMDK) | Old value before every store | Roll back |
| Undo-then-reexecute (naive) | Old value before every store | Roll back, then re-run |
| **Clobber logging** | Old value before clobber writes only | Restore clobbered inputs, then re-run |

The insight: non-clobber writes don't need logging because re-execution will produce the same values for them. Only the writes that destroy inputs the transaction depends on need to be saved.

A second log, **v_log**, captures volatile inputs (function arguments, stack values) at transaction begin so they're available during recovery on a fresh process.

---

## Repository layout

```
Clobber-NVM/
├── passes/                  LLVM 7.0.0 compiler passes (build → RollablePasses.so)
├── apps/runtime/            Runtime library (clobber_log, v_log, recovery)
├── apps/{bptree,hashmap,    Data structure benchmarks
│        rbtree,skiplist}/
├── apps/memcached/          Memcached port
├── apps/stamp/              STAMP suite (vacation, yada)
├── traces/                  YCSB traces
├── taslock/                 Spinlock library
├── mnemosyne-gcc/           Mnemosyne baseline (for comparison)
├── *clang                   Compiler wrapper scripts (each enables a different pass combo)
├── build_*.sh               Build scripts
└── run_*.sh, *.sh           Benchmark runners
```

---

## The compiler passes ([`passes/`](../Clobber-NVM/passes/))

All pass source files are LLVM 7.0.0 IR transforms, compiled into `RollablePasses.so` and loaded via `opt`.

| Pass file | What it does |
|---|---|
| `MemoryIdempotenceAnalysis.cpp` | Identifies clobber writes via alias + dependency analysis. The optimization core — decides which stores need a `clobber_log` callback. |
| `NaiveHook.cpp` | Inserts the runtime callbacks: `on_nvmm_write()` before clobber writes, `to_absolute_ptr()` to swizzle PMEMoid-style relative pointers into raw addresses on every persistent memory access. |
| `ClobberFunc.cpp` | Records function metadata (name, argument layout) at `txfunc` entry so recovery can re-invoke the right function with the right args. |
| `GlobalVal.cpp` | Instruments accesses to volatile globals so they can be captured in the v_log. |

The conservative analysis from §4.4 of the paper is in `MemoryIdempotenceAnalysis.cpp`: it first identifies *candidate input reads* (reads not dominated by an earlier same-address write), then *candidate clobber writes* (later writes that may target the same address). A second pass (dependency propagation) prunes false candidates — *unexposed* and *shadowed* writes (paper Figure 5).

---

## The runtime library ([`apps/runtime/`](../Clobber-NVM/apps/runtime/))

Built on top of PMDK v1.6 `libpmemobj`. The clobber log is layered over PMDK's existing undo log API, so the runtime stays small.

| File | Role |
|---|---|
| `clobber.h`, `clobber.c` | Public API + main callback implementations: `on_nvmm_write()`, `to_absolute_ptr()`, `pmalloc()`, `pfree()` |
| `context.c` | `ThreadContext` struct, `tx_open()` / `tx_commit()`, per-thread logs |
| `pmdk.c`, `wrapper.c`, `nolog.c` | Alternative runtime backends (PMDK-only, no-logging) for comparing against |

The user-facing API is small and mirrors PMDK's transaction style:
- `tx_open(ctx)` / `tx_commit(ctx)` — transaction boundaries (the paper's `txbegin`/`txend`)
- `pmalloc(size)` / `pfree(ptr)` — persistent allocation
- `vlog_preserve(ptr, size)` — explicitly preserve a volatile pointer's target into the v_log
- The `clobber_log` callback at clobber writes and `to_absolute_ptr` at persistent accesses are inserted by the compiler — the programmer doesn't write them.

---

## Compiler wrapper scripts (`*clang`)

These are shell wrappers around `clang + opt + llc` that load different combinations of passes. They exist mostly to support the paper's evaluation (the breakdown experiments need to disable specific optimizations).

| Wrapper | Pass flags | Use case |
|---|---|---|
| `clobberclang` | `-naivehook -statelessfunc` | The full Clobber-NVM compiler — what most apps use |
| `clobberlogclang` | `-naivehook` + `-always-inline` | Clobber-log-only mode (used in §5.3 breakdown) |
| `rollclang` | `-naivehook` | Re-execution + memory hooks, no clobber-write analysis |
| `rollinlineclang` | `-naivehook` + inlining | Variant of rollclang |
| `rollstatelessclang` | `-naivehook -statelessfunc` | Stateless function handling without clobber optimization |
| `statelessclang_hrs` | `-statelessfunc` | Stateless function handling alone |
| `vlogclang_hrs` | `-clobberfunc` | v_log metadata recording only |

For day-to-day use: **`clobberclang` is the one you want**.

---

## Programming model — what the code looks like

From the paper (Figure 2(a)) and confirmed by the apps, a Clobber-NVM transaction is a function (`txfunc`) that:

```c
void plist_ins(plist *lst, char *v, size_t vsz, lock *v_lk) {
    lock(&lst->lk); lock(v_lk);          // strong strict 2PL
    txbegin();
    vlog_preserve(v, vsz);                // volatile arg → v_log
    pnode *n = pmalloc(sizeof(pnode));    // persistent alloc
    n->val = pmalloc(strlen(v));
    strcpy(n->val, v);
    n->nxt = lst->hd;
    lst->hd = n;                          // clobber write (compiler-detected)
    txend();
    unlock(&lst->lk); unlock(v_lk);
}
```

The compiler:
1. Finds `lst->hd = n` is a clobber write (overwrites the input read on the previous line via `lst->hd`)
2. Inserts a `clobber_log` callback before that store
3. Inserts `to_absolute_ptr` swizzling at every persistent access
4. Records the function name and arg layout for recovery

The runtime ensures recovery either resumes a partially-completed transaction (replaying inputs from `clobber_log` and `v_log`, then re-invoking the function) or commits cleanly.

---

## Benchmarks ([`apps/`](../Clobber-NVM/apps/))

| Benchmark | Description |
|---|---|
| `bptree` | B+ tree, per-node reader-writer locks |
| `hashmap` | 256-bucket hashmap, per-bucket reader-writer locks |
| `rbtree` | Red-black tree, single global rwlock |
| `skiplist` | 32-level skiplist, single global lock |
| `memcached` | Persistent port of memcached |
| `vacation` | STAMP travel reservation app |
| `yada` | STAMP Delaunay mesh refinement |

YCSB workload traces live in [`traces/`](../Clobber-NVM/traces/).

---

## Build flow

`build.sh` orchestrates everything:

1. `build_llvm.sh` — downloads + builds LLVM 7.0.0, symlinks `passes/` into LLVM's `Transforms/` tree, compiles `RollablePasses.so`
2. `pmdk.sh` — clones PMDK v1.6 and installs system-wide
3. `atlas.sh` — builds HP Atlas (for comparison)
4. `build_mnemosyne.sh` — builds Mnemosyne (for comparison)
5. `build_clobberpass.sh` — builds the `ClobberPass.so` variant
6. `build_taslock.sh` — builds the spinlock library
7. `apps/build_runtime.sh` — builds the Clobber-NVM runtime

Dependencies are installed via `deps.sh` (LLVM, autoconf, libndctl-dev, libdaxctl-dev, libevent-dev, libjemalloc-dev, libboost-graph-dev, scons, etc.). PMDK is fetched and built from source by `pmdk.sh`.

The repo expects an NVM filesystem mounted at `/mnt/ram` — see [`apps/ext4.sh`](../Clobber-NVM/apps/ext4.sh). On this machine, our DAX-mounted filesystem is at `/mnt/pmem-emu`, so a path adjustment will be needed before running anything.

---

## How to read this codebase if you're modifying it

- **To add a new clobber-logged data structure:** look at `apps/skiplist/` (smallest), write the operations as `txfunc` functions with `lock → txbegin → ... → txend → unlock`, allocate persistent objects with `pmalloc`, and compile with `clobberclang`.
- **To understand what the compiler is doing to your code:** dump LLVM IR before and after the pass. The interesting analysis is in `passes/MemoryIdempotenceAnalysis.cpp`; instrumentation is in `passes/NaiveHook.cpp`.
- **To understand the runtime:** start at `apps/runtime/clobber.c` — `on_nvmm_write` is the clobber log callback, `tx_open`/`tx_commit` are the transaction boundaries.

---

## Key papers to reference alongside

- **Mnemosyne** (Volos et al., ASPLOS '11) — redo logging via STM, explicit `atomic{}` blocks. Baseline.
- **Atlas** (Chakrabarti et al., OOPSLA '14) — undo logging with lock-inferred FASEs. Most transparent of the three; also the closest in spirit to type-driven `persistent<T>`. Baseline.
- **iDO** (Liu et al., MICRO '18) — earlier recovery-via-resumption. Clobber-NVM compares directly and beats it (paper §5.4).

Both Mnemosyne and Atlas full PDFs are saved in our project; the Clobber-NVM paper is the immediate reference for this directory.
