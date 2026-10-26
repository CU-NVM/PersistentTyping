#!/usr/bin/env python3
"""Check the constructor tests in outputs/: transformed, compiled, correct,
durable and atomic.

Usage:
    ./results.py                 every test that has a <name>.cpp here
    ./results.py init_list ...   only these
    ./results.py --build         run `make -k` first
    ./results.py -v              print each run's output on failure

Columns (each test runs on fresh pools, see harness.hpp for the modes):
    transformed  outputs/<name>.cpp exists (the tool ran without error:)
    compiled     outputs/<name> is built and newer than the transformed source
    matches C++  STATE lines of the first run equal those of outputs/<name>.ref,
                 the original compiled as plain C++
    durable      run, run, kill (SIGKILL after commit), run: RUNS counts 1..4,
                 STATE and object count never change, so state is reread
                 from the pool rather than rebuilt
    abort        exception after the `new`, inside make()'s transaction: the
                 slot stays null, no object is left allocated, a later run
                 builds a correct object
    crash-make   SIGKILL after the `new` and the slot assignment, before commit:
                 after recovery the slot is null and the object count is back
                 to what it was; a later run builds a correct object
    crash-ctor   same, killed inside the constructor body; n/a when the
                 constructor under test has no body to put the crash point in

"durable" here means surviving process exit and SIGKILL. The pool is a regular
file, so the page cache keeps unflushed writes across a SIGKILL; showing that
every write is flushed needs pmemcheck, which is not installed.

A test whose header has an `XFAIL:` line is expected to fail; it reports XFAIL
(or XPASS) and does not affect the exit status.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "outputs"
TIMEOUT = 60

PASS, FAIL, NA, SKIP = "PASS", "FAIL", "n/a", "-"


class Run:
    def __init__(self, proc):
        self.code = proc.returncode
        self.out = proc.stdout
        self.lines = proc.stdout.splitlines()

    def killed(self):
        return self.code == -9

    def states(self):
        return [l for l in self.lines if l.startswith("STATE ")]

    def field(self, key):
        """Words after `key` on its first line, or None."""
        for l in self.lines:
            if l.startswith(key + " "):
                return l.split()[1:]
            if l == key:
                return []
        return None


class Pools:
    """One fresh pool file per scenario, all under one temp dir."""

    def __init__(self, name):
        self.dir = Path(tempfile.mkdtemp(prefix=f".pools-{name}-", dir=OUT))

    def run(self, binary, pool, mode):
        env = dict(os.environ, PERSISTENT_POOL_PATH=str(self.dir / pool))
        try:
            p = subprocess.run([str(binary), mode], env=env, capture_output=True,
                               text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            p = subprocess.CompletedProcess([binary, mode], -1, "TIMEOUT\n", "")
        if p.stderr:
            p.stdout += "".join("stderr: " + l + "\n" for l in p.stderr.splitlines())
        return Run(p)

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


def tests_in_dir():
    return sorted(p.stem for p in HERE.glob("*.cpp"))


def is_xfail(name):
    head = (HERE / f"{name}.cpp").read_text().splitlines()[:10]
    return any("XFAIL:" in l for l in head)


def check(name, verbose):
    """Returns (row: dict column -> status, notes: list of str)."""
    row = {c: SKIP for c in COLUMNS}
    notes = []
    src, binary, ref = OUT / f"{name}.cpp", OUT / name, OUT / f"{name}.ref"

    def fail(col, why, *runs):
        row[col] = FAIL
        notes.append(f"{col}: {why}")
        if verbose:
            for r in runs:
                notes.extend("    | " + l for l in r.lines)

    if not src.exists():
        log = OUT / f"{name}.tool.log"
        errs = [l for l in log.read_text().splitlines() if "error:" in l] if log.exists() else []
        fail("transformed", errs[0].strip() if errs else "outputs/%s.cpp missing (run make)" % name)
        return row, notes
    row["transformed"] = PASS

    if not binary.exists() or binary.stat().st_mtime < src.stat().st_mtime:
        log = OUT / f"{name}.build.log"
        errs = [l for l in log.read_text().splitlines() if "error:" in l] if log.exists() else []
        fail("compiled", errs[0].strip() if errs else
                         f"outputs/{name} missing or stale (run `make {name}`)")
        return row, notes
    row["compiled"] = PASS

    if not ref.exists():
        fail("matches C++", f"outputs/{name}.ref missing (run `make {name}`)")
        return row, notes

    pools = Pools(name)
    try:
        expected = pools.run(ref, "ref", "run").states()
        if not expected:
            fail("matches C++", "reference printed no STATE line")
            return row, notes

        # Correctness + durability on one pool.
        runs = [pools.run(binary, "durable", m) for m in ("run", "run", "kill", "run")]
        first = runs[0]

        # After an abort or crash the pool must build the same object as an
        # undisturbed run; whether that object is correct is "matches C++".
        def builds_correctly(col, pool, after):
            r = pools.run(binary, pool, "run")
            if r.code != 0 or r.states() != first.states():
                fail(col, f"run after {after} printed {r.states()} (exit {r.code}), "
                          f"an undisturbed run printed {first.states()}", r)
                return False
            return True

        if first.code != 0 or not first.states():
            fail("matches C++", f"first run failed (exit {first.code})", first)
            return row, notes
        if first.states() != expected:
            fail("matches C++", f"got {first.states()}, expected {expected}", first)
        else:
            row["matches C++"] = PASS
        want_codes = [0, 0, -9, 0]
        objs = {tuple(r.field("OBJS") or ()) for r in runs}
        bad = [i for i, r in enumerate(runs)
               if r.code != want_codes[i] or r.field("RUNS") != [str(i + 1)]
               or r.states() != first.states()]
        if bad:
            r = runs[bad[0]]
            fail("durable", f"run {bad[0] + 1}: RUNS {r.field('RUNS')}, exit {r.code}, "
                            f"STATE {r.states()}", r)
        elif len(objs) != 1:
            fail("durable", f"object count changed across runs: {sorted(objs)}", *runs)
        else:
            row["durable"] = PASS

        # Abort inside make()'s transaction.
        r = pools.run(binary, "abort", "abort")
        objs = r.field("OBJS")
        if r.field("ABORTED") is None:
            fail("abort", "make() did not throw out of its transaction", r)
        elif r.field("HAS") != ["0"]:
            fail("abort", "slot still set after abort", r)
        elif not objs or len(objs) != 2 or objs[0] != objs[1]:
            fail("abort", f"object count before/after abort: {objs}", r)
        elif builds_correctly("abort", "abort", "abort"):
            row["abort"] = PASS

        # Crash before commit, after the `new` / inside the constructor.
        for col, mode in (("crash-make", "crash-make"), ("crash-ctor", "crash-ctor")):
            r = pools.run(binary, mode, mode)
            if r.field("NOCRASH") is not None and mode == "crash-ctor":
                row[col] = NA
                continue
            if not r.killed():
                fail(col, f"process was not killed (exit {r.code})", r)
                continue
            before = r.field("OBJS")
            i = pools.run(binary, mode, "inspect")
            if i.code != 0:
                fail(col, f"reopen after crash failed (exit {i.code})", i)
            elif i.field("HAS") != ["0"]:
                fail(col, "slot still set after recovery", i)
            elif i.field("OBJS") != before:
                fail(col, f"object count {before} before crash, {i.field('OBJS')} after recovery", i)
            elif builds_correctly(col, mode, "recovery"):
                row[col] = PASS
    finally:
        pools.close()
    return row, notes


COLUMNS = ["transformed", "compiled", "matches C++", "durable", "abort", "crash-make", "crash-ctor"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("tests", nargs="*", help="test names (default: all)")
    ap.add_argument("--build", action="store_true", help="run `make -k` first")
    ap.add_argument("-v", "--verbose", action="store_true", help="show run output on failure")
    args = ap.parse_args()

    names = args.tests or tests_in_dir()
    unknown = [n for n in names if not (HERE / f"{n}.cpp").exists()]
    if unknown:
        sys.exit(f"unknown test(s): {' '.join(unknown)}")

    if args.build:
        subprocess.run(["make", "-k", "--no-print-directory"] + names, cwd=HERE,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    OUT.mkdir(exist_ok=True)
    width = max(len(n) for n in names) + 2
    header = "test".ljust(width) + "".join(c.ljust(13) for c in COLUMNS) + "verdict"
    print(header)
    print("-" * len(header))

    unexpected = 0
    all_notes = []
    for name in names:
        row, notes = check(name, args.verbose)
        failed = FAIL in row.values()
        xfail = is_xfail(name)
        if xfail:
            verdict = "XFAIL" if failed else "XPASS"
        else:
            verdict = "FAIL" if failed else "ok"
            unexpected += failed
        print(name.ljust(width) + "".join(row[c].ljust(13) for c in COLUMNS) + verdict)
        if notes:
            all_notes.append((name, notes))

    for name, notes in all_notes:
        print(f"\n{name}:")
        for n in notes:
            print("  " + n)

    print(f"\n{len(names)} tests, {unexpected} unexpected failure(s)")
    sys.exit(1 if unexpected else 0)


if __name__ == "__main__":
    main()
