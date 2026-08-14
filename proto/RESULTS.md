# Coder-core benchmark: range vs rANS on real CAPS symbol streams

Symbols dumped symbol-for-symbol from real v6 streams with the exact decode
model (`scripts/entropy_audit.py`, BRUSHIE_AUDIT_DUMP) and re-encoded with
three coder cores in `proto/bench.cpp` (clang++ -O3, Apple M1).

## kodak01 512px q50 (19 bands, 360,970 binary symbols)

| Coder | Bytes | vs range | Enc ns/sym | Dec ns/sym |
|---|---:|---:|---:|---:|
| binary range coder (current v6) | 25,713 | — | ~6.4 | ~12 |
| rANS adaptive (12-bit probs, LIFO encode, causal decode) | 24,984 | **-2.8%** | ~7.5 (+3.5 pre-pass = ~11) | **~5.6 (2.1x)** |
| rANS static per band (per-ctx tables, headers included) | 26,601 | +3.5% | ~15 | ~6 |

## kodak01 512px q75 (19 bands, 471,530 symbols)

| Coder | Bytes | vs range | Enc ns/sym | Dec ns/sym |
|---|---:|---:|---:|---:|
| binary range coder | 43,401 | — | ~6.7 | ~11 |
| rANS adaptive | 42,444 | **-2.2%** | ~7.9 (+2.3 pre-pass = ~10) | **~5.1 (2.1x)** |
| rANS static (headers 2,017B) | 44,507 | +2.5% | ~15 | ~5.5 |

## Verdict

1. **rANS-adaptive wins on both axes vs the binary range coder**: -2.2 to
   -2.8% density (12-bit exact probabilities, no 11-bit quantization, no
   range-coder slack) and ~2.1x decode speed. rANS decoding is LIFO, so the
   encoder processes symbols in reverse; with causal contexts the encoder
   needs one extra adaptation pre-pass (~+3.5 ns/sym, i.e. encoder total
   ~1.5-1.7x the coder core alone). Alternative: mirrored (future-neighbor)
   contexts make encode single-pass at the cost of changing context
   semantics (v7 format decision).
2. **Static per-band tables are rejected at whole-band granularity**: the
   per-band (pass,ctx) probability headers (~1.6-2.0KB) exceed the
   adaptation-lag savings (~0); static payload alone ≈ adaptive payload.
   Static coding would need compact priors/shared tables to pay.
3. For a binary-context model, a tANS symbol table adds nothing over rANS's
   single compare (`slot >= c1`); the rANS formulation IS the table-driven
   fast path. No separate tANS variant needed.

## Roundtrip + integration notes

- 500/500 randomized adaptive roundtrips (Python reference), all 19 real
  bands verified bit-exact in C++ (rans-adapt dec ok=1, rans-static ok=1).
- The adaptive rANS scheme keeps the model causal in raster order: encoder
  pre-computes the per-symbol adaptation state (one forward pass), decoder
  adapts live while decoding forward. Context code stays identical to v6.
- Stream layout per band: [4B final state][16-bit chunks, little-endian,
  in reverse emission order]; renorm window 2^16, prob scale M=4096,
  emit threshold x >= f<<20.
