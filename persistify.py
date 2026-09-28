#!/usr/bin/env python3
"""
Persistify a benchmark using persist-clang-tool.

For a chosen benchmark this script:
  1. Copies the benchmark's source tree from <repo>/<benchmark>/ into
     persist-clang-tool/input/<benchmark>/ (preserved as-is) and
     persist-clang-tool/output/<benchmark>/ (staging for root-setup).
  2. Runs root-setup pass on output/<benchmark>/.
  3. Stages output/<benchmark>/ → output_recurse/<benchmark>/.
  4. Runs recurse-persistent pass on output_recurse/<benchmark>/.
  5. Copies the fully-rewritten tree to <repo>/Output/<benchmark>/.

Usage:
    ./persistify.py <benchmark>

Benchmarks: counter, stack, hanoi, kvnode, DS
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT           = Path(__file__).resolve().parent
TOOL_DIR       = ROOT / "persist-clang-tool"
PERSISTENT_LIB = ROOT / "persistentLib"
OUTPUT_DIR     = ROOT / "Output"

INPUT_STAGE          = TOOL_DIR / "input"
OUTPUT_STAGE         = TOOL_DIR / "output"
OUTPUT_RECURSE_STAGE = TOOL_DIR / "output_recurse"

# Each benchmark: source dir + path-of-main relative to that dir, and any
# extra -I dirs the tool/compile commands need.
BENCHMARKS = {
    "counter": {
        "src":             ROOT / "counter",
        "main":            "user_counter.cpp",
        "extra_includes":  [],
    },
    "stack": {
        "src":             ROOT / "stack",
        "main":            "user_stack.cpp",
        "extra_includes":  [],
    },
    "hanoi": {
        "src":             ROOT / "hanoi",
        "main":            "user_hanoi.cpp",
        "extra_includes":  [],
    },
    "DS": {
        "src":             ROOT / "DS",
        "main":            "src/main.cpp",
        "extra_includes":  ["include"],
    },
    "kvstr": {
        "src":             ROOT / "kvstr",
        "main":            "user_kvstr.cpp",
        "extra_includes":  [],
    },
    "kvnode2": {
        "src":             ROOT / "kvnode2",
        "main":            "user_kvnode2.cpp",
        "extra_includes":  [],
    },
    "kvnode": {
        "src":             ROOT / "kvnode",
        "main":            "user_kvnode.cpp",
        "extra_includes":  [],
    },
}


def clean_stage(name: str) -> None:
    for d in (INPUT_STAGE / name,
              OUTPUT_STAGE / name,
              OUTPUT_RECURSE_STAGE / name,
              OUTPUT_DIR / name):
        if d.exists():
            shutil.rmtree(d)


def copy_source(name: str, src: Path) -> None:
    if not src.exists():
        sys.exit(f"error: source {src} does not exist")
    shutil.copytree(src, INPUT_STAGE / name)
    shutil.copytree(src, OUTPUT_STAGE / name)
    print(f"  copied {src} -> input/{name}/ and output/{name}/")


def stage_recurse(name: str) -> None:
    shutil.copytree(OUTPUT_STAGE / name, OUTPUT_RECURSE_STAGE / name)
    print(f"  staged output/{name}/ -> output_recurse/{name}/")


def copy_final(name: str) -> None:
    shutil.copytree(OUTPUT_RECURSE_STAGE / name, OUTPUT_DIR / name)
    print(f"  copied output_recurse/{name}/ -> Output/{name}/")


def build_cmd(name: str, pass_name: str, tree: str, bm: dict) -> str:
    main_path = f"{tree}/{name}/{bm['main']}"
    includes = " ".join(f"-I {tree}/{name}/{inc}" for inc in bm["extra_includes"])
    return (
        f"./build/bin/persist-tool --pass={pass_name} {main_path} -- "
        f"{includes} "
        f"-I {PERSISTENT_LIB} "
        f"-I /usr/local/lib/clang/20/include "
        "-std=c++17 -lpmemobj"
    )


def run_pass(cmd: str, label: str) -> None:
    print(f"\n[{label}] $ {cmd}\n")
    rc = subprocess.run(cmd, shell=True, cwd=TOOL_DIR).returncode
    if rc != 0:
        sys.exit(f"pass '{label}' failed (exit {rc})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Persistify a benchmark by running persist-clang-tool end-to-end.")
    parser.add_argument("benchmark", choices=sorted(BENCHMARKS.keys()),
                        help="Benchmark name")
    args = parser.parse_args()

    name = args.benchmark
    bm = BENCHMARKS[name]

    print(f"== persistify: {name} ==")
    clean_stage(name)
    copy_source(name, bm["src"])

    run_pass(build_cmd(name, "root-setup",         "output",         bm), "root-setup")
    stage_recurse(name)
    run_pass(build_cmd(name, "recurse-persistent", "output_recurse", bm), "recurse-persistent")

    copy_final(name)
    print(f"\n== done: Output/{name}/ ==")


if __name__ == "__main__":
    main()
