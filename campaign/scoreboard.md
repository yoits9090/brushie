# Campaign scoreboard

Frozen MS-SSIM baselines (quick: 2 Kodak + chat + meme 512px; broad: 163 images).
Gates: windowed MS-SSIM .970/.985/.995, mean bytes per image.

| Track | .970 | .985 | .995 | Status |
|---|---:|---:|---:|---|
| CAPS v5 quick | 9,120 | 16,162 | 35,380 | baseline (Mac-reproducible exact) |
| CAPS v5 broad | 11,527 | 21,498 | 54,831 | baseline |
| AVIF broad | 9,397 | 18,640 | 43,613 | to beat 2x |
| MOONSHOT | ~4,700 | ~9,300 | ~21,800 | target |

## HONEST METRICS (metric-lab, 2026-08-13, 163 images, real SSIMULACRA2 + Butteraugli)
See campaign/landscape_real_metrics.md. CAPS holds FULL coverage at every gate
on every metric (its only-constant), but trails at high quality:

| Gate | best codec | CAPS | gap |
|---|---:|---:|---:|
| SSIMULACRA2 60 | AVIF 18,374 | 23,151 | 1.26x |
| SSIMULACRA2 75 | AVIF 31,124 | 45,253 | 1.45x |
| SSIMULACRA2 85 | JXL 52,408 | 89,271 | 1.70x |
| SSIMULACRA2 90 | WebP 72,159 | 182,609 | 2.53x |
| Butteraugli 1.3 | JXL 28,173 | 45,958 | 1.63x |
| Butteraugli 0.9 | JXL 40,193 | 81,162 | 2.02x |
| Butteraugli 0.6 | JXL 56,120 | 164,125 | 2.92x |
| Butteraugli 0.4 | JXL 79,705 | 223,644 | 2.81x |

JPEG XL is the king on real metrics (full coverage everywhere). Honest-metric
2x targets: SSIMULACRA2 60 ~9,200B; S2-75 ~15,600B; Butteraugli 1.3 ~14,100B.
Implications: the high-quality gap (2-3x at transparency) is where the
campaign's byte work must land; MS-SSIM under-reported it. Ringing/detail
coding at high q + rate allocation + lapped transforms + bitplane refinement.

## Labs
- coder-lab (branch lab/coder): inter-band value prediction (mode 13),
  bitplane/refinement, RDO rate model, rANS.
- geom-lab (branch lab/geom): plane predictors (modes 13/14) in progress;
  flat/polynomial base modes, grain synthesis, lapped transforms.
- metric-lab (branch lab/metric): real-metric toolchain (ssimulacra2,
  butteraugli, cjxl built in tools/) + landscape pipeline - DONE (v1).
  Next: RDOQ under real metrics, overfitting analysis.
- speed-lab (branch lab/speed): per-thread hotmap done; SIMD/parallel next.

## Log
- 2026-08-12: campaign infra: 2 CPU boxes, total instrumentation, 4 labs,
  corpus headroom maps, entropy audit (coder near context-optimal).
- 2026-08-13: boxes died (keep-alive DNS blip); re-provisioned brushie-sweep,
  3-VM account cap discovered, ssh adc auto-create fixed.
- 2026-08-13: metric-lab first honest landscape published (above).
- 2026-08-13/14: geom-lab shipped mode 16 adaptive flat-block base
  (9,116/16,143/35,352 quick, -0.1%); measured: base LL is 17-40% of
  chat/meme at .970 (detail bands dominate), plane/RLE/block-poly predictors
  lose to median, inter-band VALUE prediction loses on text (parent-significance
  already captures correlation). Gap is representational. Next: grain synthesis,
  merged-band fix, chroma-from-luma.
- 2026-08-13/14: coder-lab: energy-bucket sig contexts rejected (+0.7-4.4%);
  unary adaptation rate 1/16 wins ~-0.6% fixed-q (harness verification pending).
- 2026-08-13/14: speed-lab: branchless range coder + in-place 5/3 lifting
  (-56MB/encode zeroing) + persistent thread pool landed, byte-identical;
  fixing inverse_level odd-dim corruption found by the refactor.
