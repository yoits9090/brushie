# CAPS ultra-low-bitrate final report

## Executive result

`IMPLEMENTED` and `MEASURED`: CAPS now has deterministic per-tile mode
competition, chroma-aware rate allocation, adaptive tile sizing, progressive
layers, arbitrary-resolution decode, and byte-complete accounting. The final
4096² Kodak reference point is 2,192,313 bytes (1.045 bpp), 480.102 ms encode,
374.022 ms decode, and 527.675 MiB peak resident memory using exactly 8 worker
threads on the Apple M5 reference host. It satisfies the 2.5 s CPU latency and
2 GB memory gates for this measured point; synthetic 4096² rows are 596--942 ms
encode and 306--486 ms decode.

The 1024² <250 ms stretch is measured on the frozen and hostile corpus (the
slowest hostile encode is 96.2 ms); real-time progressive playback and LPIPS
certification are not claimed. The result is a codec research prototype, not a
claim of Image-GS-level rate--distortion quality.

## Representation and interventions

The stream is a reversible YCoCg 5/3 pyramid. Independent 32/64/128-pixel
coefficient tiles carry layer, band, channel, quantizer, mode, offset, length,
count, and checksum in the self-contained directory. Mode 0 is zero-run plus
signed varints; mode 1 is delta varints; mode 2 is a significance mask and
per-tile signed bit packing. The encoder quantizes once and emits all candidates
in a second coefficient traversal, satisfying the maximum-two post-pyramid
pass constraint.

Three interventions were retained:

1. `IMPROVES PARETO FRONTIER`: mode-2 bit packing removes repeated one-byte
   zero runs on dense tiles.
2. `IMPROVES PARETO FRONTIER` / `QUALITY REGRESSION`: a bounded 2x chroma step
   lowers q=82 Kodak-512 bytes from the earlier 331083-byte point to 255182
   bytes, with PSNR moving from 39.56 to 38.53 dB.
3. `IMPROVES PARETO FRONTIER`: adaptive tiles cut the 4096² Kodak directory
   from 1,966,080 to 491,520 bytes without changing reconstruction.

The old delta mode is `IMPLEMENTED` for compatibility but is not selected in
the frozen Kodak audit. Learned Gaussian fitting, matching pursuit, and neural
encoders are `INVALIDATED` by the CPU/no-training/latency gate. Underlying
wavelet and entropy constructions remain `NOVELTY UNRESOLVED`.

## Rate--distortion evidence

`rate_distortion_results.csv` contains 544 frozen-corpus rows (34 images, 512²
and 1024², qualities 100/90/82/70/50/35/20, plus q=82 tile64 comparisons).
At 512² and tile32, corpus means are:

| Quality | bpp | PSNR (dB) | MS-SSIM proxy |
|---:|---:|---:|---:|
| 100 | 17.573 | 99.000 | 1.0000 |
| 90 | 11.769 | 42.161 | .99946 |
| 82 | 7.414 | 38.621 | .99868 |
| 70 | 5.761 | 36.333 | .99755 |
| 50 | 4.339 | 33.734 | .99521 |
| 35 | 3.852 | 32.343 | .99317 |
| 20 | 3.420 | 30.644 | .99000 |

LPIPS columns are blank with `unavailable` status. Therefore A/B/C threshold
rows are not certified; the MS-SSIM portion is measured only by the declared
proxy. Hostile noise, line, checker, texture, edge, and text/UI cases are in
`hostile_results.csv` and show where the transform loses sparsity.

## Baselines and reproducibility

`benchmark_dataset_results.csv` includes PNG, JPEG, WebP, AVIF, and CAPS at the
same resized corpus points. Mean q=82 rows over both sizes are CAPS 6.183 bpp /
38.141 dB, JPEG 1.615 bpp / 36.621 dB, WebP 1.246 bpp / 36.882 dB, and AVIF
0.831 bpp / 35.649 dB. PNG is lossless at 13.144 bpp. JXL is explicitly marked
unavailable in `baseline_availability.csv` rather than silently omitted.

The exact host, compiler, thread count, timing scope, and memory convention are
in `hardware_profile.txt` and `benchmark_environment.txt`. Re-run:

```sh
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic -Iinclude \
  src/codec.cpp src/main.cpp -o build/brushie -pthread
./build/test_codec
python3 scripts/benchmark.py 8
python3 scripts/rate_distortion.py
python3 scripts/byte_audit.py
```

The generated CSVs and `format_spec.md` are part of the deliverable; no hidden
model, table, or sidecar is needed to decode a stream.
