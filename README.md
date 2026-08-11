# Brushie CAPS

Brushie CAPS (Compact Adaptive Pyramid Streams) is a deterministic CPU-only
image codec. It uses a reversible YCoCg-R transform, a 5/3 lifting pyramid,
midtread scalar quantization with frequency-ordered steps, and whole-band
context-adaptive binary arithmetic coding. The current v5 stream uses a
compact 16-byte whole-band directory (v3/v4 20-byte and v1/v2 40-byte
streams still decode). Detail bands use per-coefficient local Rice
parameters plus an automatic per-band 16x16 block-significance mode for
sparse bands. Bands are ordered coarse-to-fine for progressive decoding,
and chroma is 4:2:0 subsampled at lossy operating points.

Design notes and the competitive audit are in [literature_review.md](literature_review.md)
and [design_candidates.md](design_candidates.md); the format is specified in
[format_spec.md](format_spec.md).

## Competitive status (audited harness v3)

The old global-moment scoreboard is retired. Harness v3 uses local-window
multiscale SSIM, exhaustive legal quality sweeps, strong format modes,
verified still-image AVIF, adaptive JPEG 2000 search, full-coverage-only
aggregates, clean manifests, and actual truncated progressive streams.
Clean exhaustive quick result (2 Kodak photos + chat + meme, 3,323 encodes,
all codecs 4/4 coverage):

| Windowed MS-SSIM gate | AVIF | WebP | JPEG | JPEG 2000 | CAPS |
|---:|---:|---:|---:|---:|---:|
| .970 | 6,466 | 7,658 | 10,140 | 10,046 | 10,325 |
| .985 | 12,948 | 13,901 | 16,328 | 19,084 | 18,316 |
| .995 | 31,810 | 65,498 | 32,427 | 44,377 | 38,174 |

The v4/v5 recursion (see [docs/recursion.md](docs/recursion.md)) cuts CAPS to
**9,516 / 16,311 / 35,534** mean bytes at the same gates (clean exhaustive
quick eval, 3,323 candidates, same metric and corpus) via: v4
sign/significance context separation, per-coefficient local Rice
parameters, a metric-calibrated base multiplier, per-band 16x16
block-significance modes for sparse bands, a 16-byte v5 directory, and
Catmull-Rom chroma upsampling. CAPS now beats JPEG and JPEG 2000 at every
gate and WebP at .995; AVIF remains 1.47x/.26x/.12x smaller. See
`harness_v4_final_quick_report.md` and its manifest (metric
`brushie-box11-ms-ssim-v1`). SSIMULACRA2/Butteraugli and blinded humans
remain required before product claims; cross-codec CPU timing is still
diagnostic.

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
