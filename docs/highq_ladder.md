# High-q ladder mapping (coder-lab)

The top real-metric gates (S2-85/90, BA-0.6/0.4) select qualities at q95+
where the byte ladder is coarse; the q99 -> q100 (lossless) step is a ~2x
byte cliff with no intermediate operating points.

## Bytes-vs-q (512px kodak01/02, default v7 defaults)

| q | k01 | k02 |
|---|---:|---:|
| 95 | 138,737 | 103,516 |
| 96 | 145,204 | 110,893 |
| 97 | 153,738 | 120,091 |
| 98 | 167,069 | 134,725 |
| 99 | 175,745 | 141,389 |
| 100 | 353,857 | 316,897 |

## New in-codec ladder: BRUSHIE_QFINE=0..99

step(q+f) = round(lerp(step(q), step(q+1), f/100)) in the STEP domain, per
level (the naive fractional-loss formula collapses to the same integers at
high q because of step rounding). Integer qualities behave exactly as
before; steps are stored per chunk so all streams stay version-compatible
and decode without the env. At q99:

| f | k01 bytes | k02 bytes |
|---|---:|---:|
| 0 | 175,745 | 141,389 |
| 25 | 186,889 | — |
| 50 | 214,990 | — |
| 60 | 249,903 | — |
| 75 | 249,943 | — |
| 90 | 329,931 | — |
| 100 (q100) | 353,857 | 316,897 |

~5-6 new intermediate operating points per image between q99 and lossless;
transitions are per-level step integers (l1 2->1, l2 4->3->2->1, l3 6->5->4->3->2->1
with chroma/diagonal offsets), so points are unequally spaced; per-level
refinement beyond this uses BRUSHIE_LEVELMUL / BRUSHIE_STEPMUL in the
profile-search phase (finer per-group control).

## Proposal for the harness/profile phase (metric-lab)

1. Extend the high-q sweep to (q98, q99) x qfine in {25, 50, 60, 75, 90}
   for images whose best candidate lands at q >= 98.
2. After qfine selection, run the existing per-group stepmul search around
   the chosen point for the last few %.
3. Gate on S2-85/90 + BA-0.6/0.4 with the real-metric pipeline.

Open item: the lerp transitions are still ~10-30KB apart on photos; if the
gates land inside a gap, per-level LEVELMUL interpolation (e.g. refine one
level at a time) is the finer path — the knobs already exist.
