#!/usr/bin/env python3
"""Reference entropy audit for CAPS v5 streams (pure python).

Faithfully re-walks the range-coded symbol stream of every chunk (same
contexts, same adaptive model as src/codec.cpp decode_band_arith) and measures
where the bits go:
  - per-pass bit counts (block/sig/sign/unary/rem)
  - cross-entropy of the actual adaptive model (ideal cost of the current
    model = what a perfect arithmetic coder with these probabilities pays)
  - per-context empirical entropy (the bound of an optimal static coder
    with the SAME context structure) -> context-modeling headroom
  - zero-order per-pass entropy (structure-agnostic floor)
Validation: every stream must decode to the exact payload end.

Usage: python3 scripts/entropy_audit.py <stream.brbr> [more streams...]
       python3 scripts/entropy_audit.py --corpus q30 q50 q75  (broad corpus)
"""
from __future__ import annotations
import argparse, csv, json, math, os, struct, subprocess, sys, tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from byte_audit import uvar  # noqa: F401  (kept for potential v1/v2 reuse)

KCTX_BLK = 4


def legacy_layout(version, mode):
    return version < 4 and mode == 3


def sig_idx(version, mode, ctx8, parent_sig):
    if legacy_layout(version, mode): return 8 + ctx8
    if mode in (3, 6, 11): return ctx8
    return ctx8 + (8 if parent_sig else 0)


def sign_idx(version, mode, ctx4, parent_neg):
    if legacy_layout(version, mode): return 8 + ctx4
    if mode == 4: return 16 + ctx4 + (4 if parent_neg else 0)
    return 16 + ctx4


def unary_idx(version, mode, pos, mclass):
    if legacy_layout(version, mode): return 12 + min(pos, 23)
    if mode in (4, 6): return 24 + mclass * 6 + min(pos, 5)
    return 20 + min(pos, 23)


def rem_idx(version, mode, i):
    if legacy_layout(version, mode): return 36 + i
    return (48 if mode in (4, 6) else 44) + i


def sig_context(band, stride, x, y):
    ctx = 0
    if x > 0 and band[y * stride + x - 1] != 0: ctx += 1
    if y > 0 and band[(y - 1) * stride + x] != 0: ctx += 2
    if x > 0 and y > 0 and band[(y - 1) * stride + x - 1] != 0: ctx += 4
    return ctx


def sign_context(band, stride, x, y):
    ctx = 0
    if x > 0 and band[y * stride + x - 1] < 0: ctx += 1
    if y > 0 and band[(y - 1) * stride + x] < 0: ctx += 2
    return ctx


def mag_class(v):
    a = abs(v)
    return 0 if a == 0 else (1 if a == 1 else (2 if a <= 3 else 3))


def median_predict(a, b, c):
    if c >= max(a, b): return min(a, b)
    if c <= min(a, b): return max(a, b)
    return a + b - c


class Dec:
    def __init__(self, data, size, audit):
        self.data, self.size, self.pos = data, size, 0
        self.code, self.range = 0, 0xFFFFFFFF
        self.audit = audit
        for _ in range(5):
            self.code = (self.code << 8) | self.read_byte()

    def read_byte(self):
        if self.pos >= self.size:
            self.pos += 1
            return 0
        b = self.data[self.pos]
        self.pos += 1
        return b

    def bit(self, probs, idx, pass_name, shift=5):
        prob = probs[idx]
        bound = (self.range >> 11) * prob
        if self.code < bound:
            self.range = bound
            p = prob + ((2048 - prob) >> shift)
            probs[idx] = p if p < 2048 else 2047
            b = 0
        else:
            self.code -= bound
            self.range -= bound
            p = prob - (prob >> shift)
            probs[idx] = p if p > 0 else 1
            b = 1
        while self.range < (1 << 24):
            self.code = (self.code << 8) | self.read_byte()
            self.range <<= 8
        self.audit.observe(pass_name, idx, prob, b)
        return b


