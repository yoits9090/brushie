#!/usr/bin/env python3
"""Audit every byte in CAPS v1 streams and estimate symbol entropy."""
from __future__ import annotations
import csv
import math
import struct
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def uvar(buf: bytes, pos: int) -> tuple[int, int]:
    v = 0; shift = 0
    while True:
        if pos >= len(buf): raise ValueError("truncated varint")
        b = buf[pos]; pos += 1
        v |= (b & 0x7f) << shift
        if not b & 0x80: return v, pos
        shift += 7
        if shift >= 64: raise ValueError("varint overflow")

def unzig(v: int) -> int:
    return (v >> 1) ^ -(v & 1)

def audit(path: Path) -> list[dict]:
    data = path.read_bytes()
    if data[:4] != b"CAPS": raise ValueError(path)
    levels = struct.unpack_from("<H", data, 16)[0]
    tile = struct.unpack_from("<H", data, 18)[0]
    chunks = struct.unpack_from("<I", data, 32)[0]
    directory = struct.unpack_from("<I", data, 36)[0]
    payload_start = struct.unpack_from("<Q", data, 40)[0]
    by_group = defaultdict(lambda: {"chunks": 0, "directory_bytes": 0, "payload_bytes": 0,
                                    "coefficients": 0, "nonzero": 0, "symbols": Counter(),
                                    "zero_runs": Counter(), "modes": Counter()})
    for i in range(chunks):
        off = 64 + i * 40
        layer, band, channel = struct.unpack_from("<HBB", data, off)
        x, y = struct.unpack_from("<II", data, off + 4)
        tw, th, step, mode = struct.unpack_from("<HHHH", data, off + 12)
        poff, psize, count = struct.unpack_from("<QII", data, off + 20)
        key = (layer, band, channel)
        g = by_group[key]
        g["chunks"] += 1; g["directory_bytes"] += 40; g["payload_bytes"] += psize; g["coefficients"] += count
        g["modes"][mode] += 1
        pos = poff; end = poff + psize; decoded = 0
        if mode == 2:
            if psize < 1: raise ValueError(f"empty mode2 payload in {path} chunk {i}")
            bits = data[pos]; mask_bytes = (count + 7) // 8
            if bits < 1 or bits > 32 or psize < 1 + mask_bytes:
                raise ValueError(f"invalid mode2 header in {path} chunk {i}")
            mask_start = pos + 1
            nz = sum((data[mask_start + j // 8] >> (j & 7)) & 1 for j in range(count))
            value_bytes = (nz * bits + 7) // 8
            if psize != 1 + mask_bytes + value_bytes:
                raise ValueError(f"invalid mode2 size in {path} chunk {i}")
            bit = 0
            values_start = mask_start + mask_bytes
            for j in range(count):
                if not ((data[mask_start + j // 8] >> (j & 7)) & 1):
                    continue
                raw = 0
                for b in range(bits):
                    if (data[values_start + (bit + b) // 8] >> ((bit + b) & 7)) & 1:
                        raw |= 1 << b
                if (bits == 32 and raw & 0x80000000) or (bits < 32 and raw & (1 << (bits - 1))):
                    raw -= 1 << bits
                g["symbols"][raw] += 1; bit += bits
            g["nonzero"] += nz; decoded = count
        else:
            while decoded < count:
                run, pos = uvar(data, pos); g["zero_runs"][run] += 1; decoded += run
                if decoded == count: break
                code, pos = uvar(data, pos)
                q = unzig(code - 1); g["symbols"][q] += 1; g["nonzero"] += 1; decoded += 1
            if pos > end: raise ValueError(f"payload overrun in {path} chunk {i}")
    rows = []
    for (layer, band, channel), g in sorted(by_group.items()):
        total = sum(g["symbols"].values())
        entropy = 0.0
        if total:
            entropy = -sum((n / total) * math.log2(n / total) for n in g["symbols"].values())
        display_path = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
        rows.append({"file": display_path, "layer": layer, "band": band,
                     "channel": channel, "chunks": g["chunks"],
                     "directory_bytes": g["directory_bytes"], "payload_bytes": g["payload_bytes"],
                     "coefficients": g["coefficients"], "nonzero": g["nonzero"],
                     "mode0_chunks": g["modes"][0], "mode1_chunks": g["modes"][1],
                     "mode2_chunks": g["modes"][2],
                     "nonzero_symbol_entropy_bits": entropy,
                     "entropy_lower_bound_bytes": math.ceil(total * entropy / 8),
                     "actual_total_bytes": g["directory_bytes"] + g["payload_bytes"]})
    return rows

files = [ROOT / "examples/kodak01_512_q82.caps"]
files.extend(Path("/tmp").glob("kodak01_4096*.caps"))
files.extend(Path("/tmp").glob("kodak512-delta.caps"))
rows = []
for f in files:
    if f.exists(): rows.extend(audit(f))
with (ROOT / "entropy_audit.csv").open("w", newline="") as out:
    fields = list(rows[0].keys()) if rows else ["file"]
    writer = csv.DictWriter(out, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
print(f"audited {len(files)} stream(s), {len(rows)} groups")
