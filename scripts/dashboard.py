#!/usr/bin/env python3
"""Matplotlib progress dashboard served on localhost:3567.

Reads dash/progress_history.json (written by the iteration loop) and renders
a PNG with per-gate byte curves, jump annotations (delta + note), and a
CAPS-vs-competitors bar panel at the current iteration.
"""
from __future__ import annotations
import json, os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

os.environ.pop("MPLBACKEND", None)  # kernel leaks module://matplotlib_inline; Agg required
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HISTORY = ROOT / "dash" / "progress_history.json"
PORT = 3567
GATES = (0.970, 0.985, 0.995)
COMPETITORS = {  # mean bytes at each gate, from the clean harness-v3 quick run
    "AVIF": {0.970: 6466, 0.985: 12948, 0.995: 31810},
    "WebP": {0.970: 7658, 0.985: 13901, 0.995: 65498},
    "JPEG": {0.970: 10140, 0.985: 16328, 0.995: 32427},
    "J2K":  {0.970: 10046, 0.985: 19084, 0.995: 44377},
}

def load_history():
    if not HISTORY.exists():
        return []
    return json.loads(HISTORY.read_text())

def load_broad():
    p = ROOT / "dash" / "broad.json"
    if not p.exists():
        return None
    return json.loads(p.read_text())

