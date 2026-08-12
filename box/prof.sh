#!/bin/bash
# Brushie instruction/cache/syscall profiler (Colab CPU box).
# Usage: box/prof.sh <binary> <ppm> <quality> <tag>   (run on the box)
# Produces: /content/prof/<tag>/{time_v.txt, strace.txt, callgrind.csv, cachegrind.csv}
set -e
BIN=$1; PPM=$2; Q=$3; TAG=$4
OUT=/content/prof/$TAG
mkdir -p "$OUT"
echo "== time -v =="
/usr/bin/time -v "$BIN" encode "$PPM" "$OUT/out.brbr" --quality "$Q" > "$OUT/enc_stdout.txt" 2> "$OUT/time_v.txt" || true
echo "== strace -c =="
strace -c -f "$BIN" encode "$PPM" "$OUT/out.brbr" --quality "$Q" > /dev/null 2> "$OUT/strace.txt" || true
echo "== callgrind (instruction counts) =="
valgrind --tool=callgrind --callgrind-out-file="$OUT/callgrind.out" \
  --collect-jumps=yes --cache-sim=yes "$BIN" encode "$PPM" "$OUT/out.brbr" --quality "$Q" \
  > /dev/null 2> "$OUT/callgrind_log.txt" || true
python3 - "$OUT" <<'EOF'
import re, sys, subprocess
out = sys.argv[1]
raw = subprocess.run(["callgrind_annotate", "--inclusive=yes", "--threshold=1",
                      out + "/callgrind.out"], capture_output=True, text=True).stdout
rows = []
cur = None
for line in raw.splitlines():
    m = re.match(r"^\s*([\d,]+)\s+\(([\d.]+)%\)\s+(.+)", line)
    if not m: continue
    parts = line.split()
    # lines look like: "6,849,054 (16.28%)  /build/... " or with prog count "1,234 ( 0.1%) ..."
    if len(parts) >= 3 and "%" in parts[1]:
        try:
            ir = int(parts[0].replace(",", ""))
        except ValueError:
            continue
        fn = " ".join(parts[3:]) if len(parts) > 3 else parts[2]
        if "???" in fn or "====" in line: continue
        cur = fn
        rows.append((ir, fn))
    else:
        m2 = re.match(r"^\s*([\d,]+)\s+(.+)$", line)
        if m2 and cur:
            try:
                rows.append((int(m2.group(1).replace(",", "")), cur + " | " + m2.group(2)))
            except ValueError:
                pass
rows.sort(reverse=True)
with open(out + "/callgrind.csv", "w") as f:
    f.write("instructions,function\n")
    for ir, fn in rows[:60]:
        f.write(f"{ir},\"{fn}\"\n")
print("callgrind rows:", len(rows), file=sys.stderr)
EOF
echo "== cachegrind =="
valgrind --tool=cachegrind --cachegrind-out-file="$OUT/cachegrind.out" \
  "$BIN" encode "$PPM" "$OUT/out.brbr" --quality "$Q" \
  > /dev/null 2> "$OUT/cachegrind_log.txt" || true
cg_annotate "$OUT/cachegrind.out" > "$OUT/cachegrind.txt" || true
python3 - "$OUT" <<'EOF'
import sys, re
out = sys.argv[1]
rows = []
for line in open(out + "/cachegrind.txt"):
    parts = line.split()
    if len(parts) >= 8 and parts[0].replace(",", "").isdigit():
        try:
            rows.append([int(x.replace(",", "")) for x in parts[:8]] + [" ".join(parts[8:])])
        except ValueError:
            pass
with open(out + "/cachegrind.csv", "w") as f:
    f.write("ir,d1mr,dlmr,d2mr,dl2mr,fn\n")
    for r in sorted(rows, reverse=True)[:60]:
        f.write(f"{r[0]},{r[1]},{r[2]},{r[3]},{r[4]},\"{r[8]}\"\n")
EOF
# summary
echo "== summary =="
grep -E "Maximum resident|User time|System time|Page faults|Context" "$OUT/time_v.txt" || true
grep -E "I\s+refs|D1\s+misses|LLd\s+misses" "$OUT/callgrind_log.txt" "$OUT/cachegrind_log.txt" || true
echo "PROF DONE -> $OUT"