class Audit:
    def __init__(self):
        # per (pass, ctx): [count0, count1, sum -log2(p)]
        self.ctx = {}
        self.zero = {}   # per pass: [count0, count1]
        self.pass_bits = Counter()

    def observe(self, pass_name, ctx, prob, bit):
        # codec convention: prob is the 11-bit probability of bit == 0
        p = prob / 2048.0
        self.pass_bits[pass_name] += 1
        z = self.zero.setdefault(pass_name, [0, 0])
        z[bit] += 1
        key = (pass_name, ctx)
        c = self.ctx.setdefault(key, [0, 0, 0.0])
        c[bit] += 1
        c[2] += -math.log2(p if bit == 0 else (1.0 - p))

    def totals(self):
        model_bits = sum(c[2] for c in self.ctx.values())
        ctx_entropy = 0.0
        per_pass_ent = {}
        per_pass_model = {}
        for (pn, _ctx), (c0, c1, s) in self.ctx.items():
            n = c0 + c1
            per_pass_model[pn] = per_pass_model.get(pn, 0.0) + s
            if n == 0: continue
            p1 = c1 / n
            e = 0.0
            if 0 < p1 < 1:
                e = -p1 * math.log2(p1) - (1 - p1) * math.log2(1 - p1)
            ctx_entropy += n * e
            per_pass_ent[pn] = per_pass_ent.get(pn, 0.0) + n * e
        zero_entropy = 0.0
        per_pass_zero = {}
        for pn, (c0, c1) in self.zero.items():
            n = c0 + c1
            if n == 0: continue
            p1 = c1 / n
            if 0 < p1 < 1:
                e = n * (-p1 * math.log2(p1) - (1 - p1) * math.log2(1 - p1))
                zero_entropy += e
                per_pass_zero[pn] = e
        return (model_bits, ctx_entropy, zero_entropy, per_pass_ent,
                per_pass_model, per_pass_zero)


def walk_band(dec, probs, audit, dest, stride, tw, th, version, mode,
              use_prediction, parent, parent_stride, parent_w, parent_h,
              pass_bits):
    k = dec.data[0]
    # note: dec was constructed on data[1:], so data[0] here == original k0
    return _walk(dec, probs, audit, dest, stride, tw, th, version, mode,
                 use_prediction, parent, parent_stride, parent_w, parent_h, k)


