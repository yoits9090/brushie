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

## UI/text second representation (geom-lab lead, greenlit 2026-08-14)
geom-lab stage-0 (git 525f8e7): chat .970 stream anatomy - luma detail 49%
(glyph/avatar edges; text lines quantize away; flats already free via mode 12);
wavelet-representation FLOOR measured at ~1.5-1.8KB vs AVIF 812B (context bound
1,095B, zero-order 1,567B, model 1,913B, payload 2,165B). VERDICT: <1,500B
unreachable in the wavelet domain - second representation required. Stage-1
run-mode detail (mode 19) rejected (loses to mode 12). Lapped/smooth synthesis
closed (no free lunch at these rates). base_target=128 worse. BLOCK=32 wins 14B
but env-derived (not stream-safe) - not shipped.
DECISION: v8 second representation greenlit as campaign priority for geom-lab:
palette + edge map (directional contexts) + wavelet residual, trial-selected vs
wavelet-only; 4x4 directional intra as fallback; target chat .970 -> <1,200B.
coder-lab routed: chat context headroom (-38% luma detail) + tiny-stream
overhead (+12% vs 1.2-2% photos) for rANS v7 validation.

## v8 stage-B verdict + scope decision (2026-08-14, geom-lab 9c99b99)
- Palette+edge-map+residual: DEAD at full res (7,031 edge pixels x 1B = 7KB;
  total 12.9-13.1KB = 5x worse). Edge VALUES are the cost, not the map.
- Palette prequantization: RD saturates below gate (K=8: 1,493B @ 0.925); the
  11x11-windowed metric genuinely rewards AA fringes, which palette destroys.
  AA fringe = gate-rewarded content. Palette ideas stay dead.
- Naive 8x8 block-DCT without directional prediction + skip: 5-25x worse.
- Structural: wavelet is within ~1.5x of its information bound on chat
  (ctx bound 1,095B vs 2,358B current); the gap is coder-lab's context work.
- Measured small win: finer base LL + coarser detail (bm=0.2): 2,434B @ 0.97210
  vs 2,461B @ 0.97103 (~1% on UI images) - QPARAMS integration into the
  broad-bench per-image profile = geom-lab's current task.
- DECISION: AV1-lite DEFERRED. Re-evaluation trigger: after rANS v7 +
  directional text contexts + quant retune land, chat .970 > 1,600B ->
  greenlight AV1-lite; <= 1,400B -> wavelet path wins, AV1-lite dropped.
  Trigger recorded here.

## Metric-lab M8 closeout (2026-08-14, git e484587)
- FULL-CORPUS per-band STEPMUL search: S2-85 -8.5%, BA-0.9 -9.2% (163/163).
  Numbers: S2-85 81,698 (search) vs 89,271 (grid) vs 52,408 (JXL) = gap 1.56x
  (was 1.70x); BA-0.9 73,678 vs 81,162 vs 40,193. Pattern: mid layers +10-20%
  coarser, finest layer -10-18% finer, base saturated. Handed to coder-lab.
- NEW LEVER: top gates (S2-90/BA-0.6/0.4) barely move (-0.7..-5.1%) - bounded
  by the q99->lossless cliff; a FINER IN-CODEC HIGH-Q LADDER is the coder-side
  fix (coder-lab task). Top-gate gap is ladder-coarseness, not RD.
- LPIPS (alex 0.1.4) added to the stack. Grain probe: DEAD on ALL FOUR metrics
  (LPIPS +0.004..+0.032 too). Humans are the only open door for grain.
- M3 activity: closed under every real gate (+0.6..+74.4%). RDO: still loses
  (+0.9..+12.6%) - needs real-metric weights (handed off).

## v7 rANS SHIPPED TO MAIN (2026-08-14, coder-lab 31b2f4a, orchestrator merge 81e6ed6)
- Gate-matched quick harness: **8,400 / 15,264 / 33,966** vs frozen v5
  9,120/16,162/35,380 = **-7.9% / -5.6% / -4.0%**; 4/4 coverage. Decode wall
  13.9/13.7/16.1 vs v6 16.3/18.4/21.9 (~15-25% faster). Chat tiny-stream
  fixed-q -12.8..-14.6% (range-coder termination/convergence overhead gone).
- Verified: pixels identical at every q, v5/v6 streams byte-identical, fuzz
  clean, modes 13-17 roundtrip, tests pass. Default backend now rANS
  (BRUSHIE_RANS=0 forces v6 range coder). Mode 18 text-context shot rejected
  (+1.3..2.4% gate-matched; mode 12 already captures text structure; chat
  context-bound judged unreachable via adaptive dilution).
- Pending: main quick-harness confirmation run (campaign_v7_confirm).

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

- 2026-08-14: geom-lab v8 UI-path stage A/B simulations: palette prequant
  (K=8: -37% bytes, -0.046 metric — AA fringe is gate-rewarded), full-res
  edge maps (edge values 5x too expensive), naive block-DCT (5-25x worse
  without prediction) all rejected pre-C++. Chat .970 ctx bound = 1,095B
  (vs 2,358B current). SCOPE DECISION: QPARAMS/photo polish now; AV1-lite
  DEFERRED. RE-EVALUATION TRIGGER (recorded 2026-08-14): after rANS v7 +
  directional text contexts + quant retune land, measure chat .970 —
  if > 1,600B -> greenlight AV1-lite (multi-day, sub-agents);
  if <= 1,400B -> wavelet path wins, AV1-lite dropped permanently.
