# Ultra-low-bitrate CAPS goal

## Objective

Transform the CAPS prototype into a deterministic, CPU-only, content-adaptive
image representation for a complete 4096x4096 RGB image. The encoded byte
stream must be self-contained, progressive, spatially local, and decodable to
arbitrary output resolution. The primary optimization is minimum total bytes
subject to measured visual quality, with resident-memory end-to-end encode and
decode both below 2.5 seconds on the fixed reference host.

The implementation deliberately avoids GPU/Metal/CUDA, remote services, a large
pretrained encoder, and per-image gradient descent. Pyramid creation is
analytical 5/3 lifting; primitive/parameter selection is deterministic tile
mode competition and scalar quantization.

## Acceptance points

| Point | Intended constraint | Current evidence |
|---|---|---|
| A | LPIPS <= .03, MS-SSIM >= .985 | `MEASURED` MS-SSIM proxy only; LPIPS `UNAVAILABLE` |
| B | LPIPS <= .06, MS-SSIM >= .970 | `MEASURED` MS-SSIM proxy only; LPIPS `UNAVAILABLE` |
| C | LPIPS <= .10, MS-SSIM >= .940 | `MEASURED` MS-SSIM proxy only; LPIPS `UNAVAILABLE` |
| D | thumbnail/progressive base | `MEASURED` in `progressive_results.csv`; no metric gate |

The target interfaces are implemented in the CLI/API. `--target-bytes` chooses
an analytical quality/tile point in one pass and reports actual bytes. The
`--target-lpips` surface maps to a conservative quality point but prints
`target_lpips_unverified`; the core does not claim an LPIPS measurement.

## Frozen reference result

On the reference MacBook Pro Mac17,2 (Apple M5, 10 cores: 4 performance + 6
efficiency, 32 GB, macOS 26.5.1), using exactly 8 pthread workers, the final
adaptive point (`q=82`, 64-pixel tiles) for bicubic-upscaled Kodak 01 is:

- encode: 480.102 ms, resident RGB through in-memory byte vector;
- decode: 374.022 ms to 4096x4096;
- stream: 2,192,313 bytes, 1.045 bpp;
- PSNR: 34.148 dB; MS-SSIM proxy 0.992856 on this image;
- peak resident process: 527.628 MiB.

Synthetic 4096² rows remain below 2.5 s (596--942 ms encode, 306--486 ms
decode). The complete byte decomposition and mode counts are in
`byte_accounting.md` and `entropy_audit.csv`.

## Status vocabulary

`IMPLEMENTED`, `MEASURED`, `IMPROVES PARETO FRONTIER`, `QUALITY REGRESSION`,
`SPEED REGRESSION`, `DATASET-SPECIFIC`, `INVALIDATED`, `PROPOSED`, and
`NOVELTY UNRESOLVED` are used throughout the reports. No novelty claim is made
for the underlying lifting/wavelet basis or entropy primitives.
