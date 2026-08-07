# Competitive landscape: image codecs, 2024-2026

Research log for the Brushie "beat every format" campaign. Compiled from
Cloudinary's libjxl Pareto-front analysis, arXiv, GitHub codec repos, and
Hacker News. Dates: research run on this session.

## The field (lossy, photographic content)

| Format | Encoder | Typical density | Encode speed | Notes |
|---|---|---|---|---|
| JPEG | libjpeg-turbo | baseline | very fast | sequential decode unbeatable |
| JPEG | mozjpeg | better than WebP at med quality | slow | Huffman-optimized |
| JPEG | **jpegli** (libjxl) | **beats WebP and high-speed AVIF** | fast | JPEG-compatible, from libjxl; key benchmark to beat |
| WebP | libwebp 1.3.2 | baseline+ | fast | 4:2:0 obligatory; cannot reach visually-lossless |
| AVIF | libavif/libaom | ~1.5-2x JPEG | **slow** (0.5-5 Mpx/s) | not on the Pareto front at high quality; tiled MT trades density for speed |
| HEIC | libheif/x265 | ~AVIF | slow | similar block pipeline |
| **JPEG XL** | **libjxl 0.10** | **best overall** | fast (52+ Mpx/s) | Pareto-optimal across speeds; ~20% smaller than AVIF at visually-lossless, 2.5x faster |
| QOI | — | 17 bpp | 154 Mpx/s | reference for speed, not density |

## What the metrics say

- The "safe" perceptual metrics are **SSIMULACRA2, Butteraugli, DSSIM**
  (best correlation with human opinion). PSNR/SSIM correlate poorly and
  codecs often over-fit them. Brushie's current gate is the repo's
  deterministic 4-scale MS-SSIM proxy; SSIMULACRA2/Butteraugli and blinded
  human tests are the required next validation step.
- Web-relevant quality: SSIMULACRA2 60 (medium, ~0.6 bpp) to 90 (visually
  lossless, ~3 bpp). HTTP Archive medians: AVIF on the web ~1 bpp, JPEG
  ~2.1 bpp. Brushie's .970/.985/.995 MS-SSIM gates sit inside this range.
- Aligning on *average* metric scores favors WebP/AVIF (they are the more
  inconsistent encoders); aligning on *worst-case* favors consistent codecs
  like JPEG XL. A fair vendor pitch should use worst-case alignment.

## Independent / "vibecoder" codecs found on GitHub (Feb 2026 sweep)

These are the repos that claim to beat an incumbent, plus what they actually
achieve. None beats AVIF or JPEG XL outright.

