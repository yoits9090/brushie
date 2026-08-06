# Human visual review

The generated side-by-side at
`examples/kodak01_512_comparison.png` was inspected at native display size.
The full all-layer reconstruction tracks the original facade closely: window
frames, red shutters, door edges, and masonry boundaries are present. The
layer-0 thumbnail is intentionally low-frequency and visibly blurred, which is
expected for progressive transport rather than a decoder failure.

At q=82 the main subjective risk is fine texture: mortar grain, thin window
muntins, and grass are softened before large structural edges disappear. The
chroma-aware step keeps the red shutters and neutral stone visually stable but
can desaturate small colored details. This is recorded as `QUALITY REGRESSION`
relative to the earlier fixed-step q=82 point, while the smaller stream is a
non-dominated rate point.

Hostile-pattern results in `hostile_results.csv` confirm the expected failure
classes. Noise and dense one-pixel lines retain high bpp because the local
transform has little sparsity; checker and text/UI patterns preserve hard edges
but expose ringing/softness at the coarsest progressive layer. These are
`MEASURED` stress results, not hidden from the frozen-corpus averages.

No human review is used to fill the missing LPIPS metric. LPIPS remains
`UNAVAILABLE` and all quality-threshold claims are marked unverified until an
external, declared runtime is added.
