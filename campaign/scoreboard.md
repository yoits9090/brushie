# Campaign scoreboard

Frozen baselines (quick: 2 Kodak + chat + meme 512px; broad: 163 images).
Gates: windowed MS-SSIM .970/.985/.995, mean bytes per image.

| Track | .970 | .985 | .995 | Status |
|---|---:|---:|---:|---|
| CAPS v5 quick | 9,120 | 16,162 | 35,380 | baseline |
| CAPS v5 broad | 11,527 | 21,498 | 54,831 | baseline |
| AVIF broad | 9,397 | 18,640 | 43,613 | to beat 2x |
| MOONSHOT | ~4,700 | ~9,300 | ~21,800 | target |

## Labs
- coder-lab (colab-lab, branch lab/coder): bitplane/refinement modes,
  energy-bucket contexts, tANS/rANS prototype. Byte headroom: luma detail
  = 64-70% of stream (campaign/headroom.md).
- geom-lab (colab-sweep, branch lab/geom): flat/polynomial base modes,
  grain synthesis, chroma-from-luma, lapped transforms.
- metric-lab (colab-lab, branch lab/metric): SSIMULACRA2/Butteraugli/libjxl
  builds + honest re-gate + RDOQ revisit.
- speed-lab (colab-sweep, branch lab/speed): SIMD, parallel decode,
  branchless coder, low-level entropy rewrite spike.

## Log
- 2026-08-12: campaign infra: 2 CPU boxes, total instrumentation
  (BRUSHIE_TRACK timelines, byte audits, callgrind/cachegrind), 4 labs
  spawned, corpus headroom map produced.
