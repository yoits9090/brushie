#!/usr/bin/env python3
"""Decoder robustness fuzzer for Brushie streams.

Mutations: random byte flips, range overwrites, truncations, splices, and
structured header/directory field corruption. Any non-zero (or non-2) exit
code, crash, or timeout from the decode driver is reported with the failing
input saved to /tmp. Run:

    clang++ -std=c++17 -O2 -Iinclude src/codec.cpp tests/fuzz_driver.cpp \
        -o build/fuzz_driver -pthread
    python3 tests/fuzz.py [iterations]
"""
from __future__ import annotations

import os
import random
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRIVER = os.path.join(ROOT, "build", "fuzz_driver")
SEEDS = [p for p in sys.argv[2:]] or [
    "examples/kodak01_512_q82.brbr",
]


def valid_seeds() -> list[bytearray]:
    out = []
    for p in SEEDS:
        full = os.path.join(ROOT, p) if not os.path.isabs(p) else p
        if os.path.exists(full):
            out.append(bytearray(open(full, "rb").read()))
    return out


def mutate_header(b: bytearray) -> bytearray:
    for off, size in [(0, 4), (4, 2), (6, 2), (8, 4), (12, 4), (16, 2), (18, 2),
                      (20, 1), (24, 4), (28, 4), (32, 4), (36, 4), (40, 8),
                      (48, 4), (52, 4)]:
        if random.random() < 0.3:
            if size == 4:
                struct.pack_into("<I", b, off, random.choice(
                    [0, 1, 0xFFFFFFFF, 0x7FFFFFFF, random.getrandbits(32)]))
            elif size == 8:
                struct.pack_into("<Q", b, off, random.choice(
                    [0, 1, 0xFFFFFFFFFFFFFFFF, random.getrandbits(64)]))
            elif size == 2:
                struct.pack_into("<H", b, off, random.choice(
                    [0, 1, 0xFFFF, random.getrandbits(16)]))
            else:
                b[off] = random.randrange(256)
    return b


def mutate_directory(b: bytearray) -> bytearray:
    chunks = struct.unpack_from("<I", b, 32)[0]
    for _ in range(random.randint(1, 10)):
        ci = random.randrange(min(chunks, 200))
        off = 64 + ci * 40
        field = random.randrange(9)
        if field == 0:
            struct.pack_into("<H", b, off, random.choice([0, 1, 2, 0xFFFF, random.getrandbits(16)]))
        elif field == 1:
            b[off + 2] = random.randrange(8)
        elif field == 2:
            b[off + 3] = random.randrange(8)
        elif field == 3:
            struct.pack_into("<I", b, off + 4, random.getrandbits(32))
        elif field == 4:
            struct.pack_into("<I", b, off + 8, random.getrandbits(32))
        elif field == 5:
            struct.pack_into("<HH", b, off + 12, random.choice([0, 1, 0xFFFF]),
                              random.choice([0, 1, 0xFFFF]))
        elif field == 6:
            struct.pack_into("<H", b, off + 16, random.choice([0, 1, 0xFFFF, random.getrandbits(16)]))
        elif field == 7:
            struct.pack_into("<Q", b, off + 20, random.getrandbits(64))
        elif field == 8:
            struct.pack_into("<II", b, off + 28, random.getrandbits(32), random.getrandbits(32))
    return b


def main() -> int:
    if not os.path.exists(DRIVER):
        print("build the fuzz driver first (see docstring)")
        return 2
    seeds = valid_seeds()
    if not seeds:
        print("no valid seed streams found")
        return 2
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    random.seed(1234)
    crashes = hangs = 0
    for it in range(iterations):
        seed = bytearray(random.choice(seeds))
        mode = random.randrange(4)
        if mode == 0 and len(seed) > 10:
            for _ in range(random.randint(1, 8)):
                seed[random.randrange(len(seed))] ^= random.randint(1, 255)
        elif mode == 1 and len(seed) > 10:
            for _ in range(random.randint(1, 4)):
                p = random.randrange(len(seed))
                seed[p:p + 16] = os.urandom(min(16, len(seed) - p))
        elif mode == 2:
            seed = mutate_header(mutate_directory(seed))
            if random.random() < 0.3:
                seed = seed[: random.randrange(64, len(seed))]
        elif len(seed) > 10:
            other = bytearray(random.choice(seeds))
            p = random.randrange(len(seed))
            seed = seed[:p] + other[p:p + 64]
        case = "/tmp/brushie_fuzz_case.brbr"
        with open(case, "wb") as f:
            f.write(seed)
        try:
            r = subprocess.run([DRIVER, case], capture_output=True, timeout=3)
            if r.returncode not in (0, 2):
                crashes += 1
                out = f"/tmp/brushie_fuzz_crash_{it}.brbr"
                open(out, "wb").write(seed)
                print(f"CRASH it={it} rc={r.returncode} len={len(seed)} saved={out}")
                if crashes >= 5:
                    break
        except subprocess.TimeoutExpired:
            hangs += 1
            out = f"/tmp/brushie_fuzz_hang_{it}.brbr"
            open(out, "wb").write(seed)
            print(f"HANG it={it} len={len(seed)} saved={out}")
            if hangs >= 5:
                break
    print(f"fuzz complete: {iterations} iterations, crashes={crashes}, hangs={hangs}")
    return 1 if (crashes or hangs) else 0


if __name__ == "__main__":
    sys.exit(main())