| Repo | Claim | Verdict |
|---|---|---|
| [AUREA](https://github.com/5ymph0en1x/Aurea) (Rust) | "beats JPEG" | -5.9% BD-Rate vs JPEG on Kodak 24 (22/24), keeping 4:4:4. Techniques: Golden-Ratio color transform, variable-size lapped DCT (8/16/32), **zero-bit Turing morphogenesis saliency field** (edge-preserving, activity-modulated quantization, both sides derive it from the DC grid), psychovisual pivot, gradient-aligned QMAT, rANS + Exp-Golomb, 128 contexts, PTF gamma 0.65. **Best source of transferable ideas.** |
| [WEBSL](https://github.com/iams4m/WEBSL) | "beats WebP ~70% of the time" | Grain-grid + predictor + zlib, then **re-encodes through WebP**. It "beats" WebP by pre-smoothing/quantizing before WebP. Not a standalone codec; the grain-support idea matters for smooth-region coding. |
| [2dc](https://github.com/Fukahire1010/2dc) | lossless: -25% vs PNG, -5% vs WebP | Lossless only; not relevant to the lossy race. |
| [gonbe](https://github.com/minasfostieris-ui/gonbe) | from-scratch rANS + MED | Early-stage lossless+lossy. |
| [ovm-codec](https://github.com/matelabdev/ovm-codec) | from-scratch Rust lossy | Early-stage. |

Conclusion: the premise "some random repo is doing much better than the big
codecs" does not hold up. Mature codecs (JPEG XL, AVIF, jpegli) are the real
competition; the independent repos are a source of *techniques* (activity
quantization, lapped transforms, grain, context counts), not a shortcut.

## Where Brushie stands (measured, MS-SSIM-proxy gates, 8-sample corpus)

| Profile | Gate | CAPS v2 | WebP | AVIF | JPEG |
|---|---|---|---|---:|---:|
| chat 512px | .970 | 8,848 | 11,906 | 5,290 | 15,485 |
| chat 512px | .985 | 12,203 | 12,586 | 9,067 | 16,772 |
| chat 512px | .995 | 23,508 | 19,668 | 18,564 | 32,803 |
| expanded | .970 | 17,286 | 25,108 | 11,454 | 35,501 |
| expanded | .985 | 21,969 | 25,808 | 18,340 | 36,463 |
| expanded | .995 | 39,231 | 38,719 | 35,212 | 56,314 |

vs JPEG: wins every gate (1.4-2x). vs WebP: wins .970/.985, ~ties .995.
vs AVIF: 1.1-1.7x behind. Encode: ~10-15x cheaper than AVIF (5-14 ms vs
40-90 ms for the same images). This is "better than JPEG/WebP on cost and
bytes at low/mid quality, and much cheaper than AVIF" — a real, defensible
position, but NOT yet "beats every format".

## The gap to AVIF/JPEG XL, decomposed

AV1/JPEG-XL win with tools Brushie lacks:

1. **Block-based directional intra prediction** (AV1): smooth gradients and
   text/UI content get predicted instead of coded. Brushie's worst gap is
   chat/text at .970 (3.8x behind AVIF). This is the single biggest
   structural difference.
2. **Many more entropy contexts + bitplane refinement** (both): our 8+4+14
   contexts are far below AV1's hundreds; EBCOT-style refinement passes help
   the high-quality gate (our .995 is only 1.26x behind AVIF there already).
3. **Rate-distortion optimization** (AV1/libjxl trellis): per-block mode
   selection. We do fixed analytical allocation.
4. **Lapped/overlapped transforms + adaptive block sizes** (AUREA, JPEG XL):
   fewer ringing/blocking artifacts per bit.
5. **Activity/saliency-modulated quantization** (AUREA Turing field,
   JPEG XL's adaptive quantization): zero-bit edge protection. Directly
   applicable to Brushie's wavelet bands.
6. **Grain synthesis** (JPEG XL, WEBSL): model noise as parameters instead
   of coefficients.

## Candidate next levers for Brushie (ranked by expected ROI)

1. **Activity-aware quantization** (AUREA Turing idea, adapted to wavelet
   bands; zero-bit, both sides derive it from the decoded LL). Targets the
   .970/.985 photo gates. Low format risk: modulation is implicit.
2. **More entropy contexts** (energy-bucket significance contexts like
   AUREA's 128) and **bitplane refinement** for the high-quality gate.
3. **Chroma-from-luma prediction** for the residual chroma bands.
4. **Deeper/adaptive pyramid + lapped synthesis** to reduce ringing.
5. **Long-term**: directional prediction for text/UI, then jpegli/JPEG-XL
   class RDO. Realistic horizon: months of work, not one session.

Benchmarks to beat next: jpegli (beats WebP) at .970-.995, then AVIF, then
libjxl default-effort. Validation must move from the MS-SSIM proxy to
SSIMULACRA2/Butteraugli + blinded human tests before any customer claim.

## Brushie vs JPEG 2000 (measured this session)

Added ffmpeg/openjpeg JPEG 2000 to the eval harness (scripts/enterprise_eval.py).
Per-sample at the .970 gate (512px preview, quick profile):

| Sample | CAPS | JPEG2000 (openjpeg) | AVIF |
|---|---:|---:|---:|
| photo_01 | 11,887 | 26,126 | 8,131 |
| photo_02 | 7,924 | 8,412 | 5,804 |
| chat_ui | 4,785 | 4,091 | 1,345 |
| meme_card | 3,572 | 2,850 | 1,098 |

Brushie beats openjpeg's JPEG 2000 by 1.1-2.2x on photographs, while openjpeg
wins by 15-20% on smooth synthetic content (chat/meme). This localizes the
remaining work precisely: Brushie's low-frequency/smooth-region coding
(base LL + coarse levels) is ~15-20% behind a mature bitplane coder there,
while its photo coding is already ahead. The text/UI gap to AVIF (3.8x) is
unchanged and remains the biggest structural target (roadmap M7).
