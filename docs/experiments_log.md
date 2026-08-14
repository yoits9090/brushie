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

### Compact v3 directory (SHIPPED)
Whole-band v2 still spent 40 bytes/chunk on x/y=0, explicit cumulative
offsets, derivable counts, and a 16-bit fixed mode. v3 uses 20 bytes/chunk:
layer/band/channel/mode, width/height/step, payload bytes, checksum. Offsets
and counts are derived; payloads stay contiguous; checksums remain. Decoder
supports v1/v2 and has a retained v2 fixture with its historical entropy
context layout.

At the previous tuned matched points, exact v3 means (same reconstruction)
are 10,325 / 18,342 / 38,174 bytes at .970/.985/.995, another 4.1%/2.5%/1.4%
reduction. UI/meme individual files fall 12-16% because directory overhead
was 31-34% in v2.

## Coder-lab session (lab/coder)

Measurement discipline: quick harness (2 Kodak + chat + meme, 512px,
windowed MS-SSIM `brushie-box11-ms-ssim-v1`) for gate decisions; 12-16
broad-corpus fixed-q byte sums for fast iteration (identical quantization =>
identical pixels => bytes comparable at fixed q). Baseline: CAPS
9,120 / 16,162 / 35,380 (frozen v5), reproduced locally (9,120/16,162/35,381).

### Shipped: v6 unary adaptation rate 1/16 (was 1/32)
The per-pass entropy audit showed the unary quotient contexts carry ~0
conditional structure but the adaptive model pays ~8% lag over the static
optimum (model 24,089 vs ctx-entropy 22,286 bits at q50 on kodak01).
BRUSHIE_UNARY_RATE knob A/B (fixed-q, 16 corpus images):
rate 1/16: -0.83%/-0.68%/-0.44% at q30/50/75; rate 1/8: -0.85/-0.60/-0.15;
rate 1/64: +1.5-2.6%. Gate-matched quick harness (1/16):
**9,031 / 16,066 / 35,265 vs baseline 9,120 / 16,162 / 35,380
= -0.98% / -0.59% / -0.33%** with 4/4 coverage everywhere. Shipped as stream
v6 (default 1/16, BRUSHIE_UNARY_RATE=3..8 overrides); v5 and older streams
keep 1/32 and decode unchanged. Verified: v5 stream decodes byte-identical
with the v6 binary; 1500-iteration fuzz on v6 streams clean.

### Rejected: mode 13 parent-block magnitude contexts (sig+unary by parent
2x2 block max class, Rice stays local)
Fixed-q 16-image sums: +2.75%..+3.05% vs mode 8 at q30/50/75. Per-pass audit
(kodak01 q50): sig contexts +261 B (32-class dilution), unary -448 B, rem
+2,835 B when parent mean fed the Rice parameter (fixed to local Rice after
the first audit); net still positive. Kept behind BRUSHIE_ENTROPY=13.

### Rejected: mode 14 parent-block value prediction (zigzag residual vs
parent 2x2 block mean + local residual Rice)
Fixed-q 16-image sums: +13.8%..+16.2%. Parent block mean in child-quantized
units correlates weakly with child magnitude (mean m=2.16 vs mean|res|=
5.39 with p/1 predictor, 2.73 with p/2, 1.79 with p/4 on kodak01 q30-75;
gradient-0 blocks mean m 1.7 vs gradient-8+ m 3.5-4.4), and the residual
Rice scale inflates rem bits. Consistent with geom-lab's independent
negative on inter-band value prediction. Kept behind BRUSHIE_ENTROPY=14.

### Rejected: mode 15 AUREA-style energy-bucket significance contexts
(|L|+|A|+|AL| linear buckets x parent gate; BRUSHIE_EBUCKETS=8/16/32)
Fixed-q 16-image sums: +0.64%..+4.39% at every bucket count; the sum
collapses the spatial pattern that the 3 binary neighbour flags encode.
Kept behind BRUSHIE_ENTROPY=15.

