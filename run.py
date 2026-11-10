#!/usr/bin/env python3
"""
Compile and run a persistified benchmark from Output/.

Expects ./persistify.py <benchmark> to have already produced
Output/<benchmark>/.

Usage:
    ./run.py <benchmark>            # compile + run once
    ./run.py <benchmark> --fresh    # wipe pmem pool first (test first-run)

The pool is also wiped, without --fresh, when the built program differs from
the one that last used it: a pool's bytes follow one build's class layouts, so
a changed program (or another benchmark) must not read an old pool.
    ./run.py <benchmark> --runs N   # run N times in succession
    ./run.py <benchmark> -- ARGS    # pass ARGS to the benchmark binary
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT           = Path(__file__).resolve().parent
OUTPUT_DIR     = ROOT / "Output"
PERSISTENT_LIB = ROOT / "persistentLib"
PMEM_POOL_PATH = "/mnt/pmem-emu/global_persistent_pool"
# Fingerprint of the binary that last used the pool, kept next to it.
POOL_BUILD     = PMEM_POOL_PATH + ".build"


BENCHMARKS = {
    # Single-file benchmarks compile directly with clang++.
    "counter": {
        "kind":   "single",
        "main":   "user_counter.cpp",
        "binary": "user_counter",
    },
    "stack": {
        "kind":   "single",
        "main":   "user_stack.cpp",
        "binary": "user_stack",
    },
    "hanoi": {
        "kind":   "single",
        "main":   "user_hanoi.cpp",
        "binary": "user_hanoi",
    },
    "kvstr": {
        "kind":   "single",
        "main":   "user_kvstr.cpp",
        "binary": "user_kvstr",
    },
    "kvnode2": {
        "kind":   "single",
        "main":   "user_kvnode2.cpp",
        "binary": "user_kvnode2",
    },
    "kvnode": {
        "kind":   "single",
        "main":   "user_kvnode.cpp",
        "binary": "user_kvnode",
    },
    # Multi-file benchmark uses its own Makefile.
    "DS": {
        "kind":   "make",
        "binary": "bin/datastructures",
    },
    "ycsb": {
        "kind":   "make",
        "binary": "bin/ycsb",
    },
}


def run(cmd: str, cwd: Path = None) -> None:
    print(f"$ {cmd}")
    rc = subprocess.run(cmd, shell=True, cwd=cwd).returncode
    if rc != 0:
        sys.exit(rc)


def compile_benchmark(name: str, bm: dict, bench_dir: Path) -> None:
    if bm["kind"] == "single":
        cmd = (
            f"clang++ -std=c++17 -I{PERSISTENT_LIB} "
            f"{bm['main']} -o {bm['binary']} -lpmemobj"
        )
        run(cmd, cwd=bench_dir)
    elif bm["kind"] == "make":
        # Clear previous artifacts so a fresh make actually rebuilds.
        run("make clean", cwd=bench_dir)
        run(f"PERSIST_LIB={PERSISTENT_LIB} make", cwd=bench_dir)
    else:
        sys.exit(f"unknown benchmark kind: {bm['kind']}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile and run a persistified benchmark from Output/.")
    parser.add_argument("benchmark", choices=sorted(BENCHMARKS.keys()))
    parser.add_argument("--fresh", action="store_true",
                        help=f"Wipe {PMEM_POOL_PATH} before running")
    parser.add_argument("--runs", type=int, default=1,
                        help="Number of consecutive runs (default: 1)")
    # everything after -- goes to the benchmark binary
    argv = sys.argv[1:]
    split = argv.index("--") if "--" in argv else len(argv)
    args = parser.parse_args(argv[:split])
    bench_args = argv[split + 1:]

    name = args.benchmark
    bm = BENCHMARKS[name]
    bench_dir = OUTPUT_DIR / name
    if not bench_dir.exists():
        sys.exit(
            f"error: {bench_dir} does not exist. "
            f"Run ./persistify.py {name} first."
        )

    compile_benchmark(name, bm, bench_dir)

    built = hashlib.sha256((bench_dir / bm["binary"]).read_bytes()).hexdigest()
    previous = Path(POOL_BUILD).read_text().strip() if os.path.exists(POOL_BUILD) else None
    if os.path.exists(PMEM_POOL_PATH) and (args.fresh or previous != built):
        os.remove(PMEM_POOL_PATH)
        reason = "--fresh" if args.fresh else "the program changed since the pool was written"
        print(f"  wiped {PMEM_POOL_PATH} ({reason})")
    Path(POOL_BUILD).write_text(built + "\n")

    for i in range(args.runs):
        if args.runs > 1:
            print(f"\n=== run {i + 1} / {args.runs} ===")
        run(" ".join([f"./{bm['binary']}"] + bench_args), cwd=bench_dir)


if __name__ == "__main__":
    main()
