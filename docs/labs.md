# Brushie lab playbook (multi-agent campaign)

Goal (persistent): reach 2x-better-than-competitor density at equal quality.
Targets: ~4,700 / 9,300 / 21,800 mean bytes at the .970/.985/.995 windowed
MS-SSIM gates on the 163-image broad corpus (half of AVIF's 9,397/18,640/43,613),
CPU-only, encode+decode at least as fast as today (18/17 ms @ 512px Colab).

## Frozen baselines (all labs measure against these)
Quick harness (2 Kodak + chat + meme, 512px): CAPS v5 = 9,120 / 16,162 / 35,380.
Broad corpus (163 images): CAPS 11,527 / 21,498 / 54,831 vs AVIF 9,397 / 18,640 /
43,613, WebP 11,145 / 20,260 / 43,168, JPEG 15,091 / 26,912 / 53,563,
J2K 12,744 / 23,960 / 57,501. CAPS: 4/4 and 163/163 coverage everywhere.

## Layout
- main repo:        /Users/ace/projects/brushie          (orchestrator only)
- coder-lab:        /Users/ace/projects/brushie-coder    branch lab/coder    box: colab-lab
- geom-lab:         /Users/ace/projects/brushie-geom     branch lab/geom     box: colab-sweep
- metric-lab:       /Users/ace/projects/brushie-metric   branch lab/metric   box: colab-lab
- speed-lab:        /Users/ace/projects/brushie-speed    branch lab/speed    box: colab-sweep
- datasets/ is a symlink to the orchestrator's datasets (163 images, read-only).
- benchmarks/competitors/ has cached AVIF/WebP/JPEG/J2K candidate rows (read-only).

## Boxes (Colab CLI CPU VMs, Ubuntu 22.04, 2 vCPU / 12GB)
- `ssh colab-lab` and `ssh colab-sweep` (ProxyCommand via `colab ssh`).
- Box repo: /content/brushie (built binary /content/brushie/build/brushie).
- Session management only via: `colab --auth adc sessions|status|stop <name>`.
  NEVER `colab stop` a session another lab uses. Heavy parallel sweeps ->
  `colab run` ephemeral VMs (self-cleaning) or local ProcessPool (10-core Mac).
- Sync your branch to your box slot:
  `git archive <branch> | ssh <box> "mkdir -p /content/labs/<lab> && tar x -C /content/labs/<lab>"`
  then build there with clang++ (see box/build.sh). Datasets live on the box at
  /content/brushie/datasets (read-only shared).

## Harness location note
The Mac's brew ffmpeg lacks libaom-av1/libwebp encoders, so competitor rows
(AVIF/WebP) only encode correctly on the BOXES (ffmpeg 4.4.2 has libaom-av1 +
libwebp + libopenjpeg). Run enterprise_eval.py / bench.py on your box slot
(python3 + numpy + PIL preinstalled there), or the boxes' shared
/content/brushie checkout. Local Mac runs are CAPS-only (use for fast
iterate/decode checks, not for competitor comparisons).

## Measurement discipline (non-negotiable)
1. Build locally: clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp
   src/main.cpp -o build/brushie -pthread (or cmake). Run tests: build/test_codec.
2. Gate 1: `python3 scripts/enterprise_eval.py --quick --output-prefix lab_X`
   (2 Kodak + chat/meme, ~3-5 min). Compare aggregate vs frozen baseline.
3. Gate 2 (winners only): broad corpus
   `python3 scripts/bench.py caps --images benchmarks/corpus.txt
    --competitors benchmarks/competitors --out <out> --workers 8`.
4. Every change (shipped or rejected) gets a row in docs/experiments_log.md
   with the numbers that decided it, and every benchmark run gets a
   `python3 scripts/track.py run <image> <q> --tag <descriptive-tag>` artifact.
5. Never tune against one image; never change the metric mid-comparison.

## Instrumentation (use it constantly)
- Timeline: BRUSHIE_TRACK=1 ./build/brushie encode in.ppm out.brbr --quality Q
  emits per-stage + per-chunk JSONL (ns + rdtsc cycles on x86) to
  brushie_track.jsonl (or BRUSHIE_TRACK_FILE=...). scripts/track.py wraps
  encode+decode+byte-audit+PSNR into tracking/<tag>/ (works with --box).
- Every byte: scripts/byte_audit.py <stream> -> per-chunk directory/payload
  bytes, nonzero counts, zero-runs, modes, entropy lower bound.
- Instructions/caches (box only; perf is seccomp-blocked in Colab):
  box/prof.sh <binary> <ppm> <q> <tag> -> time -v, strace -c, callgrind
  (I refs), cachegrind (D1/LLd misses). Totals are reliable; per-function
  attribution can be warped by callgrind artifacts (verify suspicious rows).
- Baseline profile (512px photo, q50, colab-lab): 146.4M Ir, 1.59M D1m,
  168K LLdm, ~10MB RSS, encode 36ms/decode 39ms wall, 1298 syscalls.
  encode_band_arith is ~43% of self Ir; worker-thread subtree ~91%.

## Codec env knobs (no rebuild needed)
BRUSHIE_ENTROPY=3..12 (detail mode; default 8 = local-k), BRUSHIE_GAP=1,
BRUSHIE_RDO=1 (closed-loop per-level step search, currently loses on the
proxy), BRUSHIE_STEPMUL="g:m,..." (per-group step multipliers),
BRUSHIE_BLOCK=8|16|32|64 (block-mode size), BRUSHIE_444_Q=<q> (chroma
subsample threshold, default 95). Stream v5: 64B header + 16B/chunk
directory, cumulative payload offsets, merged H/V/D band-4 chunks.

## Rules of engagement
- Deterministic, CPU-only, no per-image optimization/search at decode time.
- Every format change must keep old streams decodable or be clearly versioned.
- Commit on your branch only; the orchestrator merges after evidence review.
- Risk is encouraged (rewrites, new entropy backends, SIMD, format breaks
  behind new mode bytes) but everything must be measured at equal quality.
- Spawn your own sub-agents (rlm) for parallel subtasks; have them write
  files and reply with agent_message. Report wins/failures to the
  orchestrator with numbers and artifact paths.