### Analysis shipped: per-pass entropy audit (scripts/entropy_audit.py)
Extended to walk modes 13/14/15 and v6 shift rules, with per-pass
(model/context/zero-order) entropy columns, plus BRUSHIE_AUDIT_STATS
JSONL hook (parent block mean/class/gradients per significant coefficient).
Findings on kodak01 detail bands: sig is the only pass with real context
structure (ctx-entropy 99,859 vs zero-order 124,099 bits at q50, adaptive
model tracks it within 0.3%); unary/rem/sign contexts carry ~nothing; the
only modeling slack was the unary adaptation lag (now shipped as v6).
## geom-lab session (lab/geom branch, CAPS campaign)

Baseline (lab_0, committed): quick harness 9,120 / 16,162 / 35,380 — exact
match vs frozen v5 baseline. All experiments below measured with
`python3 scripts/enterprise_eval.py --quick` + per-image audits via
scripts/byte_audit.py and a single-image iter harness (bytes + windowed
MS-SSIM at the exact harness quality/base settings).

### Shipped: per-chunk adaptive base mode (mode 16 flat-block, trial-selected)
Base-LL chunks now trial-encode median-predict (mode 3) vs a new 4x4
flat-block mode 16 (flags + flat values predicted from left/above blocks +
median residuals; contexts 0..3/4..7/57..60, rest shared layout) and keep
the smaller payload; the mode byte carries the choice (deterministic,
decoder-side cost zero). Both modes reconstruct bit-identical quantized
bands, so this is pure byte selection.
Quick harness (lab_1_base16): 9,116 / 16,143 / 35,352 vs 9,120 / 16,162 /
35,380 (-0.04% / -0.12% / -0.08%). Per image at gate qualities:
chat .985 3,593 -> 3,519 (-2.1%), chat .995 7,128 -> 7,015 (-1.6%),
chat .970 2,429 -> 2,424, meme .970 1,874 -> 1,871, kodak02 .970
16,870 -> 16,864, kodak01 unchanged. Mode 16 wins when the quantized base
has flat 4x4 runs at moderate step sizes (text UI at high quality); mode 3
stays for noisy/photo bases.
Tests + 300-iteration fuzz pass; old v1-v5 streams still decode.

### Rejected: plane predictor base modes (13/14, BRUSHIE_BASE)
p = a+b-c unclamped/clamped for the base LL. Meme base chroma (gradient
sinusoid) barely moved (491 -> 494B); chat/photos neutral-to-worse. Residual
entropy analysis on dumped quantized bases: median 2.2 vs plane 2.0 b/coef
on meme luma (still loses after coder overhead); on the chat base median is
already best (1.36 b/coef). Directional continuation predictors (2A-AA,
2B-BB, planar switch) beat median only on chat chroma by ~0.15 b/coef, not
enough to survive the mode overhead. Kept behind BRUSHIE_BASE for sweeps.

### Rejected: base-band RLE (measured in Python, never coded)
Zero-run RLE on median residuals costs ~269B vs mode-3's 179B on chat base
luma (134 nonzero values in 704 coeffs at 19% nz) and loses on the meme
base too. The adaptive arithmetic sig bits are already cheaper than
varint runs at these densities.

### Rejected: 4x4/8x8 block polynomial base fitting (measured in Python)
LSQ (mean, dx, dy) per block on the quantized base: residual entropy
3.58/4.83 b/coef (4x4/8x8) vs median-predict 2.2 on meme luma; bar edges +
sinusoid make block fits overshoot. Not coded.

### Analysis: inter-band value prediction on detail bands (chat, q38)
Dumped all bands; tested parent-value and parent-gradient (x/y) predictions
in child-quantized units. All lose on the sparse text detail bands:
L1-H c0 nz 8% ent 0.60 b/coef -> parent_val nz 100% ent 2.06; parent_gx
1.63, parent_gy 1.55. L2/L3 bands similar. The chat detail bands are
already sparse (4-23% nz) and the parent-significance contexts are doing
the work; predicting VALUES from the parent re-injects the parent DC into
every coefficient. Conclusion: the chat/meme gap vs AVIF is representational
(wavelet vs block prediction), not base- or parent-prediction overhead.

