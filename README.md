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

## Competitive status (audited harness v3)

The old global-moment scoreboard is retired. Harness v3 uses local-window
multiscale SSIM, exhaustive legal quality sweeps, strong format modes,
verified still-image AVIF, adaptive JPEG 2000 search, full-coverage-only
aggregates, clean manifests, and actual truncated progressive streams.
Clean exhaustive quick result (2 Kodak photos + chat + meme, 2,923 encodes,
all codecs 4/4 coverage):

| Windowed MS-SSIM gate | AVIF | WebP | JPEG | JPEG 2000 | CAPS |
|---:|---:|---:|---:|---:|---:|
| .970 | 6,466 | 7,658 | 10,140 | 10,046 | 10,770 |
| .985 | 12,948 | 13,901 | 16,328 | 19,084 | 18,817 |
| .995 | 31,810 | 65,498 | 32,427 | 44,377 | 38,709 |

Post-audit tuning (quality-adaptive high-tier allocation + 32/64 base-mode
competition + corrected diagonal/chroma weights) cuts CAPS 5.1%/.970,
2.8%/.985, and 50.3%/.995. CAPS now beats JPEG 2000 at .985/.995 and WebP at
.995, while remaining 6-7% behind JPEG/J2K at .970 and 19-22% behind
JPEG/AVIF at .995. See [docs/harness_v3.md](docs/harness_v3.md) and clean
`harness_v3_tuned_quick_manifest.json` (SHA `012f74a`, metric
`brushie-box11-ms-ssim-v1`). SSIMULACRA2/Butteraugli and blinded humans remain
required before product claims; cross-codec CPU timing is still diagnostic.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Where CMake is not installed, the equivalent build is:

```sh
mkdir -p build
clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
# PNG I/O (optional): add -DBRUSHIE_HAVE_PNG and link libpng
```

## Use

The CLI reads and writes PPM, and PNG (RGB or RGBA) when built with libpng:

```sh
build/brushie encode input.png output.brbr 45 8 64
build/brushie decode output.brbr output.png 4096 4096 -1
```

RGBA PNGs round-trip losslessly at quality 100 (alpha is coded at full
resolution); at lossy qualities the alpha channel keeps hard edges.

`decode ... 0` emits the coarse LL layer only. `-1` emits every available
layer. The decoder may request any output width and height; it reconstructs
the needed pyramid prefix and bilinearly resamples.

Quality 1..100; 100 is exactly lossless (5/3 + YCoCg-R are reversible, chroma
is kept 4:4:4). Lower qualities use 4:2:0 chroma and progressively coarser
steps. One-pass analytical rate hints (the actual byte count is reported):

```sh
build/brushie encode input.ppm output.brbr --target-bytes 250000 --threads 8
build/brushie encode input.ppm output.brbr --target-lpips 0.06 --threads 8
```

`--target-lpips` exposes the requested control surface but is marked
`target_lpips_unverified` because LPIPS is not linked into the deterministic
CPU core.

## Enterprise evaluation

Harness v3 compares codecs at equal **local-window multiscale SSIM** gates
instead of unrelated quality settings or the retired global-moment proxy. It
uses exhaustive integer quality sweeps, strong JPEG/WebP modes, still-image
AVIF, honest full-coverage aggregation, reproducibility manifests, and actual
truncated progressive streams. The full run includes Kodak plus available
DIV2K validation sources and a `native_expanded` max-1536 profile (sources are
not upscaled).

```sh
python3 scripts/enterprise_eval.py --quick
python3 scripts/enterprise_eval.py --output-prefix harness_v3_full
```

The harness writes candidate, matched-quality, aggregate, progressive, and
preview CSVs plus a Markdown decision report. The current commercial
go/no-go assessment is in [enterprise_readiness.md](enterprise_readiness.md).
