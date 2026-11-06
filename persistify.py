#!/usr/bin/env python3
"""
Persistify a benchmark using persist-clang-tool.

The tool rewrites files in place, so every pass runs on its own copy of the
tree the previous pass left behind:

  <repo>/<benchmark>/                 the user code, never touched
    -> input/<benchmark>/             copy of the user code
    -> out_check/<benchmark>/         after check-persistent-types (unchanged)
    -> out_root/<benchmark>/          after root-setup
    -> out_recurse/<benchmark>/       after recurse-persistent (final)
    -> <repo>/Output/<benchmark>/     copy of the final tree

(stage dirs live under persist-clang-tool/)

Usage:
    ./persistify.py <benchmark>
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT           = Path(__file__).resolve().parent
TOOL_DIR       = ROOT / "persist-clang-tool"
TOOL           = TOOL_DIR / "build" / "bin" / "persist-tool"
PERSISTENT_LIB = ROOT / "persistentLib"
OUTPUT_DIR     = ROOT / "Output"

INPUT_STAGE = TOOL_DIR / "input"

# (pass, stage dir it writes), in the order they run
PASSES = [
    ("check-persistent-types", TOOL_DIR / "out_check"),
    ("root-setup",             TOOL_DIR / "out_root"),
    ("recurse-persistent",     TOOL_DIR / "out_recurse"),
]

# Each benchmark: source dir, the sources to hand the tool (relative to that
# dir), extra -I dirs (relative too) and the -std it is compiled with.
BENCHMARKS = {
    "counter": {
        "src":            ROOT / "counter",
        "sources":        ["user_counter.cpp"],
        "extra_includes": [],
        "std":            "c++17",
    },
    "stack": {
        "src":            ROOT / "stack",
        "sources":        ["user_stack.cpp"],
        "extra_includes": [],
        "std":            "c++17",
    },
    "hanoi": {
        "src":            ROOT / "hanoi",
        "sources":        ["user_hanoi.cpp"],
        "extra_includes": [],
        "std":            "c++17",
    },
    "DS": {
        "src":            ROOT / "DS",
        "sources":        ["src/main.cpp"],
        "extra_includes": ["include"],
        "std":            "c++17",
    },
    "kvstr": {
        "src":            ROOT / "kvstr",
        "sources":        ["user_kvstr.cpp"],
        "extra_includes": [],
        "std":            "c++17",
    },
    "kvnode2": {
        "src":            ROOT / "kvnode2",
        "sources":        ["user_kvnode2.cpp"],
        "extra_includes": [],
        "std":            "c++17",
    },
    "kvnode": {
        "src":            ROOT / "kvnode",
        "sources":        ["user_kvnode.cpp"],
        "extra_includes": [],
        "std":            "c++17",
    },
    "ycsb": {
        "src":            ROOT / "ycsb",
        "sources":        ["src/main.cpp", "src/ycsb_benchmark.cpp"],
        "extra_includes": ["include", "zipf_base/src", "src"],
        "std":            "c++20",
    },
}


def clean_stage(name: str) -> None:
    for d in [INPUT_STAGE / name, OUTPUT_DIR / name] + [s / name for _, s in PASSES]:
        if d.exists():
            shutil.rmtree(d)


def stage(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    # build artifacts are not user code
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("*.o"))
    print(f"  copied {src} -> {dst}")


def build_cmd(pass_name: str, bm: dict) -> list:
    includes = [f"-I{inc}" for inc in bm["extra_includes"]]
    return (
        [str(TOOL), f"--pass={pass_name}"] + bm["sources"] + includes +
        [f"-I{PERSISTENT_LIB}", "-I/usr/local/lib/clang/20/include", f"-std={bm['std']}"]
    )


def run_pass(pass_name: str, tree: Path, bm: dict) -> None:
    cmd = build_cmd(pass_name, bm)
    print(f"\n[{pass_name}] (cd {tree}) $ {' '.join(cmd)}\n")
    rc = subprocess.run(cmd, cwd=tree).returncode
    if rc != 0:
        sys.exit(f"pass '{pass_name}' failed (exit {rc})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Persistify a benchmark by running persist-clang-tool end-to-end.")
    parser.add_argument("benchmark", choices=sorted(BENCHMARKS.keys()),
                        help="Benchmark name")
    args = parser.parse_args()

    name = args.benchmark
    bm = BENCHMARKS[name]
    if not bm["src"].exists():
        sys.exit(f"error: source {bm['src']} does not exist")
    if not TOOL.exists():
        sys.exit(f"error: {TOOL} not built")

    print(f"== persistify: {name} ==")
    clean_stage(name)
    stage(bm["src"], INPUT_STAGE / name)

    prev = INPUT_STAGE / name
    for pass_name, stage_dir in PASSES:
        stage(prev, stage_dir / name)
        run_pass(pass_name, stage_dir / name, bm)
        prev = stage_dir / name

    stage(prev, OUTPUT_DIR / name)
    print(f"\n== done: Output/{name}/ ==")


if __name__ == "__main__":
    main()
