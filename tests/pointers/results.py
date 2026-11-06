#!/usr/bin/env python3
"""Runs tests/constructors/results.py over the tests in this directory."""

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("results", HERE.parent / "constructors" / "results.py")
results = importlib.util.module_from_spec(spec)
spec.loader.exec_module(results)
results.HERE = HERE
results.OUT = HERE / "outputs"
results.main()
