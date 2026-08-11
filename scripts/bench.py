#!/usr/bin/env python3
"""Broad-corpus benchmark: public datasets, cached competitors, speeds.

Two phases:
  cache-competitors  encode AVIF/JPEG/WebP/JPEG2000 once per image and cache
                     candidate rows (bytes, metrics, timings) to a directory.
  caps               sweep CAPS per image and select smallest-bytes-per-gate
                     against cached competitor rows; write matched + speed CSV.

Both phases run candidates in a process pool (subprocess CLI encodes +
numpy metrics), so a 10-core machine gets ~8x on the metric-bound path.
"""
from __future__ import annotations
import argparse, csv, json, os, shutil, struct, subprocess, sys, tempfile, time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from collections import defaultdict
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "brushie"
THRESHOLDS = (0.970, 0.985, 0.995)
sys.path.insert(0, str(ROOT / "scripts"))
from quality_metrics import evaluate as evaluate_quality

def fit(image, max_width=512, max_height=512):
    result = image.copy()
    result.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
    return result.convert("RGB")

def to_ppm(path):
    """Return (tmp_dir, ppm_path, w, h, orig, image) fitted to 512px."""
    with Image.open(path) as im:
        image = fit(im, 512, 512)
        w, h = image.size
        orig = np.asarray(image, dtype=np.uint8)
    tmp = Path(tempfile.mkdtemp(prefix="bench-"))
    ppm = tmp / "in.ppm"
    ppm.write_bytes(f"P6\n{w} {h}\n255\n".encode() + orig.tobytes())
    return tmp, ppm, w, h, orig, image

def candidate_rows_competitors(src_path, ffmpeg, max_w, max_h):
    """All competitor candidates for one image, as dict rows."""
    rows = []
    tmp, ppm_path, w, h, orig, image = to_ppm(src_path)
    def try_block(name, fn):
        try:
            fn()
        except Exception as e:
            print(f"  [{name}] fail: {e}", flush=True)

    def jpeg_block():
        for mode, opts in [("seq420", {"optimize": True, "progressive": False, "subsampling": 2}),
                           ("prog420", {"optimize": True, "progressive": True, "subsampling": 2})]:
            for q in range(1, 101, 2):
                enc = tmp / f"j-{mode}-{q}.jpg"
                t0 = time.perf_counter()
                image.save(enc, format="JPEG", quality=q, **opts)
                et = (time.perf_counter() - t0) * 1000
                t0 = time.perf_counter()
                rec = np.asarray(Image.open(enc).convert("RGB"), dtype=np.uint8)
                dt = (time.perf_counter() - t0) * 1000
                m = evaluate_quality(orig, rec)
                rows.append(dict(codec="JPEG", setting=f"{mode},q={q}", bytes=enc.stat().st_size,
                                 encode_ms=et, decode_ms=dt, ms_ssim=m.ms_ssim_windowed,
                                 psnr=m.psnr, ssim=m.ssim_windowed, bpp=enc.stat().st_size*8/(w*h)))
    try_block("jpeg", jpeg_block)

    def webp_block():
        for q in range(1, 101, 2):
            enc = tmp / f"w-{q}.webp"
            t0 = time.perf_counter()
            image.save(enc, format="WEBP", quality=q, method=6, exact=True)
            et = (time.perf_counter() - t0) * 1000
            t0 = time.perf_counter()
            rec = np.asarray(Image.open(enc).convert("RGB"), dtype=np.uint8)
            dt = (time.perf_counter() - t0) * 1000
            m = evaluate_quality(orig, rec)
            rows.append(dict(codec="WebP", setting=f"m6,q={q}", bytes=enc.stat().st_size,
                             encode_ms=et, decode_ms=dt, ms_ssim=m.ms_ssim_windowed,
                             psnr=m.psnr, ssim=m.ssim_windowed, bpp=enc.stat().st_size*8/(w*h)))
    try_block("webp", webp_block)

    def avif_block():
        if not ffmpeg:
            return
        encoders = subprocess.run([ffmpeg, "-hide_banner", "-encoders"],
                                  capture_output=True, text=True).stdout
        svt = "libsvtav1" in encoders
        for crf in range(63, 0, -2):
            enc = tmp / f"a-{crf}.avif"
            cmd = [ffmpeg, "-y", "-loglevel", "error", "-i", str(ppm_path),
                   "-frames:v", "1", "-pix_fmt", "yuv420p"]
            if svt:
                cmd += ["-c:v", "libsvtav1", "-preset", "4", "-crf", str(crf),
                        "-svtav1-params", "avif=1:tune=0:lp=8"]
            else:
                cmd += ["-c:v", "libaom-av1", "-crf", str(crf), "-cpu-used", "8",
                        "-row-mt", "1", "-still-picture", "1"]
            cmd += ["-f", "avif", str(enc)]
            t0 = time.perf_counter()
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            et = (time.perf_counter() - t0) * 1000
            t0 = time.perf_counter()
            out = tmp / f"a-{crf}.ppm"
            subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(enc),
                            "-f", "image2", "-pix_fmt", "rgb24", str(out)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            rec = np.asarray(Image.open(out).convert("RGB"), dtype=np.uint8)
            dt = (time.perf_counter() - t0) * 1000
            m = evaluate_quality(orig, rec)
            rows.append(dict(codec="AVIF", setting=f"crf={crf}", bytes=enc.stat().st_size,
                             encode_ms=et, decode_ms=dt, ms_ssim=m.ms_ssim_windowed,
                             psnr=m.psnr, ssim=m.ssim_windowed, bpp=enc.stat().st_size*8/(w*h)))
    try_block("avif", avif_block)

    def j2k_block():
        if not ffmpeg:
            return
        for pix_fmt in ("yuv420p", "rgb24"):
            lo, hi = 1, 10000
            pts = {}
            def ev_q(q):
                if q in pts: return pts[q]
                enc = tmp / f"j2k-{pix_fmt}-{q}.jp2"
                t0 = time.perf_counter()
                subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(ppm_path),
                                "-frames:v", "1", "-c:v", "jpeg2000", "-pix_fmt", pix_fmt,
                                "-tile_width", str(w), "-tile_height", str(h),
                                "-q:v", str(q), str(enc)],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                et = (time.perf_counter() - t0) * 1000
                out = tmp / f"j2k-{pix_fmt}-{q}.ppm"
                t0 = time.perf_counter()
                subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(enc),
                                "-f", "image2", "-pix_fmt", "rgb24", str(out)],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                rec = np.asarray(Image.open(out).convert("RGB"), dtype=np.uint8)
                dt = (time.perf_counter() - t0) * 1000
                m = evaluate_quality(orig, rec)
                row = dict(codec="JPEG2000", setting=f"pix={pix_fmt},q={q}", bytes=enc.stat().st_size,
                           encode_ms=et, decode_ms=dt, ms_ssim=m.ms_ssim_windowed,
                           psnr=m.psnr, ssim=m.ssim_windowed, bpp=enc.stat().st_size*8/(w*h))
                pts[q] = row
                rows.append(row)
                return row
            ev_q(lo); ev_q(hi)
            for th in THRESHOLDS:
                if float(ev_q(lo)["ms_ssim"]) < th: continue
                if float(ev_q(hi)["ms_ssim"]) >= th: continue
                a, b = lo, hi
                while b - a > 1:
                    mid = (a + b) // 2
                    if float(ev_q(mid)["ms_ssim"]) >= th: a = mid
                    else: b = mid
                for q in range(max(1, a-2), min(10000, b+3)): ev_q(q)
    try_block("j2k", j2k_block)

    shutil.rmtree(tmp, ignore_errors=True)
    return rows


