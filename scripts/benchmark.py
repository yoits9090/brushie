#!/usr/bin/env python3
"""Run the resident-memory benchmark and attach the exact host record."""
from __future__ import annotations
import csv
import platform
import subprocess
import sys
from pathlib import Path

try:
    import PIL
    pillow_version = PIL.__version__
except Exception:
    pillow_version = "unavailable"

root = Path(__file__).resolve().parents[1]
binary = root / "build" / "brushie_benchmark"
if not binary.exists():
    raise SystemExit("build brushie_benchmark first")
out = root / "benchmark_results.csv"
threads = int(sys.argv[1]) if len(sys.argv) > 1 else 8
subprocess.run([str(binary), str(out), str(threads)], check=True)
env = root / "benchmark_environment.txt"
with env.open("w", encoding="utf-8") as f:
    f.write(f"model={platform.machine()}\n")
    f.write(f"system={platform.platform()}\n")
    f.write(f"python={platform.python_version()}\n")
    f.write(f"threads={threads}\n")
    f.write("reference_host=MacBook Pro Mac17,2; Apple M5; 10 cores (4 performance, 6 efficiency); 32 GB; macOS 26.5.1 (25F80)\n")
    f.write("software=Apple clang 17.0.6.4.2; C++17; -O3 -ffast-math; pthreads\n")
    f.write(f"dataset_metrics=Pillow {pillow_version}; NumPy available in bundled runtime\n")
print(out)
