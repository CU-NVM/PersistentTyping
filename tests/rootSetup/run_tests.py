#!/usr/bin/env python3
"""Tests for persist-tool's root-setup pass, one directory per case in cases/.

Usage:
    ./run_tests.py                     every case
    ./run_tests.py extern_shared ...   only these
    ./run_tests.py -v                  print the tool / compiler / program
                                       output of each failing case
    ./run_tests.py --lib DIR           use DIR instead of persistentLib/

Each case is copied to outputs/<case>/ and checked in this order; the first
failing step is reported:
    dump       --dump-roots succeeds, prints every `dump:` line, and changes
               no file
    tool       root-setup (run `passes:` times) exits 0 with no `error:` for
               `expect: ok`; for `expect: fail` it exits non-zero, reports
               every `error:` line, and leaves every file untouched with no
               header written
    rewrite    the generated <main>_pers_root.hpp contains every `header:`
               line, and the rewritten files satisfy `in`/`notin`/`unchanged`
    compile    all rewritten .cpp files compile and link into one program
    runs       the program, run `runs:` times on one fresh pool, prints the
               `out:` lines each time; counters that keep growing prove the
               roots are reread from the pool rather than recreated

CASE file directives (one per line, `#` lines describe the case):
    inputs: a.cpp main.cpp     files passed to the tool (default: every .cpp
                               in the case, recursively, sorted)
    flags: -Iinclude           extra flags for the tool and the compiler
    expect: ok | fail          default ok
    error: <text>              fail: some `error:` line contains <text>
    toolargs: --               extra arguments for the tool only, after inputs
    nolog: <text>              the root-setup output does not contain <text>
    dump: <text>               the --dump-roots output contains <text>
    dumponce: <text>           ... contains <text> exactly once
    header: <text>             the generated root header contains <text>
    in <file>: <text>          the rewritten <file> contains <text>
    notin <file>: <text>       ... does not contain <text>
    unchanged: <file>          <file> is byte-identical to the original
    out: <line>                expected stdout line of every run, in order;
                               {n}, {n+K}, {An+B} use the run number n
    runs: N                    how many runs (default 2 when `out:` is given)
    passes: N                  run root-setup N times in a row (default 1)
    xfail: <reason>            known gap: reports XFAIL / XPASS and does not
                               affect the exit status

Only fundamental persistent<T> roots are run: a class T needs the
recurse-persistent pass for persistent<T> to allocate in pmem, which
tests/constructors covers. Class roots here stop at `compile`.

Each run gets its own pool file under outputs/ (PERSISTENT_POOL_PATH),
deleted when the case finishes.
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
TIMEOUT = 120

STEPS = ["dump", "tool", "rewrite", "compile", "runs"]


class CaseFailed(Exception):
    def __init__(self, step, why, log=""):
        super().__init__(why)
        self.step, self.why, self.log = step, why, log


class Spec:
    def __init__(self, path):
        self.desc, self.inputs, self.flags, self.toolargs = [], None, [], []
        self.nologs, self.dumponce = [], []
        self.expect, self.errors, self.dumps, self.headers = "ok", [], [], []
        self.ins, self.notins, self.unchanged, self.out = [], [], [], []
        self.runs, self.passes, self.xfail = None, 1, None
        for raw in path.read_text().splitlines():
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                self.desc.append(line.lstrip("# "))
                continue
            key, _, val = line.partition(":")
            key, val = key.strip(), val.strip()
            words = key.split(None, 1)
            if words[0] in ("in", "notin") and len(words) == 2:
                (self.ins if words[0] == "in" else self.notins).append((words[1], val))
            elif key == "inputs":   self.inputs = val.split()
            elif key == "flags":    self.flags = val.split()
            elif key == "toolargs": self.toolargs = val.split()
            elif key == "nolog":    self.nologs.append(val)
            elif key == "dumponce": self.dumponce.append(val)
            elif key == "expect":   self.expect = val
            elif key == "error":    self.errors.append(val)
            elif key == "dump":     self.dumps.append(val)
            elif key == "header":   self.headers.append(val)
            elif key == "unchanged": self.unchanged.append(val)
            elif key == "out":      self.out.append(val)
            elif key == "runs":     self.runs = int(val)
            elif key == "passes":   self.passes = int(val)
            elif key == "xfail":    self.xfail = val
            else:
                raise ValueError(f"{path}: unknown directive {line!r}")
        if self.expect not in ("ok", "fail"):
            raise ValueError(f"{path}: expect must be ok or fail")
        if self.runs is None:
            self.runs = 2 if self.out else 0


def snapshot(d):
    return {p.relative_to(d): p.read_bytes() for p in sorted(d.rglob("*")) if p.is_file()}


def run(cmd, cwd, env=None):
    p = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, capture_output=True,
                       text=True, timeout=TIMEOUT)
    return p.returncode, p.stdout + p.stderr


def expand(line, n):
    def sub(m):
        return str(int(m.group(1) or 1) * n + int(m.group(2) or 0))
    return re.sub(r"\{(\d*)n([+-]\d+)?\}", sub, line)


def check(name, spec, lib):
    work = OUT / name
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(CASES / name, work, ignore=shutil.ignore_patterns("CASE"))
    original = snapshot(work)
    inputs = spec.inputs or [str(p.relative_to(work)) for p in sorted(work.rglob("*.cpp"))]
    toolflags = [f"-I{lib}", f"-I{HERE}", "-std=c++17"] + spec.flags

    def changed():
        now = snapshot(work)
        return sorted(str(p) for p in set(now) | set(original) if now.get(p) != original.get(p))

    # dump: read-only view of the collected table
    code, log = run([TOOL, "--dump-roots", *inputs, *spec.toolargs, *toolflags], work)
    if code != 0:
        raise CaseFailed("dump", f"--dump-roots exited {code}", log)
    for d in spec.dumps:
        if d not in log:
            raise CaseFailed("dump", f"dump lacks {d!r}", log)
    for d in spec.dumponce:
        if log.count(d) != 1:
            raise CaseFailed("dump", f"{d!r} appears {log.count(d)} times, expected once", log)
    if changed():
        raise CaseFailed("dump", f"--dump-roots changed {changed()}", log)

    # tool
    logs = []
    for _ in range(spec.passes):
        code, log = run([TOOL, "--pass=root-setup", *inputs, *spec.toolargs, *toolflags], work)
        logs.append(log)
        if code != 0:
            break
    log = "\n".join(logs)
    for t in spec.nologs:
        if t in log:
            raise CaseFailed("tool", f"output contains {t!r}", log)
    errs = [l for l in log.splitlines() if "error:" in l]
    if spec.expect == "fail":
        if code == 0:
            raise CaseFailed("tool", "root-setup succeeded, expected it to fail", log)
        for e in spec.errors:
            if not any(e in l for l in errs):
                raise CaseFailed("tool", f"no error containing {e!r}", log)
        if changed():
            raise CaseFailed("tool", f"failed run still wrote {changed()}", log)
        return
    if code != 0 or errs:
        raise CaseFailed("tool", errs[0].strip() if errs else f"exited {code}", log)

    # rewrite
    headers = [p for p in work.rglob("*_pers_root.hpp") if p.relative_to(work) not in original]
    if len(headers) != 1:
        raise CaseFailed("rewrite", f"expected one generated root header, found {len(headers)}", log)
    htext = headers[0].read_text()
    for h in spec.headers:
        if h not in htext:
            raise CaseFailed("rewrite", f"header lacks {h!r}", htext)
    for f, text in spec.ins:
        if text not in (work / f).read_text():
            raise CaseFailed("rewrite", f"{f} lacks {text!r}", (work / f).read_text())
    for f, text in spec.notins:
        if text in (work / f).read_text():
            raise CaseFailed("rewrite", f"{f} still contains {text!r}", (work / f).read_text())
    for f in spec.unchanged:
        if (work / f).read_bytes() != original[Path(f)]:
            raise CaseFailed("rewrite", f"{f} was modified", (work / f).read_text())

    # compile
    binary = work / "prog"
    code, clog = run(["clang++", *toolflags, "-Wno-unused-variable", *inputs,
                      "-o", binary, "-lpmemobj"], work)
    if code != 0:
        first = next((l for l in clog.splitlines() if "error" in l), f"exited {code}")
        raise CaseFailed("compile", first.strip(), clog)

    # runs
    pool = work / "pool"
    env = dict(os.environ, PERSISTENT_POOL_PATH=str(pool))
    try:
        for n in range(1, spec.runs + 1):
            code, rlog = run([binary], work, env)
            want = [expand(l, n) for l in spec.out]
            got = rlog.splitlines()
            if code != 0 or got != want:
                raise CaseFailed("runs", f"run {n}: exit {code}, printed {got[:4]}, "
                                         f"expected {want[:4]}", rlog)
    finally:
        pool.unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--lib", type=Path, default=LIB)
    args = ap.parse_args()

    if not TOOL.exists():
        sys.exit(f"{TOOL} not built")
    names = args.cases or sorted(p.name for p in CASES.iterdir() if (p / "CASE").exists())
    OUT.mkdir(exist_ok=True)

    width = max(len(n) for n in names)
    failed = 0
    counts = {}
    for name in names:
        spec = Spec(CASES / name / "CASE")
        try:
            check(name, spec, args.lib.resolve())
            status, detail, log = "PASS", "", ""
        except CaseFailed as e:
            status, detail, log = "FAIL", f"[{e.step}] {e.why}", e.log
        except subprocess.TimeoutExpired as e:
            status, detail, log = "FAIL", f"timeout: {e.cmd[0]}", ""
        if spec.xfail:
            status = "XFAIL" if status == "FAIL" else "XPASS"
            detail = f"{spec.xfail}" + (f"  ({detail})" if detail else "")
        failed += status == "FAIL"
        counts[status] = counts.get(status, 0) + 1
        print(f"{name:<{width}}  {status:<5}  {detail}")
        if args.verbose and status in ("FAIL", "XPASS") and log:
            for l in log.splitlines()[-40:]:
                print(f"{'':<{width}}    | {l}")

    print("\n" + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
