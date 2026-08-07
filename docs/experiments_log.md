> **Metric correction:** All competitive rankings below that cite the retired
> global-moment proxy are historical engineering diagnostics only. Harness v3
> invalidated the "beats WebP/JPEG" conclusions. Use `docs/harness_v3.md`.

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

## Rejected (continued)

### M3: activity-aware quantization (AUREA Turing-field idea, adapted)
Zero-bit 3-class edge map derived from the dequantized luma base LL
(gradient energy, integer thresholds); detail coefficients map to LL cells
by integer ratio; per-coefficient effective steps. Both directions measured
at equal proxy quality on Kodak 01 512px:

| Direction | Mod (edge/smooth) | Result vs baseline |
|---|---|---|
| edge-preserving | step*0.5 / step*1.5 | q5: 20.0 KB @ 0.981 vs 11.9 KB @ 0.970 -> ~17 KB at 0.970 gate, WORSE |
| psychovisual masking | step*1.5 / step*0.8 | ~12.6 KB at 0.970 vs 11.9 KB, WORSE; ~21 KB at 0.985 vs 19.4 KB, WORSE |
| mild variants | 0.75/1.3, 1.3/0.85, 2.0/0.75 | all worse at equal quality |

The MS-SSIM proxy does not reward spatial step modulation: it is dominated
by overall per-pixel fidelity, so redistributing error spatially loses to
the frequency-ordered baseline. The masking direction may still win on
SSIMULACRA2/Butteraugli (perceptual) — parked behind M8 validation. The
implementation exists in /tmp only (codec_activity.cpp / codec_mask.cpp).

### RDOQ-lite (rate-distortion-optimized quantization)
Midtread + drop-to-zero when lambda*bits > q^2*step^2, with a static rate
model (2 + log2(|q|+1)). With a static model the decision is a uniform hard
threshold per magnitude: lambda_k < ~1/3 keeps everything, >= 1/3 drops all
q=1 coefficients (q5: 4.9 KB @ 0.93) -> equivalent to a dead-zone, which was
already rejected. A useful RDOQ needs per-context rate estimates from the
actual coder state (pre-pass with the adaptive probabilities), which is
larger than a quick experiment. Parked as a roadmap item.

## Where this leaves the campaign

Every structural lever tried this session (9/7, splats, rate allocation,
activity quantization, RDOQ-lite) failed to beat the empirically-calibrated
v2 baseline on the MS-SSIM proxy. The remaining levers are all larger
architectural items (bitplane refinement, per-context RDO, directional
prediction) tracked in docs/roadmap.md M4-M7. The v2 codec's measured
historical position is invalidated by harness v3. Corrected quick results put
CAPS behind strong JPEG/WebP/AVIF; the architecture must now optimize against
`brushie-box11-ms-ssim-v1`, not the retired proxy.

## Shipped (continued)

### Per-position Rice remainder contexts + wider unary model
Remainder bits now use one adaptive context per bit position (kCtxRem 1->13)
and the unary context set grew 14->24. Measured neutral at the .970/.985/
.995 gates (within +-0.1%) and -3.9% at q=90 (visually-lossless tier):
58,875 -> 56,597 bytes on Kodak 01 512px at equal MS-SSIM (0.997). Kept for
the high-quality/camera tier where Brushie is closest to AVIF.

### Harness-v3 high-quality allocation (SHIPPED)
The corrected local metric exposed a q99→q100 rate cliff on the hard Kodak
photo: q99 was 78,896 B at .99457, forcing a 241,350 B lossless stream to
cross .995. A quality-adaptive level exponent keeps the delivery table
(1.25) below q95 and switches to 0.8 at q95..99. Corrected quick-corpus
results from the parameter sweep:

- .970: unchanged (~11.35 KB mean)
- .985: unchanged (~19.35 KB mean)
- .995: 77.87 KB → 40.30 KB (-48.3%, full coverage)

The hard photo now crosses .995 at q97 in 91,072 B rather than q100 in
241,350 B. This should put CAPS below full-coverage WebP/JPEG2000 at .995;
a clean exhaustive rerun is required before publishing the final aggregate.

### Adaptive base-depth mode (SHIPPED as encoder option)
Corrected-metric sweep found complementary modes at 512px:

- base target 64 (historical default) wins photographs and the .995 tier;
- base target 32 cuts UI/meme .970 by ~23% and .985 by ~10%, but hurts
  photographs and high-quality UI.

`EncodeOptions.base_target` and CLI `--base-target 32|64` expose both; pyramid
levels/base dimensions already self-describe the mode, so there is no stream
format flag or decoder change. Harness v3 now competes both targets per
quality and selects the smallest actual match.

### Corrected-metric diagonal/chroma allocation (SHIPPED)
A 6-variant quick-corpus sweep under harness v3 found quality-dependent
weights with no gate tradeoff: below q95 use diagonal 1.8x and chroma 2.5x;
at q95+ relax to diagonal 1.2x and chroma 2.0x. Compared with 1.5x/2.0x,
expected full-coverage means improve ~1% (.970), ~1.4% (.985), and ~4%
(.995). Alpha was also fixed to follow luma rather than accidentally receiving
chroma's multiplier.
