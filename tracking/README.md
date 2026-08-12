# Brushie tracking convention

Every experiment produces artifacts under tracking/<tag>/ (git-ignored, kept
local). A tag is descriptive: <what>_<image>_q<quality>_<date> or
<lab>_<change>_<sha>.

Per-run artifacts (scripts/track.py run):
  run.json        provenance + totals: git sha, utc, host/box, quality,
                  stream bytes, bpp, wall ms, PSNR, stream sha256
  encode.jsonl    per-stage + per-chunk events (ns + rdtsc cycles on x86)
  decode.jsonl    same for the decode path
  timeline.csv    merged encode+decode events
  bytes.csv       scripts/byte_audit.py output: per-chunk directory/payload
                  bytes, nonzero counts, zero-runs, modes, entropy bound
  in.ppm/out.ppm/out.brbr  the actual artifacts (bit-exact, sha-verified)

Box profiling (box/prof.sh on a box):
  time_v.txt      /usr/bin/time -v (RSS, page faults, ctx switches)
  strace.txt      strace -c (syscall histogram)
  callgrind*.txt  callgrind_annotate dumps (Ir totals are reliable)
  cachegrind.txt  cg_annotate (D1/LLd misses)

Baseline (512px photo, q50, colab-lab, git a1d201b): 146.4M Ir, 1.59M D1m,
168K LLdm, 10.2MB RSS, 36/39 ms encode/decode wall, 1298 syscalls,
encode_band_arith ~43% self Ir.

Diff two runs with scripts/track_diff.py <tagA> <tagB> (timeline stages,
per-chunk bytes, and instruction deltas when prof/ artifacts exist).
