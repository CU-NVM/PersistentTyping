#!/usr/bin/env python3
"""Tests for persist-tool's check-persistent-types pass, one directory per case in cases/.

Usage:
    ./run_tests.py                 every case
    ./run_tests.py NAME ...        only these
    ./run_tests.py -v              print the tool / compiler / program output
                                   of each failing case

Each case is copied to outputs/<case>/ and checked in this order; the first
failing step is reported:
    tool       the pass (`pass:`, default check-persistent-types) exits 0 with no `error:`
               for `expect: ok`; for `expect: fail` it exits non-zero, prints
               every `error:` text, and (for a single pass) leaves every file
               untouched
    rewrite    the rewritten files satisfy `in` / `notin` / `count` / `unchanged`
    rerun      the same pass run again changes no file (check-persistent-types only)
    compile    with `out:` lines: all .cpp files compile and link into one
               program with PERSIST_TRANSFORMED as the tool leaves it
    runs       the program, run `runs:` times on one fresh pool, prints the
               `out:` lines each time; {n} is the run number

CASE file directives (one per line, `#` lines describe the case):
    inputs: a.cpp b.cpp        files passed to the tool (default: every .cpp)
    flags: -Iinclude           extra flags for the tool and the compiler
    pass: check-persistent-types | all   which pass to run (default check-persistent-types)
    expect: ok | fail          default ok
    error: <text>              some `error:` line contains <text>
    erroronce: <text>          exactly one `error:` line contains <text>
    in <file>: <text>          the rewritten <file> contains <text>
    notin <file>: <text>       ... does not contain <text>
    count <file> <n>: <text>   ... contains <text> exactly n times
    unchanged: <file>          <file> is byte-identical to the original
    compileerror: <text>       the compile step must fail with <text> (the
                               library's checks under PERSIST_TRANSFORMED)
    out: <line>                expected stdout line of every run, in order;
                               {n}, {n+K}, {n-K}, {n%K} use the run number n
    runs: N                    how many runs (default 2 when `out:` is given)
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CASES = HERE / "cases"
OUT = HERE / "outputs"
TOOL = ROOT / "persist-clang-tool" / "build" / "bin" / "persist-tool"
LIB = ROOT / "persistentLib"
CLANG_INC = "/usr/local/lib/clang/20/include"
TIMEOUT = 120


class CaseFailed(Exception):
    def __init__(self, step, why, log=""):
        super().__init__(why)
        self.step, self.log = step, log


def parse_case(path):
    spec = {"inputs": None, "flags": [], "pass": "check-persistent-types", "expect": "ok", "error": [],
            "erroronce": [], "in": [], "notin": [], "count": [], "unchanged": [],
            "out": [], "runs": None, "compileerror": []}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, _, val = line.partition(":")
        val = val.strip()
        words = key.split()
        if words[0] in ("in", "notin", "unchanged") and len(words) <= 2:
            if words[0] == "unchanged":
                spec["unchanged"].append(val)
            else:
                spec[words[0]].append((words[1], val))
        elif words[0] == "count":
            spec["count"].append((words[1], int(words[2]), val))
        elif key == "inputs":
            spec["inputs"] = val.split()
        elif key == "flags":
            spec["flags"] = val.split()
        elif key in ("error", "erroronce", "out", "compileerror"):
            spec[key].append(val)
        elif key in ("pass", "expect"):
            spec[key] = val
        elif key == "runs":
            spec["runs"] = int(val)
        else:
            raise ValueError(f"{path}: unknown directive `{line}`")
    return spec


# {n}, {n+K}, {n-K} and {n%K} in an `out:` line, with n the run number.
def expand(line, n):
    def sub(m):
        op, k = m.group(1), int(m.group(2) or 0)
        return str(n + k if op == "+" else n - k if op == "-" else n % k if op == "%" else n)
    return re.sub(r"\{n(?:([+%-])(\d+))?\}", sub, line)


def snapshot(d):
    return {p.relative_to(d): p.read_bytes() for p in d.rglob("*") if p.is_file()}


def run_tool(work, spec, inputs):
    cmd = [str(TOOL), f"--pass={spec['pass']}"] + inputs + [
        "-I", str(LIB), "-I", CLANG_INC, "-std=c++17"] + spec["flags"]
    r = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=TIMEOUT)
    return r.returncode, r.stdout + r.stderr


def check_case(name, verbose):
    spec = parse_case(CASES / name / "CASE")
    work = OUT / name
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(CASES / name, work, ignore=shutil.ignore_patterns("CASE"))
    inputs = spec["inputs"] or sorted(str(p.relative_to(work)) for p in work.rglob("*.cpp"))
    before = snapshot(work)

    code, log = run_tool(work, spec, inputs)
    errors = [l for l in log.splitlines() if "error:" in l]
    if spec["expect"] == "fail":
        if code == 0:
            raise CaseFailed("tool", "exits 0, expected failure", log)
        for e in spec["error"]:
            if not any(e in l for l in errors):
                raise CaseFailed("tool", f"no error mentions '{e}'", log)
        for e in spec["erroronce"]:
            n = sum(e in l for l in errors)
            if n != 1:
                raise CaseFailed("tool", f"'{e}' reported {n} times, expected once", log)
        # With `pass: all` the passes before the failing one have written.
        if spec["pass"] != "all" and snapshot(work) != before:
            raise CaseFailed("tool", "files changed although the pass failed", log)
        return
    if code != 0 or errors:
        raise CaseFailed("tool", f"exits {code}" + (f", {errors[0]}" if errors else ""), log)

    text = lambda f: (work / f).read_text()
    for f, t in spec["in"]:
        if t not in text(f):
            raise CaseFailed("rewrite", f"{f} lacks '{t}'", text(f))
    for f, t in spec["notin"]:
        if t in text(f):
            raise CaseFailed("rewrite", f"{f} still has '{t}'", text(f))
    for f, n, t in spec["count"]:
        got = text(f).count(t)
        if got != n:
            raise CaseFailed("rewrite", f"{f} has '{t}' {got} times, expected {n}", text(f))
    for f in spec["unchanged"]:
        if (work / f).read_bytes() != before[Path(f)]:
            raise CaseFailed("rewrite", f"{f} changed", text(f))

    if spec["pass"] == "check-persistent-types":
        once = snapshot(work)
        code, log2 = run_tool(work, spec, inputs)
        if code != 0 or snapshot(work) != once:
            raise CaseFailed("rerun", "a second run changed files or failed", log2)

    if not spec["out"] and not spec["compileerror"]:
        return
    exe = work / "prog"
    srcs = [str(work / i) for i in inputs]
    r = subprocess.run(["clang++", "-std=c++17", f"-I{LIB}", f"-I{work}"] + spec["flags"]
                       + srcs + ["-o", str(exe), "-lpmemobj"],
                       cwd=work, capture_output=True, text=True)
    if spec["compileerror"]:
        if r.returncode == 0:
            raise CaseFailed("compile", "compiles, but must not", r.stderr)
        for t in spec["compileerror"]:
            if t not in r.stderr:
                raise CaseFailed("compile", f"no compile error mentions '{t}'", r.stderr)
        return
    if r.returncode != 0:
        first = next((l for l in r.stderr.splitlines() if "error:" in l), "")
        raise CaseFailed("compile", first, r.stderr)
    pool = work / "pool"
    env = dict(os.environ, PERSISTENT_POOL_PATH=str(pool))
    env.setdefault("PMEM_IS_PMEM_FORCE", "1")
    try:
        for n in range(1, (spec["runs"] or 2) + 1):
            r = subprocess.run([str(exe)], cwd=work, capture_output=True, text=True,
                               env=env, timeout=TIMEOUT)
            want = [expand(l, n) for l in spec["out"]]
            if r.returncode != 0 or r.stdout.splitlines() != want:
                raise CaseFailed("runs", f"run {n} printed {r.stdout.splitlines()} "
                                 f"(exit {r.returncode}), expected {want}", r.stdout + r.stderr)
    finally:
        pool.unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    OUT.mkdir(exist_ok=True)
    names = args.names or sorted(p.name for p in CASES.iterdir() if (p / "CASE").exists())
    failed = 0
    for name in names:
        try:
            check_case(name, args.v)
            print(f"{name:24} PASS")
        except CaseFailed as e:
            failed += 1
            print(f"{name:24} FAIL   [{e.step}] {e}")
            if args.v:
                for line in e.log.rstrip().splitlines():
                    print(f"{'':24} | {line}")
    print(f"\n{len(names) - failed} PASS, {failed} FAIL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