### Shipped: merged band-4 chunks with absent sections (empty D/V)
H/V detail pairs now merge even when the D band is inactive: a mode byte of
0 marks an absent section (band left zero at decode; H always present).
Saves one 16B directory entry + one k0/block-flag header per empty section.
Quick harness (lab_2_merge): 9,110 / 16,139 / 35,350 vs lab_1 9,116 /
16,143 / 35,352. Per image at gates: meme .970 1,871 -> 1,859 (-12),
kodak01 15,306 -> 15,297, chat 2,424 -> 2,421, kodak02 16,864 -> 16,861.
Bit-identical reconstruction. Old v5 streams (all sections present) decode
unchanged; validation tightened for absent markers. Fuzz clean.

### Rejected: grain synthesis (M6) — zero-noise-blocks + decoder noise
BRUSHIE_GRAIN_ZERO probe zeroes noise-like 8x8 blocks (max|q|<=1-2,
significance-map lag-1 autocorr < 0.15-0.20, mean~0) in the finest luma
detail level before coding. kodak01 q40: -941B (-6.1%) for ms_ssim 0.97023
-> 0.96738; kodak02 q76: -925B for 0.97003 -> 0.96848. Decoder-side
synthetic noise (pixel-space probe, sigma 3/5/7, deterministic PRNG) makes
ALL THREE metrics worse, monotonically in sigma (kodak01: ms_ssim
0.96738->0.96550->0.96231; SSIMULACRA2 29.1->28.4->26.8; Butteraugli
2.990->3.016->3.107). Mechanism: independent synthetic noise adds
reconstruction variance without adding covariance, so windowed SSIM/S2
drop, and Butteraugli 3-norm punishes the noise directly. The zeroed
blocks were also not pure noise (|q|<=2 texture carries real metric
weight). Verdict: byte/quality slope of grain-zeroing ~= slope of coding
the coefficients (-0.003 ms_ssim/KB); synthesis only loses. Retest only
behind a noise-rewarding perceptual metric (LPIPS/blinded humans).
Probe kept behind BRUSHIE_GRAIN_ZERO for that retest.

### Rejected: chroma-from-luma (M5, CCLM-style)
Measured collocated luma 2x2-block vs chroma detail on dumped bands
(chat q38, kodak02 q76): fitted alpha ~ 0, corr in [-0.19, +0.16] on
kodak02 and ~0 on chat. Chroma detail at these levels carries smooth
color variation luma does not predict; where chroma fires (color edges)
the coefficients are already sparse/cheap. Not implemented.

### Rejected: block-DCT of the base band (measured in Python)
4x4/8x8 orthonormal DCT on the quantized base (chat + meme): total
entropy-equivalent 1.90-3.31 b/coef vs median-predict 0.64-2.16 b/coef.
Block boundaries + coarse sinusoid beat any AC-concentration gain.
Not implemented.

### Rejected: levelmul reallocation for chat (BRUSHIE_LEVELMUL sweep)
l1:0.7 etc. move along the same RD curve (2524B @ 0.97260 vs default q40
2490B @ 0.97208; l5:1.4,l4:1.2 = identical to default). The empirical
step table remains a local optimum (consistent with the v4 table-sweep
rejections).

### Rejected: BRUSHIE_BLOCK 8/32 for mode-12 detail bands
32 beats 16 by 14B on chat / 5B on meme at the gate, 8 loses. Not shipped
because the block size is env-derived on both sides (not stream-safe);
would need mode bytes 17/18 for a <1% win — not worth the format surface.

### Shipped: v7 rANS backend (BRUSHIE_RANS=1, stream version 7)
Binary rANS (M=4096 prob scale, L=2^16 renorm, 12-bit adaptive
probabilities) replaces the binary range coder for v7 streams; the encoder
runs a model pass that records (prob, bit) and a reverse rANS pass, the
decoder adapts live in raster order (contexts unchanged). v5/v6 streams
decode with the old core unchanged. Measured: kodak01 512px q30/50/75:
-3.3%/-2.8%/-2.2%; fixed-q 16-image corpus sums q30/50/75/90:
-6.1%/-5.5%/-4.4%/-3.3%; synthetic chat q25/40/55/70: -14.6%/-14.6%/-13.8%/
-12.8% (tiny chunks win the most: 4B termination vs range-coder cache
chains + exact probabilities). 1500-iteration fuzz clean; modes 13/14/15/17
roundtrip in v7; unit tests pass. Pixels identical at every q.
