# Experiment log (session)

Every intervention that was tried and either shipped or rejected, with the
evidence that decided it. All measurements use the repo's deterministic
4-scale MS-SSIM proxy at equal bytes or equal gate, on Kodak 01 512px unless
noted.

## Shipped

### v2 entropy coder (mode 3)
Replaced per-tile varint/bitpack (modes 0-2) with whole-band
context-adaptive binary arithmetic coding: 8 significance contexts (left,
above, above-left), 4 sign contexts, Golomb-Rice magnitudes with adaptive k
(per-band k0 byte + re-adaptation every 64 magnitudes), median-predicted
base LL (JPEG-LS LOCO-I). Whole-band chunks collapse the directory.
Result: 4.4-6.3x smaller than v1 at equal gates.

### Frequency-ordered quantization
Steps grow 2^(1.25*coarseness) with a 3-level saturation cap; base LL keeps
0.5x finer steps; diagonal weighted 1.5x. The v1 table had the direction
inverted (finest level got the smallest step), wasting bits on noise.

### Midtread quantization with plain q*step reconstruction
vs dead-zone (floor_div + JPEG2000 center reconstruction): at q=20 on a
512px photo, dead-zone gave 48.9 KB @ 0.951 MS-SSIM vs midtread 30.0 KB @
0.979. The dead-zone reconstruction bias penalizes SSIM-family metrics.
Midtread won decisively; dead-zone rejected.

### Chroma 4:2:0 at lossy quality
2x2 box downsample, bilinear upsample; header flag + chroma base dims.
Measured better than 4:4:4 on the proxy at equal settings (denoising-like
effect at low quality).

### Sign contexts (4)
Measured neutral on photos (-0.1% bytes), kept as principled for edge
content. Above-right significance context (8->16 contexts): slightly worse
(context dilution), reverted.

### In-place pyramid build + single-buffer inverse
Peak RSS at 4096^2: encode 446->413 MB, decode 442->420 MB. Bit-identical.

### Rate-control recalibration (--target-bytes)
The v1 ratio thresholds collapsed onto q=20 after v2's 4-6x byte reduction;
recalibrated against the v2 sweep, spans q=5..90 monotonically.

### RGBA alpha + PNG I/O
Channel count 4: alpha at full resolution (luma walk + luma steps). PNG
read/write via libpng when built with BRUSHIE_HAVE_PNG.

### Fuzz harness
tests/fuzz.py + fuzz_driver.cpp: 8,000+ random + structured header/directory
mutations, zero crashes/hangs.

## Rejected (with evidence)

### CDF 9/7 transform
Clamped-boundary 9/7 round-tripped but produced 2x the high-pass energy of
5/3 (gain mismatch with the 5/3-calibrated table). After gain-normalizing
the transform to DC=1.0/Nyquist=1.0, the energy profile matched 5/3 almost
exactly (H: 1,773K vs 1,713K abs-sum) — no compaction win on this content.
The canonical 9/7's advantage requires RDO-style per-band allocation
(JPEG2000 PCRD), which is a separate lever. Rejected as a transform swap.

### Analytical 2D Gaussian splat layer (design candidate #9)
Greedy isotropic-Gaussian splat fitting on the base LL (K=0..400) did not
reduce the residual entropy below the original LL entropy estimate
(2,070 B orig vs 2,257-3,219 B with splats+residual at K=50-400): the LL of
a photo is not Gaussian-friendly, and without per-image gradient descent
(forbidden) the splats cannot win. Rejected for photos; the technique could
still matter for stylized content, parked in roadmap M7.

### Mean-|coefficient| per-band rate allocation
step *= (global_mean_abs / band_mean_abs)^k with k in {-0.35..0.35}.
k=-0.35: 10.6 KB @ 0.9615 vs baseline 11.9 KB @ 0.9701 at q=5 (helps .970 by
~5%), but k=-0.35 at .985 needs ~22 KB vs baseline 19.4 KB. The adjustment
slides the RD curve without improving it; the empirical table stands.

### Splat variant in C++ (9/7 + ddata refs)
Built a full 9/7 double-precision variant with a header flag; worked
(lossless tests passed) but the RD was 3x worse before step rescaling and
neutral-to-worse after; the normalized energy analysis explained why.
Reverted; the variant exists in /tmp only.

## Notes on measurement discipline

- Always compare at equal MS-SSIM gate or equal bytes, never at equal
  quality settings.
- The proxy is directional, not a perceptual claim. Re-gate on
  SSIMULACRA2/Butteraugli before shipping claims (roadmap M8).