def candidate_rows_caps(src_path, max_w, max_h):
    rows = []
    tmp, ppm_path, w, h, orig, _image = to_ppm(src_path)
    threads = os.environ.get("BRUSHIE_BENCH_THREADS", "8")
    try:
        for q in range(1, 101):
            for base in (32, 64):
                enc = tmp / f"c-{q}-{base}.brbr"
                t0 = time.perf_counter()
                subprocess.run([str(CLI), "encode", str(ppm_path), str(enc), str(q), threads,
                                "64", "--base-target", str(base)],
                               capture_output=True, text=True, check=True)
                et = (time.perf_counter() - t0) * 1000
                dec = tmp / f"c-{q}-{base}.ppm"
                t0 = time.perf_counter()
                subprocess.run([str(CLI), "decode", str(enc), str(dec), str(w), str(h), "-1"],
                               capture_output=True, text=True, check=True)
                rec = np.asarray(Image.open(dec).convert("RGB"), dtype=np.uint8)
                dt = (time.perf_counter() - t0) * 1000
                m = evaluate_quality(orig, rec)
                rows.append(dict(codec="CAPS", setting=f"q={q},base={base}", bytes=enc.stat().st_size,
                                 encode_ms=et, decode_ms=dt, ms_ssim=m.ms_ssim_windowed,
                                 psnr=m.psnr, ssim=m.ssim_windowed, bpp=enc.stat().st_size*8/(w*h)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return rows

def image_list(args):
    imgs = []
    for line in args.images.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            imgs.append(Path(line))
    return imgs

def _worker_caps(p): return candidate_rows_caps(p, 512, 512)
def _worker_comp(ffmpeg, p): return candidate_rows_competitors(p, ffmpeg, 512, 512)

def run_phase(imgs, fn, workers):
    results = {}
    with ProcessPoolExecutor(max_workers=workers) as ex:
        futs = {ex.submit(fn, str(p)): p for p in imgs}
        for f in as_completed(futs):
            p = futs[f]
            try:
                results[str(p)] = f.result()
            except Exception as e:
                print(f"WARN {p}: {e}", flush=True)
                results[str(p)] = []
    return results

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("cache-competitors")
    p.add_argument("--images", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--ffmpeg", default=None)
    p.add_argument("--workers", type=int, default=8)
    p = sub.add_parser("caps")
    p.add_argument("--images", type=Path, required=True)
    p.add_argument("--competitors", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--workers", type=int, default=8)
    p = sub.add_parser("profile")
    p.add_argument("--images", type=Path, required=True)
    p.add_argument("--matched", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()

    if args.cmd == "cache-competitors":
        imgs = image_list(args)
        import functools
        res = run_phase(imgs, functools.partial(_worker_comp, args.ffmpeg), args.workers)
        args.out.mkdir(parents=True, exist_ok=True)
        for p, rows in res.items():
            name = Path(p).stem
            with (args.out / f"{name}.csv").open("w", newline="") as f:
                if rows:
                    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
                    w.writeheader(); w.writerows(rows)
        (args.out / "_manifest.json").write_text(json.dumps({
            "images": {str(p): len(v) for p, v in res.items()}, "ffmpeg": args.ffmpeg}))
        print("competitor cache done:", len(res), "images")
        return

    if args.cmd == "profile":
        imgs = image_list(args)
        run_profile_phase(imgs, args.matched, args.out, args.workers)
        return

    if args.cmd == "caps":
        imgs = image_list(args)
        res = run_phase(imgs, _worker_caps, args.workers)
        comp = defaultdict(list)
        if args.competitors.exists():
            for csvf in args.competitors.glob("*.csv"):
                with csvf.open() as f:
                    for row in csv.DictReader(f):
                        comp[csvf.stem].append(row)
        matched = []
        speed_rows = []
        for p, caps_rows in res.items():
            name = Path(p).stem
            for th in THRESHOLDS:
                elig = [r for r in caps_rows if float(r["ms_ssim"]) >= th]
                if not elig: continue
                best = min(elig, key=lambda r: int(r["bytes"]))
                matched.append(dict(sample=name, threshold=th, codec="CAPS", bytes=int(best["bytes"]),
                                    setting=best["setting"], ms_ssim=float(best["ms_ssim"]),
                                    encode_ms=float(best["encode_ms"]), decode_ms=float(best["decode_ms"])))
            for codec_name in ("AVIF", "WebP", "JPEG", "JPEG2000"):
                for th in THRESHOLDS:
                    elig = [cr for cr in comp.get(name, [])
                            if cr["codec"] == codec_name and float(cr["ms_ssim"]) >= th]
                    if not elig:
                        continue
                    best = min(elig, key=lambda cr: int(cr["bytes"]))
                    matched.append(dict(sample=name, threshold=th, codec=codec_name,
                                        bytes=int(best["bytes"]), setting=best["setting"],
                                        ms_ssim=float(best["ms_ssim"]), encode_ms=float(best["encode_ms"]),
                                        decode_ms=float(best["decode_ms"])))
            # speed rows: CAPS encode+decode mean wall ms at the .970 candidate
            b970 = next((m for m in matched if m["sample"]==name and m["threshold"]==0.970 and m["codec"]=="CAPS"), None)
            if b970:
                setting = b970["setting"]
                hit = [r for r in caps_rows if r["setting"]==setting][0]
                speed_rows.append(dict(sample=name, codec="CAPS", encode_ms=hit["encode_ms"], decode_ms=hit["decode_ms"]))
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(matched[0].keys()) if matched else ["sample"])
            w.writeheader(); w.writerows(matched)
        by = defaultdict(list)
        for m in matched: by[(m["codec"], m["threshold"])].append(m)
        agg_out = args.out.with_name(args.out.stem + "_aggregate.csv")
        with agg_out.open("w", newline="") as f:
            w = csv.writer(f); w.writerow(["codec","threshold","samples","mean_bytes","mean_encode_ms","mean_decode_ms"])
            for (codec, th), rows in sorted(by.items()):
                w.writerow([codec, th, len(rows), int(np.mean([r["bytes"] for r in rows])),
                            round(float(np.mean([r["encode_ms"] for r in rows])),2),
                            round(float(np.mean([r["decode_ms"] for r in rows])),2)])
        sp_out = args.out.with_name(args.out.stem + "_speeds.csv")
        with sp_out.open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["sample","codec","encode_ms","decode_ms"])
            w.writeheader(); w.writerows(speed_rows)
        print("caps eval done:", len(matched), "matched rows ->", args.out)
        print(open(agg_out).read())



# ---------------------------------------------------------------------------
# profile phase: per-image adaptive quant profiles with the REAL metric.
# For each image and gate, find the baseline gate quality, then try a small
# grid of quant-table profiles around it and keep the smallest candidate that
# still clears the gate. This is per-image encoder adaptivity (like AVIF's
# own RDO), measured by the actual windowed MS-SSIM.
# ---------------------------------------------------------------------------
PROFILES = {
    "base":   "",
    "coarse_hi": "6,1.45,0.8,1.8,1.2,2.5,2.0,0.5",   # protect coarse levels
    "coarse_lo": "6,1.05,0.8,1.8,1.2,2.5,2.0,0.3",   # protect base, fine coarse
    "chroma_hi": "6,1.25,0.8,1.8,1.2,2.8,2.0,0.4",   # crush low-q chroma
    "diag_hi":   "6,1.25,0.8,2.2,1.2,2.5,2.0,0.4",   # crush diagonals
    "fine_lo":   "6,1.25,0.8,1.8,1.2,2.5,2.0,0.6",   # finer base
}

def _caps_one(ppm_path, q, base, threads, profile):
    w, h = None, None
    with Image.open(ppm_path) as im:
        w, h = im.size
    enc = Path(ppm_path).parent / f"p-{q}-{base}-{abs(hash(profile))}.brbr"
    subprocess.run([str(CLI), "encode", str(ppm_path), str(enc), str(q), threads,
                    "64", "--base-target", str(base)],
                   capture_output=True, text=True, check=True)
    dec = Path(ppm_path).parent / f"p-{q}-{base}.ppm"
    subprocess.run([str(CLI), "decode", str(enc), str(dec), str(w), str(h), "-1"],
                   capture_output=True, text=True, check=True)
    with Image.open(ppm_path) as im0:
        orig = np.asarray(im0.convert("RGB"), dtype=np.uint8)
    rec = np.asarray(Image.open(dec).convert("RGB"), dtype=np.uint8)
    m = evaluate_quality(orig, rec)
    sz = enc.stat().st_size
    enc.unlink(missing_ok=True); dec.unlink(missing_ok=True)
    return dict(codec="CAPS", setting=f"q={q},base={base},prof={profile}", bytes=sz,
                ms_ssim=m.ms_ssim_windowed, psnr=m.psnr, ssim=m.ssim_windowed,
                bpp=sz*8/(w*h))

def _worker_profile(args):
    # args = (src_path, gate, q, base)
    src, gate, q, base = args
    tmp, ppm, _w, _h, _orig, _ = to_ppm(src)
    threads = os.environ.get("BRUSHIE_BENCH_THREADS", "8")
    best = None
    for name, params in PROFILES.items():
        old = os.environ.get("BRUSHIE_QPARAMS")
        os.environ["BRUSHIE_QPARAMS"] = params
        try:
            r = _caps_one(ppm, q, base, threads, name)
        finally:
            if old is None: os.environ.pop("BRUSHIE_QPARAMS", None)
            else: os.environ["BRUSHIE_QPARAMS"] = old
        if float(r["ms_ssim"]) >= gate and (best is None or r["bytes"] < best["bytes"]):
            best = r
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    return (src, gate, best)

def run_profile_phase(imgs, matched_csv, out_csv, workers):
    """matched_csv: caps matched rows (sample,threshold,bytes,setting)."""
    with matched_csv.open() as f:
        rows = list(csv.DictReader(f))
    tasks = []
    for r in rows:
        if r["codec"] != "CAPS":
            continue
        q = int(r["setting"].split("q=")[1].split(",")[0])
        base = int(r["setting"].split("base=")[1])
        tasks.append((r["sample"], float(r["threshold"]), q, base))
    # map sample stem -> src path
    stem2src = {Path(p).stem: p for p in imgs}
    jobs = []
    for sample, gate, q, base in tasks:
        if sample not in stem2src:
            continue
        jobs.append((stem2src[sample], sample, gate, q, base))
    out_rows = []
    with ProcessPoolExecutor(max_workers=workers) as ex:
        futs = {ex.submit(_worker_profile, (src, gate, q, base)): (src, sample, gate, q, base)
                for src, sample, gate, q, base in jobs}
        for f in as_completed(futs):
            src, sample, gate, q, base = futs[f]
            try:
                _src2, gate2, best = f.result()
                if best:
                    out_rows.append({"sample": sample, "threshold": gate2,
                                     "bytes": best["bytes"], "setting": best["setting"],
                                     "ms_ssim": best["ms_ssim"], "profile": best["setting"].split("prof=")[1]})
            except Exception as e:
                print(f"WARN profile {sample}: {e}", flush=True)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["sample","threshold","bytes","setting","ms_ssim","profile"])
        w.writeheader(); w.writerows(out_rows)
    print("profile phase done:", len(out_rows), "improved candidates ->", out_csv)

if __name__ == "__main__":
    main()