def parent_at(parent, stride, pw, ph, x, y):
    if parent is None or pw == 0 or ph == 0: return 0
    px = min(x // 2, pw - 1)
    py = min(y // 2, ph - 1)
    return parent[py * stride + px]


def parent_mag_mode(mode):
    return mode in (13, 14)


def energy_bucket_mode(mode):
    return mode == 15


def energy_bucket_count():
    e = os.environ.get("BRUSHIE_EBUCKETS")
    if e and int(e) in (8, 16, 32):
        return int(e)
    return 8


def energy_bucket_sum(band, stride, x, y):
    s = 0
    if x > 0: s += abs(band[y * stride + x - 1])
    if y > 0: s += abs(band[(y - 1) * stride + x])
    if x > 0 and y > 0: s += abs(band[(y - 1) * stride + x - 1])
    return min(s, energy_bucket_count() - 1)


def sig_idx15(bucket, parent_sig):
    return bucket + (energy_bucket_count() if parent_sig else 0)


def unary_shift(version):
    if version < 6:
        return 5
    e = os.environ.get("BRUSHIE_UNARY_RATE")
    if e and int(e) in range(3, 9):
        return int(e)
    return 4


def sig_idx_pm(ctx8, pclass):
    return ctx8 + pclass * 8


def sign_idx_pm(ctx4):
    return 32 + ctx4


def unary_idx_pm(pos, pclass):
    return 36 + pclass * 6 + min(pos, 5)


def rem_idx_pm(i):
    return 60 + i


def parent_block_features(parent, stride, pw, ph, scale_num, scale_den, w, h):
    pclass = [0] * (w * h)
    pmean = [0] * (w * h)
    if parent is None or pw == 0 or ph == 0:
        return pclass, pmean
    for y in range(h):
        py = min(y // 2, ph - 1)
        py1 = min(py + 1, ph - 1)
        for x in range(w):
            px = min(x // 2, pw - 1)
            px1 = min(px + 1, pw - 1)
            s0 = (abs(parent[py * stride + px]) * scale_num) // scale_den
            s1 = (abs(parent[py * stride + px1]) * scale_num) // scale_den
            s2 = (abs(parent[py1 * stride + px]) * scale_num) // scale_den
            s3 = (abs(parent[py1 * stride + px1]) * scale_num) // scale_den
            mx = max(s0, s1, s2, s3)
            i = y * w + x
            pclass[i] = 0 if mx == 0 else (1 if mx == 1 else (2 if mx <= 3 else 3))
            pmean[i] = (s0 + s1 + s2 + s3 + 2) // 4
    return pclass, pmean


def zigzag_residual(m_orig, mp):
    d = m_orig - mp
    return 2 * d if d >= 0 else -2 * d - 1


def _walk(dec, probs, audit, dest, stride, tw, th, version, mode,
          use_prediction, parent, parent_stride, parent_w, parent_h, k0,
          step_c=1, step_p=1):
    k = k0
    mag_sum, mag_count = 0, 0
    use_parent = parent is not None
    if os.environ.get("BRUSHIE_AUDIT_STATS"):
        _walk.stat_out = []
        _walk.stat_par = parent
        _walk.stat_pw = parent_w
        _walk.stat_ph = parent_h
        _walk.stat_pstride = parent_stride
        _walk.stat_sp = step_p
        _walk.stat_sc = step_c
    pclass_arr, pmean_arr = [], []
    if (parent_mag_mode(mode) or os.environ.get("BRUSHIE_AUDIT_STATS")) and use_parent:
        pclass_arr, pmean_arr = parent_block_features(
            parent, parent_stride, parent_w, parent_h, step_p, step_c, tw, th)
    if mode in (12, 21, 22, 23):
        kB = 21 if False else (8 if mode == 21 else (32 if mode == 22 else (64 if mode == 23 else 16)))
        bw, bh = (tw + kB - 1) // kB, (th + kB - 1) // kB
        block_nz = [0] * (bw * bh)
        for by in range(bh):
            for bx in range(bw):
                bctx = 0
                if bx > 0 and block_nz[by * bw + bx - 1]: bctx += 1
                if by > 0 and block_nz[(by - 1) * bw + bx]: bctx += 2
                block_nz[by * bw + bx] = dec.bit(probs, KCTX_BLK + bctx, "blk")
        for by in range(bh):
            for bx in range(bw):
                if not block_nz[by * bw + bx]: continue
                for yy in range(by * kB, min(th, (by + 1) * kB)):
                    for xx in range(bx * kB, min(tw, (bx + 1) * kB)):
                        x, y = xx, yy
                        pv = parent_at(parent, parent_stride, parent_w, parent_h, x, y)
                        raw_ctx = sig_context(dest, stride, x, y)
                        s = dec.bit(probs, sig_idx(version, mode, raw_ctx, use_parent and pv != 0), "sig")
                        q = 0
                        if s:
                            sc = sign_idx(version, mode, sign_context(dest, stride, x, y),
                                          use_parent and pv < 0)
                            sign = dec.bit(probs, sc, "sign")
                            qq = 0
                            kk = k
                            lv8 = dest[y * stride + x - 1] if x > 0 else 0
                            av8 = dest[(y - 1) * stride + x] if y > 0 else 0
                            ml = abs(lv8); ma = abs(av8)
                            mean1 = (ml + ma) // 2 + 1
                            nk = 0
                            while nk < 12 and (1 << (nk + 1)) <= mean1: nk += 1
                            kk = max(nk, k)
                            while True:
                                uctx = unary_idx(version, mode, qq, 0)
                                if dec.bit(probs, uctx, "unary", unary_shift(version)): break
                                qq += 1
                            m = qq << kk
                            for i in range(kk):
                                m |= dec.bit(probs, rem_idx(version, mode, i), "rem") << i
                            q = m + 1
                            if sign: q = -q
                            mag_sum += m
                            mag_count += 1
                            if mag_count == 64:
                                v = mag_sum // 64
                                nk2 = 0
                                while nk2 < 12 and (1 << (nk2 + 1)) <= v: nk2 += 1
                                k = nk2; mag_sum = 0; mag_count = 0
                        if use_prediction:
                            a = dest[y * stride + x - 1] if x > 0 else 0
                            b = dest[(y - 1) * stride + x] if y > 0 else 0
                            c = dest[(y - 1) * stride + x - 1] if (x > 0 and y > 0) else 0
                            p = a if y == 0 else (b if x == 0 else median_predict(a, b, c))
                            q += p
                        dest[y * stride + x] = q
        return
    if mode == 16:
        # flat-block base mode (v6): flags 0..3, block-value sig 4..7 +
        # sign 57..60, non-flat coeffs sig 8..15 / sign 16..19, unary
        # 20..43, rem 44..56, separate vk/k adaptation.
        kB = 4
        bw, bh = (tw + kB - 1) // kB, (th + kB - 1) // kB
        flat = [0] * (bw * bh)
        bval = [0] * (bw * bh)
        bres = [0] * (bw * bh)
        for by in range(bh):
            for bx in range(bw):
                fctx = 0
                if bx > 0 and flat[by * bw + bx - 1]: fctx += 1
                if by > 0 and flat[(by - 1) * bw + bx]: fctx += 2
                flat[by * bw + bx] = dec.bit(probs, fctx, "blk16")
        vk = 0
        v_mag_sum, v_mag_count = 0, 0
        for by in range(bh):
            for bx in range(bw):
                idx = by * bw + bx
                y0, x0 = by * kB, bx * kB
                y1, x1 = min(th, y0 + kB), min(tw, x0 + kB)
                if flat[idx]:
                    pv = 0
                    if bx > 0 and flat[idx - 1]: pv = bval[idx - 1]
                    elif by > 0 and flat[idx - bw]: pv = bval[idx - bw]
                    lr = bres[idx - 1] if (bx > 0 and flat[idx - 1]) else 0
                    ar = bres[idx - bw] if (by > 0 and flat[idx - bw]) else 0
                    vctx = 4 + (1 if lr != 0 else 0) + (2 if ar != 0 else 0)
                    rv = 0
                    if dec.bit(probs, vctx, "sig16v"):
                        sctx = 57 + (1 if lr < 0 else 0) + (2 if ar < 0 else 0)
                        sign = dec.bit(probs, sctx, "sign16v")
                        mean1 = (abs(lr) + abs(ar)) // 2 + 1
                        nk = 0
                        while nk < 12 and (1 << (nk + 1)) <= mean1: nk += 1
                        kk = max(nk, vk)
                        qq = 0
                        while True:
                            if dec.bit(probs, unary_idx(version, mode, qq, 0), "unary"): break
                            qq += 1
                        m = qq << kk
                        for i in range(kk):
                            m |= dec.bit(probs, rem_idx(version, mode, i), "rem") << i
                        rv = m + 1
                        if sign: rv = -rv
                        v_mag_sum += m
                        v_mag_count += 1
                        if v_mag_count == 32:
                            nk2 = 0
                            vv = v_mag_sum // 32
                            while nk2 < 12 and (1 << (nk2 + 1)) <= vv: nk2 += 1
                            vk = nk2; v_mag_sum = 0; v_mag_count = 0
                    bres[idx] = rv
                    bval[idx] = pv + rv
                    for yy in range(y0, y1):
                        for xx in range(x0, x1):
                            dest[yy * stride + xx] = bval[idx]
                    continue
                for yy in range(y0, y1):
                    for xx in range(x0, x1):
                        s = dec.bit(probs, 8 + sig_context(dest, stride, xx, yy), "sig")
                        q = 0
                        if s:
                            sign = dec.bit(probs, 16 + sign_context(dest, stride, xx, yy), "sign")
                            qq = 0
                            while True:
                                if dec.bit(probs, unary_idx(version, mode, qq, 0), "unary"): break
                                qq += 1
                            m = qq << k
                            for i in range(k):
                                m |= dec.bit(probs, rem_idx(version, mode, i), "rem") << i
                            q = m + 1
                            if sign: q = -q
                            mag_sum += m
                            mag_count += 1
                            if mag_count == 64:
                                v = mag_sum // 64
                                nk = 0
                                while nk < 12 and (1 << (nk + 1)) <= v: nk += 1
                                k = nk; mag_sum = 0; mag_count = 0
                        a = dest[yy * stride + xx - 1] if xx > 0 else 0
                        b = dest[(yy - 1) * stride + xx] if yy > 0 else 0
                        c = dest[(yy - 1) * stride + xx - 1] if (xx > 0 and yy > 0) else 0
                        p = a if yy == 0 else (b if xx == 0 else median_predict(a, b, c))
                        q += p
                        dest[yy * stride + xx] = q
        return
    for y in range(th):
        for x in range(tw):
            pv = parent_at(parent, parent_stride, parent_w, parent_h, x, y)
            raw_ctx = sig_context(dest, stride, x, y)
            fi = y * stride + x
            if mode == 11 and use_parent:
                s = dec.bit(probs, raw_ctx if pv == 0 else sig_idx(version, mode, raw_ctx, False), "sig")
            elif parent_mag_mode(mode) and use_parent:
                s = dec.bit(probs, sig_idx_pm(raw_ctx, pclass_arr[fi]), "sig")
            elif energy_bucket_mode(mode) and use_parent:
                s = dec.bit(probs, sig_idx15(energy_bucket_sum(dest, stride, x, y), pv != 0), "sig")
            else:
                s = dec.bit(probs, sig_idx(version, mode, raw_ctx, use_parent and pv != 0), "sig")
            q = 0
            if s:
                if parent_mag_mode(mode) and use_parent:
                    sc = sign_idx_pm(sign_context(dest, stride, x, y))
                else:
                    sc = sign_idx(version, mode, sign_context(dest, stride, x, y),
                                  use_parent and pv < 0)
                sign = dec.bit(probs, sc, "sign")
                qq = 0
                kk = k
                if parent_mag_mode(mode) and use_parent:
                    if mode == 14:
                        rl = ra = 0
                        if x > 0 and dest[fi - 1] != 0:
                            dl = (abs(dest[fi - 1]) - 1) - pmean_arr[fi - 1]
                            rl = 2 * dl if dl >= 0 else -2 * dl - 1
                        if y > 0 and dest[fi - stride] != 0:
                            da = (abs(dest[fi - stride]) - 1) - pmean_arr[fi - stride]
                            ra = 2 * da if da >= 0 else -2 * da - 1
                        mean1 = (rl + ra) // 2 + 1
                    else:
                        lv13 = dest[fi - 1] if x > 0 else 0
                        av13 = dest[fi - stride] if y > 0 else 0
                        mean1 = (abs(lv13) + abs(av13)) // 2 + 1
                    nk = 0
                    while nk < 12 and (1 << (nk + 1)) <= mean1: nk += 1
                    kk = max(nk, k)
                elif mode in (8, 9):
                    lv8 = dest[y * stride + x - 1] if x > 0 else 0
                    av8 = dest[(y - 1) * stride + x] if y > 0 else 0
                    ml = abs(lv8); ma = abs(av8)
                    mean1 = (ml + ma) // 2 + 1
                    nk = 0
                    while nk < 12 and (1 << (nk + 1)) <= mean1: nk += 1
                    kk = max(nk, k)
                while True:
                    if parent_mag_mode(mode) and use_parent:
                        uctx = unary_idx_pm(qq, pclass_arr[fi])
                    elif mode in (4, 6):
                        lv = dest[y * stride + x - 1] if x > 0 else 0
                        av = dest[(y - 1) * stride + x] if y > 0 else 0
                        mclass = min(mag_class(lv), mag_class(av))
                        uctx = unary_idx(version, mode, qq, mclass)
                    else:
                        uctx = unary_idx(version, mode, qq, 0)
                    if dec.bit(probs, uctx, "unary", unary_shift(version)): break
                    qq += 1
                m = qq << kk
                for i in range(kk):
                    if parent_mag_mode(mode) and use_parent:
                        m |= dec.bit(probs, rem_idx_pm(i), "rem") << i
                    else:
                        m |= dec.bit(probs, rem_idx(version, mode, i), "rem") << i
                m_decoded = m
                if parent_mag_mode(mode) and mode == 14 and use_parent:
                    d = (m + (m & 1)) >> 1
                    neg = (m & 1) != 0
                    m = pmean_arr[fi] - d if neg else pmean_arr[fi] + d
                if mode == 9:
                    lv9 = dest[y * stride + x - 1] if x > 0 else 0
                    av9 = dest[(y - 1) * stride + x] if y > 0 else 0
                    mp = min(abs(lv9), abs(av9))
                    d = (m + (m & 1)) >> 1
                    neg = (m & 1) != 0
                    m = mp - d if neg else mp + d
                q = m + 1
                if sign: q = -q
                if parent_mag_mode(mode) and mode == 14 and use_parent:
                    mag_sum += m_decoded
                else:
                    mag_sum += m
                mag_count += 1
                if mag_count == 64:
                    v = mag_sum // 64
                    nk = 0
                    while nk < 12 and (1 << (nk + 1)) <= v: nk += 1
                    k = nk; mag_sum = 0; mag_count = 0
                if os.environ.get("BRUSHIE_AUDIT_STATS") and pmean_arr:
                    rec = {"pclass": pclass_arr[fi], "pmean": pmean_arr[fi],
                           "m": abs(q) - 1, "sig": 1}
                    if _walk.stat_par is not None:
                        par = _walk.stat_par
                        pw = _walk.stat_pw; ph = _walk.stat_ph
                        px = min(x // 2, pw - 1); py = min(y // 2, ph - 1)
                        px1 = min(px + 1, pw - 1); py1 = min(py + 1, ph - 1)
                        s00 = (abs(par[py * _walk.stat_pstride + px]) * _walk.stat_sp) // _walk.stat_sc
                        s10 = (abs(par[py * _walk.stat_pstride + px1]) * _walk.stat_sp) // _walk.stat_sc
                        s01 = (abs(par[py1 * _walk.stat_pstride + px]) * _walk.stat_sp) // _walk.stat_sc
                        s11 = (abs(par[py1 * _walk.stat_pstride + px1]) * _walk.stat_sp) // _walk.stat_sc
                        rec.update({"gx": abs(s10 - s00), "gy": abs(s01 - s00),
                                    "gd": abs(s11 - s00), "gmx": max(s00, s10, s01, s11)})
                    _walk.stat_out.append(rec)
            if use_prediction:
                a = dest[y * stride + x - 1] if x > 0 else 0
                b = dest[(y - 1) * stride + x] if y > 0 else 0
                c = dest[(y - 1) * stride + x - 1] if (x > 0 and y > 0) else 0
                p = a if y == 0 else (b if x == 0 else median_predict(a, b, c))
                q += p
            dest[y * stride + x] = q


def walk_shapes(w, h, levels):
    out = []
    cw, chh = w, h
    for _ in range(levels):
        out.append((cw, chh, (cw + 1) // 2, (chh + 1) // 2))
        cw = (cw + 1) // 2
        chh = (chh + 1) // 2
    return out, (cw, chh)


def _dump_stats(path, channel, layer, band):
    rows = [{"channel": channel, "layer": layer, "band": band, **s}
            for s in _walk.stat_out]
    _walk.stat_out = []
    out = os.environ.get("BRUSHIE_AUDIT_STATS_FILE",
                         str(path) + ".stats.jsonl")
    with open(out, "a") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")


def audit_stream(path: Path):
    data = path.read_bytes()
    assert data[:4] == b"CAPS"
    version = struct.unpack_from("<H", data, 4)[0]
    flags = struct.unpack_from("<H", data, 6)[0]
    subsampled = (flags & 1) != 0
    src_w, src_h = struct.unpack_from("<II", data, 8)
    levels = struct.unpack_from("<H", data, 16)[0]
    base_w, base_h = struct.unpack_from("<II", data, 24)
    chunk_count = struct.unpack_from("<I", data, 32)[0]
    dir_bytes = struct.unpack_from("<I", data, 36)[0]
    data_offset = struct.unpack_from("<Q", data, 40)[0]
    c_base_w, c_base_h = struct.unpack_from("<II", data, 48)
    channels = 4 if data[21] == 4 else 3
    entry = 16 if version >= 5 else (20 if version >= 3 else 40)
    assert dir_bytes == chunk_count * entry
    shapes0, luma_base = walk_shapes(src_w, src_h, levels)
    assert luma_base == (base_w, base_h), (luma_base, (base_w, base_h))
    # chroma shapes
    if subsampled:
        c_in_w, c_in_h = (src_w + 1) // 2, (src_h + 1) // 2
        shapes1 = []
        w, h = c_in_w, c_in_h
        while (w, h) != (c_base_w, c_base_h):
            shapes1.append((w, h, (w + 1) // 2, (h + 1) // 2))
            w, h = (w + 1) // 2, (h + 1) // 2
        shapes2 = list(shapes1)
    else:
        shapes1, _ = walk_shapes(src_w, src_h, levels)
        shapes2, _ = walk_shapes(src_w, src_h, levels)
    shapes = [shapes0, shapes1, shapes2]
    if channels == 4:
        shapes3, _ = walk_shapes(src_w, src_h, levels)
        shapes.append(shapes3)
    # storage: base bands + per-level detail arrays per channel
    base_store = {0: [0] * (base_w * base_h)}
    ch_bw = c_base_w if subsampled else base_w
    ch_bh = c_base_h if subsampled else base_h
    base_store[1] = [0] * (ch_bw * ch_bh)
    base_store[2] = [0] * (ch_bw * ch_bh)
    if channels == 4: base_store[3] = [0] * (base_w * base_h)
    detail = {}
    for ci, sh in enumerate(shapes):
        detail[ci] = []
        for (w, h, lw, lh) in sh:
            detail[ci].append({
                1: [0] * ((w // 2) * lh),
                2: [0] * (lw * (h // 2)),
                3: [0] * ((w // 2) * (h // 2)),
            })
    rows = []
    steps = {}  # (channel, layer, band) -> step, for parent scaling in modes 13/14
    cumulative = data_offset
    for ci in range(chunk_count):
        d = data[64 + ci * entry:64 + (ci + 1) * entry]
        layer, band, channel, mode = d[0], d[1], d[2], d[3]
        tw, th = struct.unpack_from("<HH", d, 4)
        step = struct.unpack_from("<H", d, 8)[0]
        psize = struct.unpack_from("<I", d, 12)[0]
        poff = cumulative
        cumulative += psize
        assert layer <= levels and channel < channels
        payload = data[poff:poff + psize]
        audit = Audit()
        probs = [2047] * 128  # fresh per decode_band_arith call; band-4 resets per section below
        sh = shapes[channel]
        row = {"layer": layer, "band": band, "channel": channel, "mode": mode,
               "payload_bytes": psize, "k0": -1}
        if band == 4:
            step_d = struct.unpack_from("<H", payload, 0)[0]
            modes = (payload[2], payload[3], payload[4])
            size_h = struct.unpack_from("<I", payload, 5)[0]
            size_v = struct.unpack_from("<I", payload, 9)[0]
            idx = len(sh) - layer
            (w, h, lw, lh) = sh[idx]
            dims = [(w // 2, lh), (lw, h // 2), (w // 2, h // 2)]
            cursor = 13
            secs = [size_h, size_v, psize - 13 - size_h - size_v]
            for bi in range(3):
                if modes[bi] == 0:
                    continue
                sec = payload[cursor:cursor + secs[bi]]
                cursor += secs[bi]
                probs = [2047] * 128  # each section is its own decode_band_arith call
                sec_step = step if bi < 2 else step_d
                # parent: base for layer 1, else same band one level coarser
                if layer == 1:
                    par = base_store[channel]
                    pw = ch_bw if channel in (1, 2) and subsampled else base_w
                    ph = ch_bh if channel in (1, 2) and subsampled else base_h
                else:
                    pl = sh[len(sh) - (layer - 1)]
                    par = detail[channel][len(sh) - (layer - 1)][bi + 1]
                    pw = (pl[0] // 2) if bi != 1 else pl[2]
                    ph = pl[3] if bi == 0 else (pl[1] // 2)
                par_step = steps.get((channel, 0, 0), 1) if layer == 1 \
                    else steps.get((channel, layer - 1, bi + 1), 1)
                dec = Dec(sec[1:], len(sec) - 1, audit)
                row["k0"] = sec[0] if bi == 0 else row["k0"]
                _walk(dec, probs, audit, detail[channel][idx][bi + 1],
                      dims[bi][0], dims[bi][0], dims[bi][1], version,
                      modes[bi], False, par, pw, pw, ph, sec[0],
                      step_c=sec_step, step_p=par_step)
                if os.environ.get("BRUSHIE_AUDIT_STATS"):
                    _dump_stats(path, channel, layer, bi + 1)
                assert dec.pos == len(sec) - 1, f"chunk {ci} sec {bi} consumed {dec.pos}/{len(sec)-1}"
                steps[(channel, layer, bi + 1)] = sec_step
        else:
            if layer == 0:
                bw = base_w if channel in (0, 3) else ch_bw
                bh = base_h if channel in (0, 3) else ch_bh
                dest = base_store[channel]
                stride = bw
            else:
                idx = len(sh) - layer
                (w, h, lw, lh) = sh[idx]
                bw = (w // 2) if band == 1 else (lw if band == 2 else w // 2)
                bh = lh if band == 1 else (h // 2 if band in (2, 3) else h // 2)
                dest = detail[channel][idx][band]
                stride = bw
            assert (bw, bh) == (tw, th), ((bw, bh), (tw, th), layer, band, channel)
            par = None
            pw = ph = 0
            if mode != 3 and layer > 0:
                if layer == 1:
                    par = base_store[channel]
                    pw = ch_bw if channel in (1, 2) and subsampled else base_w
                    ph = ch_bh if channel in (1, 2) and subsampled else base_h
                else:
                    pl = sh[len(sh) - (layer - 1)]
                    par = detail[channel][len(sh) - (layer - 1)][band]
                    pw = (pl[0] // 2) if band == 1 else (pl[2] if band == 2 else pl[0] // 2)
                    ph = pl[3] if band == 1 else (pl[1] // 2 if band in (2, 3) else pl[1] // 2)
            dec = Dec(payload[1:], psize - 1, audit)
            row["k0"] = payload[0]
            par_step = (steps.get((channel, 0, 0), 1) if layer == 1
                        else steps.get((channel, layer - 1, band), 1)) \
                if (mode != 3 and layer > 0) else 1
            _walk(dec, probs, audit, dest, stride, tw, th, version, mode,
                  band == 0, par, pw, pw, ph, payload[0],
                  step_c=step, step_p=par_step)
            if os.environ.get("BRUSHIE_AUDIT_STATS"):
                _dump_stats(path, channel, layer, band)
            assert dec.pos == psize - 1, f"chunk {ci} consumed {dec.pos}/{psize-1}"
            steps[(channel, layer, band)] = step
        mb, ce, ze, ppe, ppm, ppz = audit.totals()
        row.update({"pass_bits": dict(audit.pass_bits), "model_bits": mb,
                    "ctx_entropy_bits": ce, "zero_entropy_bits": ze})
        for pn in ("blk", "sig", "sign", "unary", "rem"):
            row[f"ent_{pn}"] = ppe.get(pn, 0.0)
            row[f"model_{pn}"] = ppm.get(pn, 0.0)
            row[f"zero_{pn}"] = ppz.get(pn, 0.0)
        rows.append(row)
    assert cumulative == len(data), f"trailing bytes: {cumulative} vs {len(data)}"
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("streams", nargs="*")
    ap.add_argument("--corpus", nargs="*", type=int, default=[])
    args = ap.parse_args()
    paths = [Path(s) for s in args.streams]
    if args.corpus:
        from PIL import Image
        cli = ROOT / "build" / "brushie"
        corpus = ROOT / "benchmarks" / "corpus.txt"
        tmp = Path(tempfile.mkdtemp(prefix="entropy-"))
        for img in [l.strip() for l in corpus.read_text().splitlines() if l.strip() and not l.startswith("#")][:40]:
            im = Image.open(img).convert("RGB")
            im.thumbnail((512, 512), Image.Resampling.LANCZOS)
            ppm = tmp / (Path(img).stem + ".ppm")
            im.save(ppm)
            for q in args.corpus:
                st = tmp / f"{Path(img).stem}_q{q}.brbr"
                subprocess.run([str(cli), "encode", str(ppm), str(st), "--quality", str(q)],
                               check=True, capture_output=True)
                paths.append(st)
    out_rows = []
    for p in paths:
        try:
            rows = audit_stream(p)
        except AssertionError as e:
            print(f"FAIL {p.name}: {e}")
            continue
        for r in rows:
            r["file"] = p.name
            out_rows.append(r)
        tot_mb = sum(r["model_bits"] for r in rows)
        tot_pb = sum(r["payload_bytes"] for r in rows) * 8
        print(f"{p.name}: payload {tot_pb/8:,.0f}B  model_bits {tot_mb:,.0f} "
              f"({tot_pb-tot_mb:+.0f} coder overhead)")
    out = ROOT / "campaign" / "entropy_audit.csv"
    rows = []
    for r in out_rows:
        pb = r.pop("pass_bits")
        for k, v in pb.items():
            r[f"bits_{k}"] = v
        rows.append(r)
    if not rows:
        print("no valid rows; nothing written")
        return
    keys = sorted({k for r in rows for k in r.keys()})
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader(); w.writerows(rows)
    print("wrote", out)


if __name__ == "__main__":
    main()
