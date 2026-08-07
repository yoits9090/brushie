# Brushie CAPS

Brushie CAPS (Compact Adaptive Pyramid Streams) is a deterministic CPU-only
image codec. It uses a reversible YCoCg-R transform, a 5/3 lifting pyramid,
midtread scalar quantization with frequency-ordered steps, and whole-band
context-adaptive binary arithmetic coding. Bands are ordered coarse-to-fine
for progressive decoding, and chroma is 4:2:0 subsampled at lossy operating
points.

Design notes and the competitive audit are in [literature_review.md](literature_review.md)
and [design_candidates.md](design_candidates.md); the format is specified in
[format_spec.md](format_spec.md).

## Competitive status (512px chat-preview profile, equal MS-SSIM gates)

Mean bytes at each gate on the repository's deterministic four-scale MS-SSIM
proxy (6 photographs + synthetic chat + synthetic meme):

| Gate | CAPS v2 | WebP | AVIF | JPEG |
|---|---:|---:|---:|---:|
| .970 | **7,039** | 8,739 | 4,095 | 12,396 |
| .985 | 12,000 | 10,099 | 8,568 | 14,971 |
| .995 | 23,327 | 19,704 | 18,560 | 36,428 |

CAPS beats JPEG at every gate, beats WebP at the .970 gate, and trails WebP
by ~1.2x and AVIF by ~1.3-1.7x at higher gates, while encoding about 10x
cheaper than AVIF (see [enterprise_readiness.md](enterprise_readiness.md)).
LPIPS and blinded human preference tests are still required before making
customer-facing perceptual claims; MS-SSIM values use the repository's
deterministic proxy.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Where CMake is not installed, the equivalent build is:

```sh
mkdir -p build
clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
```

## Use

```sh
build/brushie encode input.ppm output.caps 45 8 64
build/brushie decode output.caps output.ppm 4096 4096 -1
```

`decode ... 0` emits the coarse LL layer only. `-1` emits every available
layer. The decoder may request any output width and height; it reconstructs
the needed pyramid prefix and bilinearly resamples.

Quality 1..100; 100 is exactly lossless (5/3 + YCoCg-R are reversible, chroma
is kept 4:4:4). Lower qualities use 4:2:0 chroma and progressively coarser
steps. One-pass analytical rate hints (the actual byte count is reported):

```sh
build/brushie encode input.ppm output.caps --target-bytes 250000 --threads 8
build/brushie encode input.ppm output.caps --target-lpips 0.06 --threads 8
```

`--target-lpips` exposes the requested control surface but is marked
`target_lpips_unverified` because LPIPS is not linked into the deterministic
CPU core.

## Enterprise evaluation

The enterprise harness compares codecs at equal MS-SSIM-proxy gates instead
of comparing unrelated quality settings. It routes photo and synthetic chat/UI
content through a 512-pixel chat-preview profile and, unless `--quick` is
used, a 1536-pixel expanded-image profile.

```sh
python3 scripts/enterprise_eval.py --quick
python3 scripts/enterprise_eval.py --output-prefix enterprise_eval
```

The harness writes candidate, matched-quality, aggregate, progressive, and
preview CSVs plus a Markdown decision report. The current commercial
go/no-go assessment is in [enterprise_readiness.md](enterprise_readiness.md).
