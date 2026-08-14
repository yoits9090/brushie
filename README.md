# Brushie CAPS

Brushie CAPS (Compact Adaptive Pyramid Streams) is a deterministic CPU-only
image codec. It uses a reversible YCoCg-R transform, a 5/3 lifting pyramid,
midtread scalar quantization with frequency-ordered steps, and whole-band
context-adaptive binary arithmetic coding. The current research stream uses a
compact 16-byte whole-band directory and a v7 rANS backend by default (older
v1-v6 streams still decode). The stream format is experimental and may change. Detail bands use per-coefficient local Rice
parameters, an automatic per-band 16x16 block-significance mode for sparse
bands, and each level's H/V/D triple is merged into a single band-4 chunk.
Bands are ordered coarse-to-fine for progressive decoding, and chroma is
4:2:0 subsampled at lossy operating points.

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

The earlier v4/v5 recursion (see [docs/recursion.md](docs/recursion.md)) plus
broad-corpus work measured CAPS at **9,120 / 16,162 / 35,380** mean bytes on
the original quick eval, and **11,527 / 21,498 / 54,831** on a 163-image
public-benchmark corpus (Kodak 24 + DIV2K 100 + USC-SIPI 39, same windowed
MS-SSIM gates, per-image adaptive quant profiles). These are historical
MS-SSIM-proxy results, not current v7 real-metric product claims: v4 sign/significance
context separation, per-coefficient local Rice parameters, a
metric-calibrated base multiplier, per-band 16x16 block-significance modes,
a 16-byte compact directory, merged H/V/D band-4 chunks, and a harness-side
per-image profile search measured with the real metric. CAPS beats JPEG and
JPEG 2000 at every gate with FULL coverage on the broad corpus (AVIF/WebP
lose coverage at .995: 153/163 and 144/163); AVIF remains 1.23x/1.15x/1.26x
smaller and WebP 1.03x/1.06x/1.27x. Wall-clock (Colab CPU node, same VM):
CAPS encode 18ms/decode 17ms vs AVIF 217/12, JPEG2000 57/12, WebP 42/4,
JPEG 4/3. See `benchmarks/`, `dash/broad.json`, and
`harness_v5_final_quick_report.md`. SSIMULACRA2/Butteraugli and blinded
humans remain required before product claims.

## Research status

The repository contains the research logs and benchmark artifacts behind the codec. They are useful for reproduction, but they are not a product benchmark or a universal ranking: the primary broad-corpus loop uses a local-window MS-SSIM proxy, while the checked-in SSIMULACRA2/Butteraugli landscape shows CAPS trailing JPEG XL, WebP, and AVIF on comparable real-metric gates. See `enterprise_readiness.md` and `campaign/landscape_real_metrics.md` for the limitations and exact caveats.

## Open-source status

Brushie is an experimental research codec, not a standards-compliant or production-ready replacement for JPEG XL, AVIF, WebP, or PNG. The benchmark corpus and historical experiment artifacts are included for reproducibility where their upstream terms permit; see `DATASETS.md` and verify dataset terms before redistributing them.

The current implementation is intentionally honest about its limits: real-metric results are corpus-dependent, the headline numbers include per-image search in some tables, LPIPS control is unverified, and blinded human testing, sanitizer coverage, color/metadata handling, and portability validation are still incomplete. Treat the stream format and API as unstable until a versioned release is announced.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Where CMake is not installed, the equivalent build is:

```sh
mkdir -p build
clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
# PNG I/O (optional): add -DBRUSHIE_HAVE_PNG and link libpng
```

## Tests

The canonical C++ test is built by CMake and run with CTest:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Optional Python metric tests require NumPy (and Pillow for the broader benchmark scripts):

```sh
python3 tests/test_quality_metrics.py
python3 tests/test_enterprise_eval_helpers.py
```

## Use

The CLI reads and writes PPM, and PNG (RGB or RGBA) when built with libpng:

```sh
build/brushie encode input.ppm output.brbr 45 8 64
build/brushie decode output.brbr output.ppm 4096 4096 -1
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

`--target-lpips` is only a heuristic quality preset; no LPIPS model is
linked or evaluated by the deterministic CPU core, and the CLI labels the
result `target_lpips_unverified`. Do not treat it as an LPIPS target.

PNG support is optional and is enabled only when CMake finds libpng. A build without libpng is PPM-only; the no-CMake command below intentionally builds PPM-only. Input PNG metadata and 16-bit samples are not preserved by the optional CLI path.

For the Python metric tests, install Python 3 with NumPy (and Pillow for the broader evaluation scripts). The codec library and C++ round-trip test do not require Python.

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
