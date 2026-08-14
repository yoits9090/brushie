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

## Metric-lab M8 verdicts (2026-08-14, 78,566 candidates)
- MS-SSIM-tuned production selection is INVALID under real metrics: clears
  S2-85 on 6/163, S2-90 on 2/163, BA-0.6 on 3/163; +25..84% bytes where it
  clears. Real-metric re-selection wins -1.9%..-16.5% (S2-85 -11.8%,
  BA-0.9 -16.5%, BA-1.3 -11.9%).
- BRUSHIE_RDO still loses (+0.9..12.6%): sens table is proxy-fitted.
- M3 activity quantization re-rejected under S2 (v6 probe): +0.6..74.4%.
  Closed for good.
- Per-band STEPMUL pilot under SSIMULACRA2: -8.1% at S2-85 vs plain grid;
  winning pattern OPPOSITE of proxy-tuned table (coarser coarse layers,
  finer finest layers). Full-corpus sweep running. => quant-table retune
  against real metrics = ~8-16% free at high gates (coder-lab implements).
- Blind 2AFC rig built (scripts/blind_test_rig.py, 12 imgs x 6 codecs).

## Coder-lab v7 decision (2026-08-14, git 1d3626b)
- rANS adaptive prototype (symbol-exact dumps, 500/500 roundtrips, bit-exact
  C++ decode): **-2.2..-2.8% density + 2.1x decode speedup** (5.6 vs 12.0
  ns/sym); encode core ~1.5x (LIFO reverse pass + causal pre-pass); static
  per-band tables rejected (+3.5%, header costs). Context-modeling book
  CLOSED: modes 13/14/15/17 all measured and rejected (parent-mag +2.75..3.05%,
  parent-value +13.8..16.2%, energy buckets +0.64..4.39%, second-order sig
  +4.55..5.95%).
- Orchestrator decision: GREENLIGHT v7 rANS with pre-pass scheme (contexts
  unchanged); decode 2.1x flips our weakest competitive axis (decode 17ms vs
  WebP 4ms/JPEG 3ms/AVIF 12ms -> ~8ms). coder-lab owns semantics, speed-lab
  owns SIMD path. Mode numbering: geom took 16, coder renumbered to 17.

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