def render():
    history = load_history()
    if not history:
        return None
    broad = load_broad()
    fig = plt.figure(figsize=(17, 13 if broad else 10))
    if broad:
        gs = fig.add_gridspec(3, 1, height_ratios=[1.7, 1.0, 0.9], hspace=0.42)
    else:
        gs = fig.add_gridspec(2, 1, height_ratios=[2.1, 1.0], hspace=0.32)
    ax = fig.add_subplot(gs[0])
    ax2 = fig.add_subplot(gs[1])
    ax3 = fig.add_subplot(gs[2]) if broad else None

    colors = {0.970: "#1f77b4", 0.985: "#ff7f0e", 0.995: "#d62728"}
    xs = list(range(len(history)))
    for gate in GATES:
        ys = [e["gates"][f"{gate:.3f}"] for e in history]
        ax.plot(xs, ys, "-o", color=colors[gate], lw=2.2, ms=7, label=f"CAPS gate {gate}")
        # competitor reference lines
        for name, comp in COMPETITORS.items():
            ax.axhline(comp[gate], ls=":", lw=1.0, color=colors[gate], alpha=0.35)
            ax.text(len(history) - 0.5, comp[gate], f" {name} {comp[gate]:,}", fontsize=7,
                    color=colors[gate], va="center", alpha=0.75)
        # jump annotations
        for i, e in enumerate(history):
            if i == 0:
                continue
            prev = history[i - 1]["gates"][f"{gate:.3f}"]
            cur = e["gates"][f"{gate:.3f}"]
            delta = (cur - prev) / prev * 100.0
            note = e.get("notes", {}).get(f"{gate:.3f}", "")
            label = f"{delta:+.1f}%"
            if note:
                label += f"\n{note[:42]}"
            ax.annotate(label, (i, cur), textcoords="offset points",
                        xytext=(0, 10 if delta <= 0 else -16),
                        ha="center", fontsize=7.5, color=colors[gate],
                        arrowprops=dict(arrowstyle="-", lw=0.4, color=colors[gate], alpha=0.4))
    ax.set_yscale("log")
    ax.set_ylabel("mean bytes @ gate (log)")
    ax.set_xlabel("iteration")
    ax.set_title("Brushie CAPS recursion — mean bytes at equal windowed MS-SSIM gates", fontsize=13)
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(loc="upper left", fontsize=9)
    ax.set_xticks(xs)
    ax.set_xticklabels([e.get("tag", str(i)) for i, e in enumerate(history)], rotation=30, ha="right", fontsize=7)

    # Bar panel: current CAPS vs competitors at each gate (log scale).
    cur = history[-1]
    xpos = np.arange(len(GATES))
    width = 0.14
    for j, (name, comp) in enumerate(COMPETITORS.items()):
        vals = [comp[g] for g in GATES]
        ax2.bar(xpos + (j - 1.5) * width, vals, width, label=name, alpha=0.8)
    caps_vals = [cur["gates"][f"{g:.3f}"] for g in GATES]
    ax2.bar(xpos + 1.5 * width, caps_vals, width, label="CAPS (current)", color="#2ca02c", alpha=0.95)
    for j, g in enumerate(GATES):
        ax2.text(xpos[j] + 1.5 * width, caps_vals[j] * 1.08, f"{caps_vals[j]:,}", ha="center", fontsize=8, color="#2ca02c")
        ax2.text(xpos[j] - 1.5 * width, 6000, f"gate {g}", ha="center", fontsize=8, fontweight="bold")
    ax2.set_yscale("log")
    ax2.set_ylabel("mean bytes (log)")
    ax2.set_xticks(xpos)
    ax2.set_xticklabels([f"{g}" for g in GATES])
    ax2.legend(loc="upper left", fontsize=8, ncol=5)
    ax2.grid(True, which="both", alpha=0.25)
    ax2.set_title("Current iteration vs competitors (lower is better)", fontsize=11)

    if broad is not None and ax3 is not None:
        gates = broad.get("gates", {})
        xs = np.arange(len(gates))
        width = 0.14
        codecs = ["AVIF", "WebP", "JPEG", "JPEG2000", "CAPS"]
        colors = {"AVIF": "#1f77b4", "WebP": "#ff7f0e", "JPEG": "#2ca02c",
                  "JPEG2000": "#d62728", "CAPS": "#9467bd"}
        for j, codec in enumerate(codecs):
            vals = []
            for g in gates.values():
                entry = g.get(codec)
                vals.append(entry["mean_bytes"] if entry else 0)
            ax3.bar(xs + (j - 2) * width, vals, width, label=codec,
                    color=colors[codec], alpha=0.85)
        for i, g in enumerate(gates):
            ax3.text(i, 5000, f"gate {g}", ha="center", fontsize=8, fontweight="bold")
        ax3.set_yscale("log")
        ax3.set_ylabel("mean bytes (log)")
        ax3.set_xticks(xs)
        ax3.set_xticklabels([f"{broad.get('samples', 0)} imgs"] + [""] * (len(gates) - 1))
        ax3.legend(loc="upper left", fontsize=8, ncol=5)
        ax3.grid(True, which="both", alpha=0.25)
        title = f"BROAD CORPUS ({broad.get('samples', 0)} public-benchmark images, cached competitors)"
        speeds = broad.get("speeds", {})
        if speeds:
            sp = speeds.get("CAPS", {})
            title += f" | CAPS enc {sp.get('encode_ms', '?')}ms / dec {sp.get('decode_ms', '?')}ms"
        ax3.set_title(title, fontsize=11)

    # Honest-metric panel: CAPS vs best-per-gate on SSIMULACRA2/Butteraugli.
    honest_path = ROOT / "dash" / "honest.json"
    if broad is not None and honest_path.exists():
        honest = json.loads(honest_path.read_text())
        metrics = honest.get("metrics", {})
        if metrics:
            fig2 = plt.figure(figsize=(17, 6.5))
            s2 = [k for k in metrics if k.startswith("ssimulacra2")]
            ba = [k for k in metrics if k.startswith("butteraugli")]
            for pi, group in enumerate([s2, ba]):
                axh = fig2.add_subplot(1, 2, pi + 1)
                gates = group
                xs = np.arange(len(gates))
                codecs = ["CAPS", "JPEG XL", "AVIF", "WebP", "JPEG"]
                ccolors = {"CAPS": "#2ca02c", "JPEG XL": "#9467bd", "AVIF": "#1f77b4",
                           "WebP": "#ff7f0e", "JPEG": "#7f7f7f"}
                width = 0.15
                for j, codec in enumerate(codecs):
                    vals = []
                    for g in gates:
                        e = metrics[g].get(codec)
                        vals.append(e["mean_bytes"] if e else 0)
                    axh.bar(xs + (j - 2) * width, vals, width, label=codec,
                            color=ccolors[codec], alpha=0.85)
                axh.set_yscale("log")
                axh.set_xticks(xs)
                axh.set_xticklabels([g.split()[-1] for g in gates])
                axh.set_xlabel("gate (higher = better)" if pi == 0 else "gate (lower = better)")
                axh.set_ylabel("mean bytes (log)")
                axh.set_title(("SSIMULACRA2 gates (163 imgs, real metric)" if pi == 0
                               else "Butteraugli 3-norm gates (163 imgs, real metric)"), fontsize=11)
                axh.legend(loc="upper left", fontsize=8, ncol=5)
                axh.grid(True, which="both", alpha=0.25)
                for j, g in enumerate(gates):
                    caps_e = metrics[g].get("CAPS")
                    if caps_e:
                        axh.text(j - 2 * width, caps_e["mean_bytes"] * 1.15,
                                 f"{caps_e['mean_bytes']:,}", ha="center",
                                 fontsize=7.5, color="#2ca02c", fontweight="bold")
            fig2.savefig(ROOT / "dash" / "honest.png", dpi=110, bbox_inches="tight")
            plt.close(fig2)

    out = ROOT / "dash" / "progress.png"
    fig.savefig(out, dpi=110, bbox_inches="tight")
    plt.close(fig)
    return out

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            history = load_history()
            rows = "".join(
                f"<tr><td>{e.get('tag','')}</td><td>{e.get('timestamp','')}</td>"
                f"<td>{e['gates']['0.970']:,}</td><td>{e['gates']['0.985']:,}</td>"
                f"<td>{e['gates']['0.995']:,}</td><td>{e.get('notes',{}).get('_summary','')}</td></tr>"
                for e in history
            )
            body = f"""<html><head><title>brushie recursion</title></head>
<body style="font-family:system-ui;background:#111;color:#eee;margin:24px">
<h2>Brushie CAPS recursion — <a href="/progress.png">graph</a></h2>
<img src="/progress.png" style="max-width:100%;border:1px solid #333;border-radius:8px">
<h2>Real perceptual metrics (SSIMULACRA2 + Butteraugli, 163 images)</h2>
<img src="/honest.png" style="max-width:100%;border:1px solid #333;border-radius:8px">
<table border="1" cellspacing="0" cellpadding="5" style="margin-top:18px;border-collapse:collapse">
<tr><th>tag</th><th>time</th><th>.970</th><th>.985</th><th>.995</th><th>note</th></tr>{rows}</table>
</body></html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body.encode())
        elif self.path == "/progress.png":
            out = render()
            if out is None:
                self.send_response(404); self.end_headers(); return
            data = out.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
        elif self.path == "/honest.png":
            p = ROOT / "dash" / "honest.png"
            if not p.exists():
                self.send_response(404); self.end_headers(); return
            data = p.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
        else:
            self.send_response(404); self.end_headers()
    def log_message(self, *a):
        pass

def main():
    render()
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"dashboard on http://localhost:{PORT}")
    server.serve_forever()

if __name__ == "__main__":
    main()
