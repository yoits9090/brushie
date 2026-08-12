#!/usr/bin/env python3
"""Brushie total-instrumentation tracker.

One command captures every timeline stage (encode/decode JSONL with ns + cycles),
every byte (v5 stream audit), and the run metadata (git sha, host, image, quality,
bytes, bpp, ms, PSNR). All artifacts land in tracking/<tag>/ and are listed in a
manifest so any experiment can be diffed against any other.

Usage:
  python3 scripts/track.py run <image> <quality> [--tag TAG] [--box colab-lab]
"""
from __future__ import annotations
import argparse, csv, hashlib, json, os, platform, subprocess, sys, time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from byte_audit import audit  # noqa: E402

TRACK = ROOT / "tracking"


def git_sha() -> str:
    r = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                       capture_output=True, text=True)
    return r.stdout.strip() or "unknown"


def local_run(tag_dir: Path, image: str, quality: int, codec: str | None) -> dict:
    from PIL import Image
    cli = codec or str(ROOT / "build" / "brushie")
    im = Image.open(image).convert("RGB")
    ppm = str(tag_dir / "in.ppm")
    im.save(ppm)
    stream = tag_dir / "out.brbr"
    dec = tag_dir / "out.ppm"
    env = dict(os.environ)
    env["BRUSHIE_TRACK"] = "1"
    env["BRUSHIE_TRACK_FILE"] = str(tag_dir / "encode.jsonl")
    t0 = time.time()
    r = subprocess.run([cli, "encode", ppm, str(stream), "--quality", str(quality)],
                       capture_output=True, text=True, env=env)
    enc_ms = (time.time() - t0) * 1000.0
    enc_out = r.stdout.strip()
    if r.returncode != 0:
        raise RuntimeError("encode failed: " + r.stderr)
    env["BRUSHIE_TRACK_FILE"] = str(tag_dir / "decode.jsonl")
    w, h = Image.open(ppm).size
    t0 = time.time()
    r = subprocess.run([cli, "decode", str(stream), str(dec), str(w), str(h)],
                       capture_output=True, text=True, env=env)
    dec_ms = (time.time() - t0) * 1000.0
    dec_out = r.stdout.strip()
    if r.returncode != 0:
        raise RuntimeError("decode failed: " + r.stderr)
    import numpy as np
    a = np.asarray(Image.open(ppm)).astype(float)
    b = np.asarray(Image.open(dec)).astype(float)
    mse = np.mean((a - b) ** 2)
    psnr = 99.0 if mse == 0 else 10 * np.log10(255.0**2 / mse)
    rows = audit(stream)
    bytes_csv = tag_dir / "bytes.csv"
    with open(bytes_csv, "w", newline="") as f:
        wcsv = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        wcsv.writeheader()
        wcsv.writerows(rows)
    meta = {
        "tag": tag_dir.name, "git_sha": git_sha(),
        "utc": datetime.now(timezone.utc).isoformat(),
        "host": platform.node(), "platform": platform.platform(), "box": None,
        "image": os.path.basename(image), "quality": quality,
        "stream_bytes": stream.stat().st_size,
        "bpp": 8.0 * stream.stat().st_size / (w * h),
        "encode_ms_wall": round(enc_ms, 3), "decode_ms_wall": round(dec_ms, 3),
        "psnr_db": round(psnr, 3), "encode_cli": enc_out, "decode_cli": dec_out,
        "stream_sha256": hashlib.sha256(stream.read_bytes()).hexdigest(),
    }
    (tag_dir / "run.json").write_text(json.dumps(meta, indent=2))
    return meta


BOX_SCRIPT = """set -e
cd /content/brushie
export BRUSHIE_TRACK=1
export BRUSHIE_TRACK_FILE=__REMOTE__/encode.jsonl
./build/brushie encode __REMOTE__/in.ppm __REMOTE__/out.brbr --quality __Q__
export BRUSHIE_TRACK_FILE=__REMOTE__/decode.jsonl
W=$(python3 -c "from PIL import Image; print(Image.open('__REMOTE__/in.ppm').size[0])")
H=$(python3 -c "from PIL import Image; print(Image.open('__REMOTE__/in.ppm').size[1])")
./build/brushie decode __REMOTE__/out.brbr __REMOTE__/out.ppm $W $H
"""


