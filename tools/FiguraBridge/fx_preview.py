#!/usr/bin/env python3
"""Offline preview of the stick armor's match-moment particles (stick_tiers.FX): simulates each emitter the way the
particle JSON describes it and draws it over a real in-game screenshot, side view, a few moments in time.
usage: fx_preview.py BACKGROUND.jpg out.png"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).parent))
import stick_tiers as T   # noqa: E402

ATL = np.asarray(T.fx_atlas(), float) / 255


def sim(mat, ev, t, seed=3):
    cell, n, shape, speed, life, grav, size, cmode = T.FX[mat][ev]
    rng = np.random.default_rng(seed)
    out = []
    for i in range(n):
        r1, r2, r3, r4 = rng.random(4)
        L = life * (0.75 + 0.5 * r2)
        if t > L:
            continue
        sp = speed * (0.6 + 0.6 * r3)
        th, ph = rng.uniform(0, 2 * math.pi), math.acos(rng.uniform(-1, 1))
        dvec = np.array([math.sin(ph) * math.cos(th), math.cos(ph), math.sin(ph) * math.sin(th)])
        if shape == "sphere":
            p0 = np.array([0, 1.0, 0]) + dvec * 0.35 * rng.random() ** (1 / 3); v0 = dvec * sp
        elif shape == "disc":
            d2 = np.array([math.cos(th), 0, math.sin(th)]); p0 = np.array([0, .1, 0]) + d2 * .4; v0 = d2 * sp
        elif shape == "up":
            rr = .55 * math.sqrt(rng.random()); p0 = np.array([rr * math.cos(th), .2, rr * math.sin(th)]); v0 = np.array([0, sp, 0])
        elif shape == "shell":
            p0 = np.array([0, 1.0, 0]) + dvec; v0 = dvec * sp
        elif shape == "in":
            p0 = np.array([0, 1.0, 0]) + dvec * 1.6; v0 = -dvec * sp
        elif shape == "spiral":
            p0 = np.array([.7 * math.cos(th), .2, .7 * math.sin(th)]); v0 = np.array([0, sp, 0])
        else:
            p0 = np.array([0, .08, 0]); v0 = np.zeros(3)
        p, v = p0.copy(), v0.copy()
        dt = 1 / 120
        for k in range(int(t / dt)):
            age = k * dt
            a = np.array([0, grav, 0], float)
            if shape == "spiral":
                ang = math.radians(age * 400 + r1 * 360)
                a = np.array([math.cos(ang) * 6, grav, math.sin(ang) * 6])
            v += (a - 0.6 * v) * dt
            p += v * dt
        f = t / L
        if shape == "point":
            sz = size * (0.2 + 1.2 * f)
        else:
            sz = size * (1 - 0.6 * f) * (0.7 + 0.6 * r4)
        alpha = min(1, max(0, 2 - 2 * f))
        if cmode == "spectrum":
            h = math.radians(r1 * 300)
            c = [0.62 + 0.38 * math.cos(h), 0.62 + 0.38 * math.cos(h - 2.094), 0.62 + 0.38 * math.cos(h - 4.189)]
        elif cmode == "white":
            c = [1, 1, 1]; alpha *= .85
        else:
            c = list(T.ACCENT[mat])
        out.append((p, sz, c, alpha, v, cell, shape == "point"))
    fc, fs, fl = T.FLASH[mat]
    if t < fl:
        out.append((np.array([0, 1.05, 0]), fs * (0.5 + 0.8 * t / fl), list(T.ACCENT[mat]), 1 - t / fl, np.zeros(3), fc, False))
    return out


def draw(bg, parts, scale, origin, additive):
    H, W = bg.shape[:2]
    img = bg.copy()
    for p, sz, c, alpha, v, cell, flat in parts:
        i = T.FX_CELLS.index(cell)
        sprite = ATL[(i // 4) * 16:(i // 4) * 16 + 16, (i % 4) * 16:(i % 4) * 16 + 16]
        x, y = origin[0] + p[0] * scale, origin[1] - p[1] * scale
        w = max(2, int(sz * scale)); h = w
        if flat:
            h = max(2, int(w * 0.28))           # a ring lying on the ground, seen from the side
        if cell == "streak":
            w = max(2, int(w * 0.35)); h = max(3, int(sz * scale))
        spr = np.asarray(Image.fromarray((sprite * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR), float) / 255
        x0, y0 = int(x - w / 2), int(y - h / 2)
        if x0 < 0 or y0 < 0 or x0 + w > W or y0 + h > H:
            continue
        a = spr[..., 3:4] * alpha
        reg = img[y0:y0 + h, x0:x0 + w]
        col = np.array(c)[None, None]
        img[y0:y0 + h, x0:x0 + w] = np.clip(reg + col * a, 0, 1) if additive else reg * (1 - a) + col * a
    return img


def main():
    bgp, outp = sys.argv[1:3]
    bg = np.asarray(Image.open(bgp).convert("RGB").resize((1920, 1080)), float) / 255
    crop = bg[300:780, 760:1160] * 0.75
    mats = ["leather", "copper", "chainmail", "iron", "golden", "netherite", "diamond"]
    times = (0.1, 0.35, 0.8)
    cw, ch = 400, 480
    sheet = Image.new("RGB", (cw * 3 * 3, (ch + 34) * len(mats)), (10, 10, 14))
    dr = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
    except OSError:
        font = ImageFont.load_default()
    for r, m in enumerate(mats):
        for e, ev in enumerate(T.EVENTS):
            for k, t in enumerate(times):
                base = crop.copy()
                fig = Image.fromarray((base * 255).astype(np.uint8))
                d = ImageDraw.Draw(fig)   # the stick, for scale (2 blocks tall)
                ox, oy, s = 200, 440, 170
                d.ellipse((ox - 24, oy - 2.0 * s, ox + 24, oy - 2.0 * s + 48), fill=(90, 190, 40))
                d.line((ox, oy - 1.72 * s, ox, oy - 0.75 * s), fill=(90, 190, 40), width=9)
                d.line((ox, oy - 0.75 * s, ox - 18, oy), fill=(90, 190, 40), width=9)
                d.line((ox, oy - 0.75 * s, ox + 18, oy), fill=(90, 190, 40), width=9)
                d.line((ox - 45, oy - 1.0 * s, ox, oy - 1.6 * s, ox + 45, oy - 1.0 * s), fill=(90, 190, 40), width=8)
                base = np.asarray(fig, float) / 255
                img = draw(base, sim(m, ev, t), s, (ox, oy), m not in ("leather", "turtle"))
                sheet.paste(Image.fromarray((img * 255).astype(np.uint8)), ((e * 3 + k) * cw, r * (ch + 34) + 34))
            dr.text(((e * 3) * cw + 10, r * (ch + 34) + 4), f"{T.TIERS[m]['name']} ({m}) - {ev}  0.1s / 0.35s / 0.7s",
                    font=font, fill=(235, 235, 245))
    sheet.save(outp)


if __name__ == "__main__":
    main()
