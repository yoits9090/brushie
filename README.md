# Brushie CAPS

Brushie CAPS (Compact Adaptive Pyramid Streams) is a deterministic CPU-only prototype for the 4096x4096 / 2.5 s research target. It uses a reversible YCoCg transform, an analytical 5/3 lifting pyramid, dead-zone scalar quantization, and independent local coefficient tiles. Every tile deterministically competes absolute varints, delta varints, and a significance-mask/bit-packed signed mode; lossy points also allocate a bounded extra step to chroma. Tiles are ordered coarse-to-fine for progressive decoding.

This is a research baseline, not a claim that a wavelet codec is novel. The audit and complexity gate are in [literature_review.md](literature_review.md) and [design_candidates.md](design_candidates.md). The public API measures only the resident-memory encode path; the CLI adds PPM disk I/O for convenience.

The dataset CSVs include PSNR and a deterministic four-scale MS-SSIM-style
proxy implemented in `scripts/dataset_benchmark.py`; LPIPS is blank because no
LPIPS runtime is installed on the CPU-only reference environment.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

On the reference image, where CMake is not installed, the equivalent build is:

```sh
mkdir -p build
clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
```

## Use

```sh
build/brushie encode input.ppm output.caps 82 8
build/brushie decode output.caps output.ppm 4096 4096 -1

# One-pass analytical rate hints (the actual byte count is reported):
build/brushie encode input.ppm output.caps --target-bytes 250000 --threads 8
build/brushie encode input.ppm output.caps --target-lpips 0.06 --threads 8
```

`decode ... 0` emits the coarse LL layer only. `-1` emits every available layer. The decoder may request any output width and height; it reconstructs the needed pyramid prefix and bilinearly resamples.

`--target-bytes` selects a calibrated quality point and adaptive 32/64/128-pixel
tiles without trial re-encodes. `--target-lpips` exposes the requested control
surface but is marked `target_lpips_unverified` because LPIPS is not linked into
the deterministic CPU core; LPIPS cells remain blank until an external audit
runtime is available.

## Enterprise evaluation

The enterprise harness compares codecs at equal MS-SSIM-proxy gates instead of
comparing unrelated quality settings. It routes photo and synthetic chat/UI
content through a 512-pixel chat-preview profile and, unless `--quick` is used,
a 1536-pixel expanded-image profile. It also accounts for the directory and
payload bytes required to reach each CAPS progressive layer.

```sh
python3 scripts/enterprise_eval.py --quick
python3 scripts/enterprise_eval.py --output-prefix enterprise_eval
```

The harness writes candidate, matched-quality, aggregate, progressive, and
preview CSVs plus a Markdown decision report. Its deterministic four-scale
MS-SSIM calculation is a directional proxy; LPIPS and blinded human preference
tests are still required before making customer-facing perceptual claims.
The current commercial go/no-go assessment is in
[enterprise_readiness.md](enterprise_readiness.md).
