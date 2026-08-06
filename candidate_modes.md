# Candidate and hybrid modes

The implementation keeps all decisions analytical and tile-local. Each mode
was checked against the two-pass and 2 GB memory gates before measurement.

| Intervention | Complexity estimate | Result/status |
|---|---|---|
| Absolute zero-run + signed varints | One quantization pass plus emitted-byte work; 0 optimization iterations | `IMPLEMENTED`, `MEASURED` compatibility mode |
| Delta varints between nonzero symbols | Same O(N) bound; one previous-symbol state per tile | `IMPLEMENTED`, `DATASET-SPECIFIC`; not selected on Kodak |
| Significance mask + signed bit packing (mode 2) | Two coefficient passes, <=32 bit operations/nonzero; tile scratch <=128² int32 | `IMPLEMENTED`, `IMPROVES PARETO FRONTIER` |
| Chroma-aware 2x scalar step | One branch/channel, no extra pass or memory | `IMPLEMENTED`, `IMPROVES PARETO FRONTIER`; lower PSNR at a smaller byte point |
| Adaptive 32/64/128 tile selection | O(1) policy from dimensions/target hint; no image scan | `IMPLEMENTED`, `IMPROVES PARETO FRONTIER`; removes directory overhead |
| Flat/palette tile records | Requires a second image-space residual test and a new decoder path | `PROPOSED`; not retained in this constrained iteration |
| Edge/wedge codebook | Fixed codebook is plausible, exhaustive line search is not | `PROPOSED`; `NOVELTY UNRESOLVED` |
| Analytic Gaussian splats / tiny 3x3 colour solve | 45--90 ops/pixel with bounded support; no fitting iterations | `PROPOSED`; needs a fair decoder/rate comparison |
| Greedy matching pursuit, per-image Gaussian fitting, neural encoder | K-dependent/global work or GPU/pretrained model | `INVALIDATED` by the 2.5 s CPU/no-training gate |

## Measured intervention evidence

At Kodak 512, q=82, tile32, mode-2-only selection changed the earlier
331083-byte stream at 39.56 dB PSNR to a final chroma-aware point of 255182
bytes at 38.53 dB. The lower-rate point is non-dominated and is therefore
retained as a rate--distortion frontier point; the quality change is recorded as
`QUALITY REGRESSION` relative to the earlier fixed-step point. Adaptive tiles
leave reconstruction unchanged while reducing the 4096² Kodak stream to
2,192,313 bytes from the fixed-32 structural overhead and reduce encode time.

The old delta mode remains readable for v1 streams, even though the frozen
corpus selected no mode-1 chunks. This compatibility choice is intentional and
is recorded as `NOVELTY UNRESOLVED`, not as a claim of a new entropy model.
