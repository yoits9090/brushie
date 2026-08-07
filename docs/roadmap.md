# Brushie roadmap: from "beats JPEG/WebP" to "beats every format"

Status as of this session: **v2 wavelet codec** — beats JPEG at every gate,
beats WebP at .970/.985, ~ties at .995, 1.1-1.7x behind AVIF, ~10-15x cheaper
to encode than AVIF. This file tracks the campaign to beat AVIF and JPEG XL.

## Rules of engagement (unchanged)

- Deterministic, CPU-only, no per-image gradient descent, end-to-end
  encode+decode < 2.5 s at 4096^2 on the reference Apple-Silicon host.
- Every intervention is measured at equal quality on the frozen corpus
  (Kodak + synthetic chat/meme, both profiles) before it ships.
- The MS-SSIM proxy gates (.970/.985/.995) drive iteration; SSIMULACRA2 /
  Butteraugli / blinded human tests are the acceptance gate for claims.

## Milestones

### M1 (DONE) — v1 research baseline
7-12x behind WebP/AVIF at equal proxy quality. Directory + entropy coding
dominated the payload.

### M2 (DONE) — v2 wavelet codec
Context-adaptive arithmetic coding, whole-band chunks, frequency-ordered
midtread quantization, 4:2:0 chroma, median-predicted LL, RGBA, PNG I/O,
fuzzing. Result: beats JPEG everywhere, beats WebP at .970/.985, ties .995,
1.1-1.7x behind AVIF. Docs: format_spec.md, enterprise_readiness.md,
competitive_landscape.md.

### M3 (RESULT: REJECTED on the MS-SSIM proxy) — activity-aware quantization
AUREA-style zero-bit activity map was implemented and measured in both
directions (edge-preserving and psychovisual masking); both lose at equal
proxy quality (see docs/experiments_log.md). The proxy does not reward
spatial step modulation. Re-test behind M8 with SSIMULACRA2/Butteraugli
before closing it out. Same for RDOQ-lite: needs coder-integrated rate
estimation to be meaningful.

### M4 — entropy-coder depth
- Energy-bucket significance contexts (neighbor magnitude classes) and a
  larger context set (AUREA-style, 128-class), measured per band type.
- Bitplane refinement coding for the high-quality gate (.995, currently
  1.26x behind AVIF): EBCOT-style significance/refinement passes with
  embedded truncation points.

Exit criteria: close .995 to parity with AVIF on photos.

### M5 — chroma-from-luma prediction
Predict Co/Cg residuals from the luma band content (AV1 CCLM-style, but
analytical per band). Chroma is already cheap (4:2:0 + 2x steps); this is a
smaller lever but low risk.

### M6 — structural options (evaluate, keep what wins)
- Lapped/overlapped synthesis to cut wavelet ringing.
- Adaptive/deeper pyramid for smooth gradients.
- Grain parameterization for noisy content (JPEG XL / WEBSL idea): detect
  grain, code a seed + strength, synthesize on decode. Big win for photos
  at low bitrate (grain is otherwise incompressible).

### M7 — the text/UI gap (biggest remaining, hardest)
Chat/meme at .970 is 3.8x behind AVIF because AV1 predicts flat regions and
text. Options: (a) a planar/flat-region mode on the base LL (smooth patches
coded as polynomial/gradient parameters), (b) run-length / pattern coding
for UI content, (c) accept the gap and win on photo/web workloads instead.

### M8 — validation and claims
- Add SSIMULACRA2 (or Butteraugli) to the eval harness; re-gate everything.
- Blinded human preference test on a private corpus.
- Fuzz + sanitizer pass, alpha/color-profile/metadata surface, WASM decoder,
  Linux x86/ARM builds. Then approach design partners.

## Benchmark targets (in order)

1. jpegli (JPEG-compatible, beats WebP): match or beat at .970-.995.
2. WebP at every gate (currently losing .995 by ~1.2x).
3. AVIF at every gate (currently losing 1.1-1.7x).
4. libjxl default effort on the same corpus (the current king).

## Track record (session log)

- 9/7 CDF transform: rejected. With clamped boundaries it matched 5/3's
  energy profile (no win); proper symmetric extension still showed no
  measurable gain on this content. Documented in session notes.
- Analytical 2D Gaussian splat layer (original design candidate #9):
  rejected. Greedy splats on the base LL did not beat median-predicted
  coefficients; residual entropy did not shrink without per-image
  optimization (forbidden). See docs/experiments_splat.md.
- Mean-|coefficient| per-band rate allocation: rejected. Scaling steps by
  (global_mean/band_mean)^k just slides the RD curve along the rate axis
  (k<0 helps .970, hurts .985; k>0 the reverse; k=0 is the empirical table).
- Sign contexts (4) and above-right significance context: measured neutral
  on photos; sign contexts kept (principled), above-right dropped (context
  dilution).
- In-place transforms: kept (memory -7%).
- Midtread quantization + plain reconstruction vs dead-zone: midtread won
  decisively; dead-zone's reconstruction bias hurt SSIM-family metrics.

## Open questions

- Does the MS-SSIM proxy reward edge protection enough for M3 to show?
  (Measure; if not, gate M3 on SSIMULACRA2 once M8 lands.)
- Is the 5/3 wavelet the right long-term basis, or should M6 revisit
  lapped DCT (AUREA) / 9/7 with RDO?
- WebP/AVIF encode speed on this host is a known advantage (~10x); should
  the product pitch lead with the cost axis ("same bytes as WebP at JPEG
  CPU") rather than raw density?
