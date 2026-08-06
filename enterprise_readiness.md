# Brushie enterprise readiness

## Proposed product

Brushie is best evaluated as an on-premise codec SDK for platforms that own
their media pipeline and clients. The enterprise claim to test is:

> Reduce delivered image bytes at the same human-rated quality without
> increasing total infrastructure cost or user-visible latency.

This is an SDK/source-license product, not initially a hosted SaaS. A Discord-
scale customer would need to run the encoder inside its own media proxy and
deploy trusted decoders across native, desktop, and web clients.

## Current equal-quality result

`scripts/enterprise_eval.py` swept 592 candidates across six photographs, two
deterministic chat/UI images, a 512-pixel preview profile, and a 1536-pixel
expanded-image profile. It selected the smallest candidate meeting each
MS-SSIM-proxy gate rather than comparing unrelated codec quality settings.

| Profile | MS-SSIM gate | CAPS mean bytes | Best full-coverage standard mean bytes | CAPS / best per-sample standard | CAPS wins |
|---|---:|---:|---:|---:|---:|
| Chat preview | .970 | 53,642 | AVIF 5,290 | 12.58x | 0/8 |
| Chat preview | .985 | 62,138 | AVIF 9,067 | 10.57x | 0/8 |
| Chat preview | .995 | 106,385 | WebP 19,668 | 7.47x | 0/8 |
| Expanded | .970 | 109,348 | AVIF 11,454 | 11.00x | 0/8 |
| Expanded | .985 | 124,861 | AVIF 18,340 | 10.18x | 0/8 |
| Expanded | .995 | 172,973 | AVIF 35,212 | 7.05x | 0/8 |

The per-sample ratio uses the smallest available standard candidate for each
image, so it may use AVIF, WebP, or JPEG. AVIF reached only 7/8 samples at the
.995 preview gate; the table uses WebP as the smallest standard codec with full
coverage at that aggregate gate.

CAPS encoding is directionally faster than the WebP and AVIF paths in this
harness, but the timing scopes are not equivalent: CAPS reports its resident
codec call, Pillow reports wall time, and ffmpeg includes process and file-I/O
overhead. No production CPU-cost claim should be made from these values.

## Implemented improvement

The encoder previously emitted a directory entry and terminal zero-run payload
for every all-zero coefficient tile. The decoder already initializes omitted
coefficients to zero, so the encoder now omits those tiles.

On the four-image quick enterprise set, with reconstruction unchanged:

- equal-quality full-file size fell by 2.76% at the .970 gate;
- equal-quality full-file size fell by 2.11% at the .985 gate;
- equal-quality full-file size fell by 0.72% at the .995 gate;
- UI/text progressive-preview prefixes fell by 14.35% to 15.65%.

This is a worthwhile format-compatible optimization, but it confirms that
directory overhead is not the primary full-image bottleneck.

## Progressive result

At CAPS q=82 with 64-pixel tiles:

| Profile | Preview gate | Mean prefix bytes | Mean fraction of CAPS file |
|---|---:|---:|---:|
| Chat preview | .950 | 28,390 | 24.4% |
| Chat preview | .970 | 34,032 | 28.1% |
| Expanded | .950 | 46,805 | 21.8% |
| Expanded | .970 | 82,185 | 37.1% |

The progressive path is measurable, but its preview prefix is often larger
than an entire WebP or AVIF at the same proxy-quality gate. It is not yet an
enterprise bandwidth advantage.

## Go/no-go gate

Do not approach a Discord-scale buyer with an infrastructure-savings claim
until all of the following are true on a representative private corpus:

1. At least 20% fewer bytes than the buyer's current WebP/AVIF pipeline at the
   same blinded human preference level.
2. No increase in combined encode, decode, and CDN cost that erases the byte
   savings.
3. A safe incremental rollout with standard-format fallback.
4. Production support for alpha, color profiles, metadata, malformed-input
   hardening, fuzzing, Linux x86/ARM, mobile, and WebAssembly.
5. A clean IP and dependency record plus explicit commercial licensing.

The current result is a no-go for an infrastructure-savings sales pitch.

## Next engineering target

The remaining 7x--12x gap cannot be closed by directory compaction alone. The
next codec experiment should combine:

- lower-resolution chroma at lossy operating points;
- context-adaptive entropy coding across coefficient neighborhoods;
- perceptual, activity-aware quantization rather than one step per
  channel/level; and
- equal-quality measurement after every intervention.

If a bounded experiment cannot reduce the gap by at least 30% without harming
speed or progressive behavior, the commercial project should pivot from a new
image format to an optimization/encoding pipeline built around established
formats.

## Reproduction

```sh
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic \
  -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic \
  -Iinclude src/codec.cpp tests/test_codec.cpp -o build/test_codec -pthread
./build/test_codec
python3 scripts/enterprise_eval.py --output-prefix enterprise_eval_full
```

The primary outputs are `enterprise_eval_full_candidates.csv`,
`enterprise_eval_full_matched.csv`, `enterprise_eval_full_aggregate.csv`,
`enterprise_eval_full_progressive.csv`, and
`enterprise_eval_full_report.md`.
