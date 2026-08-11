# CAPS recursion log (v4/v5)

Session log for the recursive eval that took CAPS from the audited v3
baseline to the v5 stream. Every jump below is measured with the same
exhaustive quick harness (2 Kodak photos + chat + meme at 512px, windowed
MS-SSIM `brushie-box11-ms-ssim-v1`, smallest-bytes-per-gate selection).
The live matplotlib dashboard (localhost:3567, `dash/progress.png`) plots
the same numbers with per-jump annotations.

| # | Tag | .970 | .985 | .995 | Jump | What changed |
|---:|---|---:|---:|---:|---|---|
| 0 | 000-baseline | 10,325 | 18,316 | 38,174 | — | audited v3 (sign+significance contexts overlapped by a historical quirk) |
| 1 | 001-sign-sig-sep | 9,605 | 17,196 | 36,289 | -7.0/-6.1/-4.9% | v4: significance and sign contexts separated (old code shared one index range, wasting bits); parent/class A/B variants rejected |
| 2 | 002-local-k | 9,381 | 16,497 | 35,800 | -2.3/-4.1/-1.3% | per-coefficient Rice parameter from causal neighbour magnitudes; mode-9 magnitude prediction and mode-10 class contexts rejected |
| 3 | 003-rdo (rejected) | 9,711 | 17,285 | 36,082 | worse | closed-loop per-level step allocation with a metric-calibrated distortion proxy; the proxy cannot track windowed MS-SSIM, search disabled by default (kept behind BRUSHIE_RDO) |
| 4 | 004-v5 | 9,292 | 16,406 | 35,693 | -0.9/-0.6/-0.3% | v5: 16-byte directory (per-chunk checksum dropped, 4B x chunks) |
| 5 | 005-block-auto | 9,209 | 16,266 | 35,518 | -0.9/-0.9/-0.5% | per-band 16x16 block-significance mode (EBCOT-style) chosen automatically for block-sparse bands; helps synthetic content a lot (chat .970 2,897 -> 2,562; meme 2,080 -> 1,930), neutral-positive on photos |

Other experiments that lost and were reverted: decode-side damping (neutral),
GAP base predictor (worse everywhere), 4:2:0 at q95+ (cannot hold the .995
gate on kodak_02 even with finer chroma steps), chroma/diagonal/exponent
table sweeps (base table already at a local optimum), parent-significance
hard-skip "zerotree" mode 11 (dense parents at these operating points),
per-block Rice parameters (stream restructuring not worth it).

Final v5 configuration: mode-8 detail entropy (local-k + parent significance
context), base multiplier 0.4, per-band auto block mode (16x16, sparse
threshold 50%), Catmull-Rom chroma upsampling on decode.

Net (clean full harness, `harness_v4_final_quick_report.md`): **9,516 /
16,311 / 35,534** mean bytes vs the audited v3 baseline 10,325 / 18,316 /
38,174 = **-7.8% / -10.9% / -6.9%** at equal gates. CAPS beats JPEG and
JPEG 2000 at every gate and WebP at .995; AVIF remains 1.47x / 1.26x / 1.12x
smaller (block-adaptive intra prediction on synthetic content).
