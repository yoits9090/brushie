# Candidate gate

Reference host for the estimate: MacBook Pro Mac17,2, Apple M5, 10 cores (4 performance + 6 efficiency), 32 GB, macOS 26.5.1, Apple clang 17.0.6.4.2. The benchmark uses exactly 8 worker threads unless a run explicitly says otherwise.

For 4096², N = 16,777,216 pixels. A full-resolution pass is counted as one read/write traversal of all N pixels after pyramid construction. Pyramid work is counted separately. The estimates below are conservative order-of-magnitude budgets, not claimed measurements.

| # | Candidate | Post-pyramid passes | Approx. work/pixel | Local optimization | Parallelism / temporary memory | Entropy cost | Gate |
|---:|---|---:|---:|---:|---|---:|---|
| 1 | Gradient-guided analytical isotropic Gaussians | 2 | 35-60 scalar ops + bounded splat writes | 0 | 8 threads; 4-6 bytes/pixel scratch | 5-15 ops/nonzero | **Pass** if splat support is capped |
| 2 | Structure-tensor anisotropic splats | 2 | 90-150 ops incl. 2x2 eigen solve | 0 | 8 threads; 8-12 bytes/pixel | 8-20 ops/nonzero | **Conditional**; block tensor aggregation required |
| 3 | Laplacian-pyramid compact B-spline residual atoms | 1-2 | 20-35 ops, plus 4/3N pyramid | 0 | 8 threads; 6-10 bytes/pixel | 4-10 ops/coefficient | **Pass; primary** |
| 4 | Quadtree polynomial patches | 1-2 | 25-50 ops with integral statistics | 0 (fixed split budget) | 8 threads; 8-16 bytes/pixel | 5-12 ops/node | **Pass; primary/backup** |
| 5 | Compact-support elliptical polynomial codebook | 2 | 45-90 ops and 8-32 bounded taps/atom | 0 | 8 threads; 8-16 bytes/pixel | 5-15 ops/atom | **Pass** only with 8-shape codebook |
| 6 | Discrete orientation/scale Gaussian codebook | 2 | 30-70 ops + bounded tile bins | 0 | 8 threads; 4-8 bytes/pixel | 5-15 ops/atom | **Pass** |
| 7 | Fixed geometry + local 3x3 colour least-squares | 2 | 45-80 ops; 9 accumulations/node | 0 (one analytical solve/node) | 8 threads; <16 bytes/pixel | 5-15 ops/node | **Pass** with <=4K nodes/tile |
| 8 | Greedy matching pursuit residual fitting | K+1 | >=80K ops/pixel for K=1000 | K=256-10K | 8 threads; residual + heap >500 MB | heap dominates | **Reject** |
| 9 | Splat base + sparse raster residual tiles | 2 | 25-45 ops; residual only in selected tiles | 0 | 8 threads; 1-2 bytes/pixel residual mask | 4-12 ops/selected byte | **Pass; chosen hybrid** |
| 10 | Tiny learned CPU encoder | 2 | 1K-50K MACs/pixel depending on model | 0 | 8 threads; 100 MB-1 GB activations | model-dependent | **Reject primary**; optional comparison only |

## Selected representation

CAPS (Compact Adaptive Pyramid Streams) combines candidate 3 with candidate 9. The base is a reversible YCoCg transform followed by a 5/3 lifting pyramid. Each nonzero quantized detail coefficient is an explicit compact-support oriented wavelet atom; coefficient tiles form the spatial random-access unit. The base LL layer and coarse detail layers are independently decodable, so reconstruction is progressive and arbitrary output resolution is obtained by stopping at the level whose sample spacing is at most the requested output spacing, then bilinear resampling. A sparse residual layer may retain a small number of finest-level coefficients in high-energy tiles.

The encoder has no optimizer and no iteration-dependent state. It computes fixed lifting coefficients, applies a deterministic dead-zone quantizer, emits zero-run/signed-value pairs per tile, and appends a checksum. The measured process high-water mark is 1,065 MiB on the reference host at 4096² (below the 2 GB limit); the implementation keeps the resident input, int32 pyramid, and encoded chunk payloads in memory. The only full-resolution work after pyramid creation is quantization/streaming of the finest detail bands and the optional residual mask; all other work is on progressively smaller levels.

The basis and sparse coefficient coding are established transform-coding ideas. The research value here is the explicit CPU latency budget, hard primitive/stream locality, deterministic analytical allocation, and a hybrid residual fallback—not a claim to have invented wavelets.
