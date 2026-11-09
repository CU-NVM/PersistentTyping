#!/usr/bin/env python3
"""Tests for persistentLib on its own, with no persist-tool involved.

Usage:
    ./run_tests.py            every test
    ./run_tests.py NAME ...   only these
    ./run_tests.py -v         print the output of each failing test

Three kinds of test:
    compile    type_identity.cpp (with PERSIST_TRANSFORMED) and
               untransformed.cpp (without) must compile and exit 0
    fail       each compile_fail/*.cpp must fail to compile under
               PERSIST_TRANSFORMED with the text on its `// expect:` line;
               a file with a `// untransformed` line is compiled without it
    scenario   ptr_array.cpp, run several times in sequence on one fresh pool;
               each run must print exactly the expected lines. A `crash:`
               mode is killed with SIGKILL, so only its first lines are checked.

Each scenario gets its own pool file under outputs/, deleted afterwards.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIB = HERE.parent.parent / "persistentLib"
OUT = HERE / "outputs"
CXX = ["clang++", "-std=c++17", f"-I{LIB}"]

# The state after one `run` on a fresh pool: the root slot array, then per
# table the table itself, its bucket array and one node.
AFTER_RUN_1 = ["ROOT 1", "T0 size 1 walk 1", "T1 size 1 walk 1", "OBJS 7"]
AFTER_RUN_2 = ["ROOT 1", "T0 size 2 walk 2", "T1 size 2 walk 2", "OBJS 9"]

# name -> [(mode, expected stdout lines, expected exit code)]
SCENARIOS = {
    # Run 2 rereads both arrays and the tables: only the two new nodes are
    # allocated, so nothing was reallocated or leaked.
    "two_run": [
        ("run", AFTER_RUN_1, 0),
        ("run", AFTER_RUN_2, 0),
    ],
    # Arrays of slots are one PMEMoid each with no destructor: no cookie.
    "new_array_no_cookie": [
        ("run", AFTER_RUN_1, 0),
        ("offsets", ["ROOTARRAY 0", "BUCKETS 0"], 0),
    ],
    # Tracked has a destructor, so new[] puts the element count in front of
    # the elements and the stored OID points 8 bytes into the allocation.
    "new_array_cookie": [
        ("cookie-make", ["HAS 1", "VALUES 10 20 30", "OFFSET 8"], 0),
        ("cookie-show", ["HAS 1", "VALUES 10 20 30", "OFFSET 8"], 0),
    ],
    "delete_array": [
        ("cookie-make", ["HAS 1", "VALUES 10 20 30", "OFFSET 8"], 0),
        ("cookie-delete", ["OBJS 1 0", "HAS 0"], 0),
        ("run", AFTER_RUN_1, 0),
        ("delete-all", ["OBJS 7 0"], 0),
        ("inspect", ["ROOT 0", "OBJS 0"], 0),
    ],
    # Killed inside the transaction that allocates the root array.
    "crash_root_alloc": [
        ("crash:root-alloc", ["OBJS 0"], -9),
        ("inspect", ["ROOT 0", "OBJS 0"], 0),
        ("run", AFTER_RUN_1, 0),
    ],
    # A write, allocation or delete in the pool needs the caller's
    # transaction: outside one, assigning a slot and `new` throw, and `delete`
    # (which cannot throw) stops the program. The pool is unchanged each time.
    "outside_tx": [
        ("cookie-make", ["HAS 1", "VALUES 10 20 30", "OFFSET 8"], 0),
        ("unlink-outside", ["THREW assignment to a pmem pointer outside a transaction",
                            "HAS 1", "VALUES 10 20 30", "OFFSET 8", "OBJS 1"], 0),
        ("alloc-outside", ["THREW allocation in the persistent pool outside a transaction",
                           "HAS 1", "VALUES 10 20 30", "OFFSET 8", "OBJS 1"], 0),
        ("delete-outside", [], -6),
        ("cookie-show", ["HAS 1", "VALUES 10 20 30", "OFFSET 8"], 0),
    ],
    # Killed in HashTable's constructor after its bucket array is allocated:
    # only the root array, committed by an earlier run, is left.
    "crash_ctor_bucket": [
        ("init-root", ["ROOT 1", "T0 none", "T1 none", "OBJS 1"], 0),
        ("crash:ctor-bucket", ["OBJS 1"], -9),
        ("inspect", ["ROOT 1", "T0 none", "T1 none", "OBJS 1"], 0),
        ("run", AFTER_RUN_1, 0),
    ],
    # Killed between `nd->next = table[k]` and `table[k] = nd`: no partial
    # node, no leak, and every table's size still equals its chain walk.
    "crash_insert_links": [
        ("run", AFTER_RUN_1, 0),
        ("crash:insert-links", ["OBJS 7"], -9),
        ("inspect", AFTER_RUN_1, 0),
        ("run", AFTER_RUN_2, 0),
    ],
    # Killed after `table[k] = nd` but before `size += 1`: the slot write must
    # be undone, or the walk would find a node that size does not count.
    "crash_after_link": [
        ("run", AFTER_RUN_1, 0),
        ("crash:after-link", ["OBJS 7"], -9),
        ("inspect", AFTER_RUN_1, 0),
    ],
}


class Failed(Exception):
    pass


def compile_program(src, out, transformed, log):
    flags = ["-DPERSIST_TRANSFORMED"] if transformed else []
    r = subprocess.run(CXX + flags + [str(src), "-o", str(out), "-lpmemobj"],
                       capture_output=True, text=True)
    log.append(r.stdout + r.stderr)
    return r.returncode == 0


def test_compile(name, transformed, log):
    exe = OUT / name
    if not compile_program(HERE / f"{name}.cpp", exe, transformed, log):
        raise Failed("does not compile")
    # The library opens a pool at startup, even for a program that never uses it.
    pool = OUT / f"{name}.pool"
    env = dict(os.environ, PERSISTENT_POOL_PATH=str(pool))
    env.setdefault("PMEM_IS_PMEM_FORCE", "1")
    try:
        r = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
    finally:
        pool.unlink(missing_ok=True)
    log.append(r.stdout + r.stderr)
    if r.returncode != 0:
        raise Failed(f"exits {r.returncode}")


def test_fail(src, log):
    text = src.read_text()
    m = re.search(r"^// expect: (.*)$", text, re.M)
    if not m:
        raise Failed("no `// expect:` line")
    flags = [] if re.search(r"^// untransformed$", text, re.M) else ["-DPERSIST_TRANSFORMED"]
    r = subprocess.run(CXX + flags + ["-fsyntax-only", str(src)],
                       capture_output=True, text=True)
    log.append(r.stderr)
    if r.returncode == 0:
        raise Failed("compiles, but must not")
    if m.group(1) not in r.stderr:
        raise Failed(f"error does not mention '{m.group(1)}'")


def test_scenario(name, steps, exe, log):
    pool = OUT / f"{name}.pool"
    pool.unlink(missing_ok=True)
    env = dict(os.environ, PERSISTENT_POOL_PATH=str(pool))
    env.setdefault("PMEM_IS_PMEM_FORCE", "1")
    try:
        for i, (mode, expected, code) in enumerate(steps, 1):
            r = subprocess.run([str(exe), mode], capture_output=True, text=True,
                               env=env, timeout=120)
            got = r.stdout.splitlines()
            log.append(f"$ {exe.name} {mode}  (exit {r.returncode})\n{r.stdout}{r.stderr}")
            if r.returncode != code:
                raise Failed(f"step {i} `{mode}` exits {r.returncode}, expected {code}")
            if got != expected:
                raise Failed(f"step {i} `{mode}` printed {got}, expected {expected}")
    finally:
        pool.unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    OUT.mkdir(exist_ok=True)

    tests = [("type_identity", lambda log: test_compile("type_identity", True, log)),
             ("untransformed", lambda log: test_compile("untransformed", False, log))]
    for src in sorted((HERE / "compile_fail").glob("*.cpp")):
        tests.append((f"fail/{src.stem}", lambda log, src=src: test_fail(src, log)))

    exe = OUT / "ptr_array"
    build_log = []
    built = compile_program(HERE / "ptr_array.cpp", exe, True, build_log)
    for name, steps in SCENARIOS.items():
        def run(log, name=name, steps=steps):
            if not built:
                log.extend(build_log)
                raise Failed("ptr_array.cpp does not compile")
            test_scenario(name, steps, exe, log)
        tests.append((name, run))

    if args.names:
        tests = [t for t in tests if t[0] in args.names]

    failed = 0
    for name, fn in tests:
        log = []
        try:
            fn(log)
            print(f"{name:28} PASS")
        except Failed as e:
            failed += 1
            print(f"{name:28} FAIL   {e}")
            if args.v:
                for chunk in log:
                    for line in chunk.rstrip().splitlines():
                        print(f"{'':28} | {line}")
    print(f"\n{len(tests) - failed} PASS, {failed} FAIL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
