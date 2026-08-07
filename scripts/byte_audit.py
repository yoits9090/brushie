#!/usr/bin/env python3
"""Audit every byte in legacy and compact Brushie streams."""
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
    version = struct.unpack_from("<H", data, 4)[0]
    entry_bytes = 20 if version >= 3 else 40
    levels = struct.unpack_from("<H", data, 16)[0]
    tile = struct.unpack_from("<H", data, 18)[0]
    chunks = struct.unpack_from("<I", data, 32)[0]
    directory = struct.unpack_from("<I", data, 36)[0]
    payload_start = struct.unpack_from("<Q", data, 40)[0]
    if directory != chunks * entry_bytes or payload_start != 64 + directory:
        raise ValueError(f"invalid directory accounting in {path}")
    by_group = defaultdict(lambda: {"chunks": 0, "directory_bytes": 0, "payload_bytes": 0,
                                    "coefficients": 0, "nonzero": 0, "symbols": Counter(),
                                    "zero_runs": Counter(), "modes": Counter()})
    cumulative = payload_start
    for i in range(chunks):
        off = 64 + i * entry_bytes
        if version >= 3:
            layer, band, channel, mode = struct.unpack_from("<BBBB", data, off)
            tw, th, step = struct.unpack_from("<HHH", data, off + 4)
            psize = struct.unpack_from("<I", data, off + 12)[0]
            poff, count = cumulative, tw * th
            cumulative += psize
        else:
            layer, band, channel = struct.unpack_from("<HBB", data, off)
            tw, th, step, mode = struct.unpack_from("<HHHH", data, off + 12)
            poff, psize, count = struct.unpack_from("<QII", data, off + 20)
        key = (layer, band, channel)
        g = by_group[key]
        g["chunks"] += 1; g["directory_bytes"] += entry_bytes; g["payload_bytes"] += psize; g["coefficients"] += count
        g["modes"][mode] += 1
        pos = poff; end = poff + psize; decoded = 0
        if mode == 3:
            # v2 whole-band arithmetic payload: report sizes; symbol entropy
            # analysis is not available for the range-coded stream.
            g["nonzero"] += count
            decoded = count
        elif mode == 2:
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

files = [ROOT / "examples/kodak01_512_q82.brbr"]  # legacy v1 example
files.extend(Path("/tmp").glob("kodak01_4096*.brbr"))
files.extend(Path("/tmp").glob("kodak512-delta.brbr"))
rows = []
for f in files:
    if f.exists(): rows.extend(audit(f))
with (ROOT / "entropy_audit.csv").open("w", newline="") as out:
    fields = list(rows[0].keys()) if rows else ["file"]
    writer = csv.DictWriter(out, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
print(f"audited {len(files)} stream(s), {len(rows)} groups")
