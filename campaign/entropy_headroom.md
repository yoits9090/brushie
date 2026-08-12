# Entropy headroom audit (the 2x verdict)

Method: scripts/entropy_audit.py re-walks every v5 stream symbol-for-symbol
with the exact decode model (contexts + adaptive probabilities) and measures
(a) model cross-entropy = ideal cost of the CURRENT model, (b) per-context
empirical entropy = optimal static coder with the SAME contexts, (c) zero-order
entropy. Validated by exact stream-consumption assertions on all chunks.
Corpus: 40 broad-corpus images at 512px, q30/50/75 (40 streams each).

## Per-gate headroom of the CURRENT entropy coder

| Gate proxy | luma detail share | ctx-model headroom (luma detail) | overall |
|---|---:|---:|---:|
| q30 (~.970) | 66.3% | 5.4% | 6.9% |
| q50 (~.985) | 68.8% | 3.7% | 5.6% |
| q75 (~.995) | 71.3% | **1.8%** | 3.8% |

Coder implementation overhead (range coder vs ideal arithmetic): ~1.2-2%
across all streams. Zero-order (context-free) gap: +5.8% (q30), +7.9% (q50),
+10.2% (q75).

## Verdict

The entropy coder is nearly context-optimal. Bitplane refinement, richer
significance contexts, and better unary models together can yield at most
~2-6% density, not 2x. The moonshot must change the SYMBOLS, not the coder:

1. **Rate allocation (RDO)** — the sensitivity table weights the base LL
   ~5000x the finest chroma detail; if the gates tolerate coarser fine bands
   for more base bits, per-band step search with coder-integrated rate
   estimation is the biggest single lever. (metric-lab RDOQ revisit +
   coder-lab BRUSHIE_RDO path; per-image adaptive profile currently only
   tries +/-4 quality on groups.)
2. **Inter-band value prediction** — parents currently gate only
   significance (zerotree-ish); predicting detail magnitudes AND values from
   the coarser band (2x2 parent block average) decorrelates the symbols the
   entropy audit sees. Changes luma-detail symbol statistics at every gate.
3. **Grain synthesis** — noise-dominated finest bands are pure entropy;
   detect + synthesize on decode (geom-lab). Direct byte cuts at .970/.985.
4. **Base-band structure** — base LL is 16-25% of the stream and only
   median-predicted; flat/polynomial/run-length modes attack the chat/meme
   gap (geom-lab).
5. **tANS/rANS** stays worth it for SPEED (146M Ir at 43% in encode_band_arith),
   with density gains ~1-2% — a speed-lab + coder-lab collaboration, not a
   density moonshot by itself.

Data: campaign/entropy_audit.csv (per chunk: pass bit counts, model bits,
ctx entropy, zero entropy).
