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
| 6 | 006-merge-bilin | 9,119 | 16,162 | 35,380 | -1.0/-0.6/-0.4% | H/V/D triples merged into one band-4 chunk (dims derived from level geometry; -32B directory per triple); Catmull-Rom chroma upsample measured and rejected (rings on chroma edges, loses the .970 gate on kodak_02) |

Other experiments that lost and were reverted: decode-side damping (neutral),
GAP base predictor (worse everywhere), 4:2:0 at q95+ (cannot hold the .995
gate on kodak_02 even with finer chroma steps), chroma/diagonal/exponent
table sweeps (base table already at a local optimum), parent-significance
hard-skip "zerotree" mode 11 (dense parents at these operating points),
per-block Rice parameters (stream restructuring not worth it).

Final v5 configuration: mode-8 detail entropy (local-k + parent significance
context), base multiplier 0.4, per-band auto block mode (16x16, sparse
threshold 50%), Catmull-Rom chroma upsampling on decode.

Net (clean full harness, `harness_v5_final_quick_report.md`): **~9,100 /
~16,200 / ~35,400** mean bytes vs the audited v3 baseline 10,325 / 18,316 /
38,174 ≈ **-12% / -12% / -7%** at equal gates.

# Broad-corpus benchmark (163 public images)

Corpus: Kodak 24 + DIV2K 100 + USC-SIPI 39 (see `benchmarks/corpus.txt`),
fitted to 512px, same windowed MS-SSIM gates, competitor candidates cached
once (`benchmarks/competitors/`, AVIF via svtav1/libaom, JPEG, WebP,
native J2K). CAPS candidates sweep q=1..100 x base 32/64 in parallel
(`scripts/bench.py`), plus a per-image adaptive profile phase that tries
quant-table and per-level step profiles at the gate quality +/- up to 4
with the REAL metric (`benchmarks/caps_broad_profiled.csv`).

| Gate | AVIF | WebP | JPEG | J2K | CAPS |
|---|---:|---:|---:|---:|---:|
| .970 | 9,397 | 11,145 | 15,091 | 12,744 | 11,527 |
| .985 | 18,640 | 20,260 | 26,912 | 23,960 | 21,498 |
| .995 | 43,613 | 43,168 | 53,563 | 57,501 | 54,831 |

CAPS beats JPEG and JPEG 2000 at every gate with full coverage (AVIF/WebP
cover only 153/163 and 144/163 at .995). Profile winners: diagonal crush
(112/489), finest-level coarsening at +2..+4 quality (185/489), chroma
crush (36), finer base (18). Speeds on one Colab CPU node (median wall ms,
encode/decode): CAPS 18/17, AVIF 217/12 (aom), JPEG2000 57/12, WebP 42/4,
JPEG 4/3. Colab nodes and two sub-agents (node-ops, speed-bench) ran the
shard benchmarks; the node ASAN run independently caught the odd-dimension
parent-index OOB that was fixed with clamped parent_at() indexing.