def box_run(tag_dir: Path, image: str, quality: int, box: str) -> dict:
    from PIL import Image
    remote = "/content/track/" + tag_dir.name
    subprocess.run(["ssh", box, "mkdir -p " + remote], check=True, capture_output=True)
    im = Image.open(image).convert("RGB")
    ppm = tag_dir / "in.ppm"
    im.save(ppm)
    with open(ppm, "rb") as fh:
        subprocess.run(["ssh", box, "cat > " + remote + "/in.ppm"], input=fh.read(),
                       check=True)
    script = BOX_SCRIPT.replace("__REMOTE__", remote).replace("__Q__", str(quality))
    r = subprocess.run(["ssh", box, "bash -s"], input=script,
                       capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise RuntimeError("box run failed: " + r.stderr[-2500:])
    for f in ["encode.jsonl", "decode.jsonl", "out.brbr", "out.ppm"]:
        cat = subprocess.run(["ssh", box, "cat " + remote + "/" + f],
                             capture_output=True, timeout=300)
        if cat.returncode != 0:
            raise RuntimeError("pull failed for " + f + ": " + cat.stderr[-300:])
        (tag_dir / f).write_bytes(cat.stdout)
    import numpy as np
    a = np.asarray(Image.open(tag_dir / "in.ppm")).astype(float)
    b = np.asarray(Image.open(tag_dir / "out.ppm")).astype(float)
    mse = np.mean((a - b) ** 2)
    psnr = 99.0 if mse == 0 else 10 * np.log10(255.0**2 / mse)
    rows = audit(tag_dir / "out.brbr")
    bytes_csv = tag_dir / "bytes.csv"
    with open(bytes_csv, "w", newline="") as f:
        wcsv = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        wcsv.writeheader()
        wcsv.writerows(rows)
    w, h = Image.open(tag_dir / "in.ppm").size
    meta = {
        "tag": tag_dir.name, "git_sha": git_sha(),
        "utc": datetime.now(timezone.utc).isoformat(), "host": "remote",
        "platform": box, "box": box, "image": os.path.basename(image),
        "quality": quality, "stream_bytes": (tag_dir / "out.brbr").stat().st_size,
        "bpp": 8.0 * (tag_dir / "out.brbr").stat().st_size / (w * h),
        "psnr_db": round(psnr, 3),
        "stream_sha256": hashlib.sha256((tag_dir / "out.brbr").read_bytes()).hexdigest(),
    }
    (tag_dir / "run.json").write_text(json.dumps(meta, indent=2))
    return meta


def merge_timeline(tag_dir: Path) -> None:
    rows = []
    for fn in ["encode.jsonl", "decode.jsonl"]:
        p = tag_dir / fn
        if not p.exists():
            continue
        for line in p.read_text().splitlines():
            line = line.strip()
            if not line:
                continue
            ev = json.loads(line)
            rows.append({
                "phase": ev["phase"], "name": ev["name"], "ns": ev["ns"],
                "cycles": ev.get("cycles", 0), "tid": ev.get("tid", 0),
                "layer": ev.get("a", ""), "band": ev.get("b", ""),
                "channel": ev.get("c", ""), "bytes": ev.get("d", ""),
                "kv": ev.get("kv", ""),
            })
    with open(tag_dir / "timeline.csv", "w", newline="") as f:
        wcsv = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["phase"])
        wcsv.writeheader()
        wcsv.writerows(rows)


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_run = sub.add_parser("run")
    p_run.add_argument("image")
    p_run.add_argument("quality", type=int)
    p_run.add_argument("--tag", default=None)
    p_run.add_argument("--box", default=None)
    p_run.add_argument("--codec", default=None, help="override codec binary path")
    args = ap.parse_args()
    if args.cmd == "run":
        tag = args.tag or (os.path.basename(args.image).split(".")[0] + "_q" + str(args.quality) + "_" + time.strftime("%m%d%H%M%S"))
        tag_dir = TRACK / tag
        tag_dir.mkdir(parents=True, exist_ok=True)
        if args.box:
            meta = box_run(tag_dir, args.image, args.quality, args.box)
        else:
            meta = local_run(tag_dir, args.image, args.quality, args.codec)
        merge_timeline(tag_dir)
        print(json.dumps(meta, indent=2))
        print("artifacts: " + str(tag_dir))


if __name__ == "__main__":
    main()
