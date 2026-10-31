#!/usr/bin/env python3
"""Build the LyricsFlow panel's UI image atlas: icons (LyricsFlow v2.0's own, 374m, MIT - tools/LyricsPanel/icons),
procedural sky-preset thumbnails in the teaser's style (sky over a dark terrain silhouette), preset thumbnails cut
from offline preview stills (optional), and player logos.
Output: resources/lyricsflow/ui/ui_atlas.png + ui_atlas.json ({"name": [u0, v0, u1, v1]}).
usage: python3 tools/LyricsPanel/make_ui_atlas.py <repo> [preset stills dir with <id>.png]"""
import json
import math
import pathlib
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

TW, TH = 200, 100  # thumbnail size in the atlas (shown at 100 x 50 design px)
rng = np.random.default_rng(7)


def grad(top, bottom, h=TH, w=TW):
    t = np.linspace(0, 1, h)[:, None, None]
    return (np.array(top, float) * (1 - t) + np.array(bottom, float) * t) * np.ones((h, w, 1))


def stars(img, n, col=(255, 255, 255), maxy=0.7):
    for _ in range(n):
        x, y = rng.integers(0, TW), rng.integers(0, int(TH * maxy))
        b = rng.uniform(0.4, 1.0)
        img[y, x] = img[y, x] * (1 - b) + np.array(col) * b
    return img


def glow_disc(img, cx, cy, r, col, glow=2.5, core=1.0):
    yy, xx = np.mgrid[0:TH, 0:TW]
    d = np.hypot(xx - cx, yy - cy)
    a = np.clip(1 - d / (r * glow), 0, 1) ** 2 * 0.6 + (d < r) * core
    a = np.clip(a, 0, 1)[..., None]
    return img * (1 - a) + np.array(col, float) * a


def beams(img, n, col, origin=(0.5, -0.2), spread=1.2, width=0.02, alpha=0.5):
    yy, xx = np.mgrid[0:TH, 0:TW] / np.array([TH, TW])[:, None, None]
    ox, oy = origin
    ang = np.arctan2(yy - oy, (xx - ox) * TW / TH)
    for i in range(n):
        a0 = math.pi / 2 + (i / max(n - 1, 1) - 0.5) * spread
        m = np.exp(-((ang - a0) / width) ** 2) * alpha
        img = img * (1 - m[..., None]) + np.array(col, float) * m[..., None]
    return img


def aurora(img, cols, y0=0.25, amp=0.08, thick=0.12, alpha=0.7):
    yy, xx = np.mgrid[0:TH, 0:TW] / np.array([TH, TW])[:, None, None]
    for i, c in enumerate(cols):
        mid = y0 + i * 0.07 + amp * np.sin(xx * 9 + i * 1.7) + 0.03 * np.sin(xx * 23 + i)
        m = np.exp(-((yy - mid) / thick) ** 2) * alpha * (0.6 + 0.4 * np.sin(xx * 31 + i * 2) ** 2)
        img = img * (1 - m[..., None]) + np.array(c, float) * m[..., None]
    return img


def clouds(img, cols, n=14, alpha=0.6, maxy=0.65):
    im = Image.fromarray(np.uint8(np.clip(img, 0, 255)))
    lay = Image.new('RGBA', (TW, TH), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for i in range(n):
        x, y = rng.uniform(-20, TW), rng.uniform(0, TH * maxy)
        r = rng.uniform(14, 34)
        c = cols[i % len(cols)]
        d.ellipse([x - r * 1.6, y - r, x + r * 1.6, y + r], fill=tuple(c) + (int(255 * alpha),))
    lay = lay.filter(ImageFilter.GaussianBlur(9))
    im = Image.alpha_composite(im.convert('RGBA'), lay).convert('RGB')
    return np.asarray(im, float)


def rain(img, n, col, slant=0.25, length=14, alpha=0.6):
    im = Image.fromarray(np.uint8(np.clip(img, 0, 255))).convert('RGBA')
    lay = Image.new('RGBA', (TW, TH), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for _ in range(n):
        x, y = rng.uniform(0, TW), rng.uniform(-10, TH)
        d.line([x, y, x + slant * length, y + length], fill=tuple(col) + (int(255 * alpha),), width=1)
    return np.asarray(Image.alpha_composite(im, lay).convert('RGB'), float)


def synth_grid(img, col):
    im = Image.fromarray(np.uint8(np.clip(img, 0, 255)))
    d = ImageDraw.Draw(im)
    hz = int(TH * 0.72)
    for i in range(1, 8):
        y = hz + (TH - hz) * (i / 8) ** 1.8
        d.line([0, y, TW, y], fill=col, width=1)
    for i in range(-10, 11):
        d.line([TW / 2 + i * 6, hz, TW / 2 + i * 40, TH], fill=col, width=1)
    return np.asarray(im, float)


def terrain(img, col=(8, 8, 14), y0=0.78):
    # dark ground silhouette with grass tufts, like the teaser thumbnails
    xs = np.arange(TW)
    h = TH * y0 + 3 * np.sin(xs / 11) + 2 * np.sin(xs / 4.3 + 1) + rng.normal(0, 0.8, TW)
    tufts = rng.random(TW) < 0.18
    h = h - tufts * rng.uniform(1, 4, TW)
    yy = np.arange(TH)[:, None]
    m = yy >= h[None, :]
    img[m] = col
    return img


def thumb(kind):
    k = kind
    if k == 'inferno':
        im = grad((40, 6, 0), (230, 90, 10))
        im = clouds(im, [(255, 120, 20), (255, 200, 60), (180, 30, 0)], n=16, alpha=0.75, maxy=0.75)
    elif k == 'matrix':
        im = grad((0, 12, 4), (0, 40, 14))
        im = beams(im, 26, (40, 255, 120), origin=(0.5, -0.6), spread=0.9, width=0.012, alpha=0.6)
    elif k == 'prism':
        im = grad((14, 8, 40), (40, 20, 80))
        im = beams(im, 5, (170, 120, 255), origin=(0.5, -0.1), spread=1.6, width=0.03, alpha=0.55)
        im = stars(im, 60)
    elif k == 'pulsar':
        im = grad((6, 12, 30), (16, 30, 60))
        im = stars(im, 90)
        im = glow_disc(im, TW * 0.48, TH * 0.4, 6, (140, 220, 255), glow=6)
        im = beams(im, 1, (120, 220, 255), origin=(0.48, 0.4), spread=0, width=0.05, alpha=0.8)
    elif k == 'watercolor':
        im = grad((235, 210, 230), (200, 220, 245))
        im = clouds(im, [(255, 180, 200), (180, 220, 255), (220, 200, 255), (255, 230, 180)], n=20, alpha=0.7, maxy=0.8)
    elif k == 'synthwave':
        im = grad((40, 10, 70), (230, 60, 160))
        im = stars(im, 50, maxy=0.4)
        im = glow_disc(im, TW * 0.55, TH * 0.66, 13, (255, 150, 60), glow=2.2)
        im = synth_grid(im, (255, 80, 200))
    elif k == 'northern_lights':
        im = grad((4, 12, 28), (12, 40, 60))
        im = stars(im, 70)
        im = aurora(im, [(40, 255, 160), (60, 200, 255)])
    elif k == 'meteors':
        im = grad((8, 8, 26), (26, 26, 60))
        im = stars(im, 80)
        im = rain(im, 6, (255, 255, 230), slant=1.4, length=22, alpha=0.9)
    elif k == 'sakura':
        im = grad((255, 190, 210), (255, 230, 240))
        im = clouds(im, [(255, 150, 190), (255, 210, 225)], n=12, alpha=0.6)
    elif k == 'ocean':
        im = grad((10, 40, 90), (40, 140, 200))
        im = aurora(im, [(120, 230, 255)], y0=0.4, thick=0.06, alpha=0.35)
    elif k == 'blood_moon':
        im = grad((30, 0, 4), (110, 10, 16))
        im = stars(im, 40)
        im = glow_disc(im, TW * 0.7, TH * 0.3, 11, (230, 40, 30), glow=2.8)
    elif k == 'golden_hour':
        im = grad((90, 60, 140), (255, 170, 70))
        im = glow_disc(im, TW * 0.4, TH * 0.68, 14, (255, 220, 120), glow=3)
    elif k == 'neon_city':
        im = grad((20, 4, 40), (90, 20, 120))
        im = beams(im, 6, (255, 60, 200), origin=(0.5, 1.2), spread=1.4, width=0.02, alpha=0.5)
        im = beams(im, 4, (60, 220, 255), origin=(0.5, 1.2), spread=1.0, width=0.015, alpha=0.5)
    elif k == 'ice':
        im = grad((150, 200, 240), (230, 245, 255))
        im = aurora(im, [(255, 255, 255), (170, 230, 255)], y0=0.3, alpha=0.5)
    elif k == 'toxic':
        im = grad((10, 30, 6), (90, 180, 20))
        im = clouds(im, [(150, 255, 40), (60, 140, 10)], n=12, alpha=0.5)
    elif k == 'void':
        im = grad((0, 0, 0), (12, 6, 20))
        im = glow_disc(im, TW * 0.5, TH * 0.38, 12, (0, 0, 0), glow=2.5, core=1.0)
        im = stars(im, 30, (180, 140, 255))
    elif k == 'lilac':
        im = grad((120, 90, 190), (230, 200, 255))
        im = clouds(im, [(240, 220, 255), (190, 160, 240)], n=10, alpha=0.5)
    elif k == 'storm':
        im = grad((30, 34, 44), (80, 86, 100))
        im = clouds(im, [(50, 54, 64), (100, 104, 120)], n=18, alpha=0.8)
        im = rain(im, 120, (190, 200, 220), slant=0.2, length=10, alpha=0.4)
    elif k == 'lavender':
        im = grad((90, 70, 160), (190, 160, 230))
        im = stars(im, 40)
    elif k == 'sunset_beams':
        im = grad((80, 30, 90), (255, 130, 70))
        im = beams(im, 9, (255, 220, 150), origin=(0.5, 0.75), spread=2.2, width=0.03, alpha=0.4)
    elif k == 'cosmic':
        im = grad((10, 4, 30), (40, 10, 70))
        im = aurora(im, [(200, 80, 255), (80, 120, 255)], y0=0.3, thick=0.2, alpha=0.4)
        im = stars(im, 120)
    elif k == 'eclipse':
        im = grad((6, 6, 12), (30, 24, 40))
        im = glow_disc(im, TW * 0.5, TH * 0.38, 13, (255, 200, 120), glow=1.8)
        im = glow_disc(im, TW * 0.5, TH * 0.38, 12, (4, 4, 8), glow=1.0)
    elif k == 'fireflies':
        im = grad((6, 16, 20), (20, 40, 40))
        for _ in range(30):
            im = glow_disc(im, rng.uniform(0, TW), rng.uniform(TH * 0.3, TH * 0.85), 1, (255, 240, 120), glow=4, core=1)
    else:  # night_lights
        im = grad((10, 14, 40), (40, 50, 100))
        im = stars(im, 100)
    im = terrain(im)
    return Image.fromarray(np.uint8(np.clip(im, 0, 255)))


def apple_logo():
    im = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, 127, 127], fill=(250, 45, 72, 255))
    # a simple white note
    d.ellipse([34, 74, 62, 98], fill=(255, 255, 255, 255))
    d.ellipse([70, 66, 98, 90], fill=(255, 255, 255, 255))
    d.rectangle([56, 30, 62, 86], fill=(255, 255, 255, 255))
    d.rectangle([92, 22, 98, 78], fill=(255, 255, 255, 255))
    d.polygon([(56, 30), (98, 20), (98, 34), (56, 44)], fill=(255, 255, 255, 255))
    return im


def main():
    repo = pathlib.Path(sys.argv[1])
    stills = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else None
    icons_dir = repo / 'tools/LyricsPanel/icons'
    # LyricsFlow 2.1 SkyPreset.order() -> the thumbnail style used for it (procedural until the sky painter is ported)
    sky_ids = {"inferno": "inferno", "matrix": "matrix", "prism": "prism", "pulsar": "pulsar", "aquarell": "watercolor",
               "synthwave": "synthwave", "aurora": "northern_lights", "nebula": "cosmic", "black_hole": "eclipse", "cosmos": "night_lights",
               "neon_city": "neon_city", "golden_hour": "golden_hour", "cotton_candy": "sakura", "sakura": "sakura", "frost": "ice",
               "midnight": "meteors", "deep_sea": "ocean", "blood_moon": "blood_moon", "storm": "storm", "custom": "lilac"}
    entries = []  # (name, image)
    renders = repo / 'tools/LyricsPanel/sky_thumbs'  # real renders of live_sky.fsh (tools/LyricsSky/render_sky.js)
    for sid, kind in sky_ids.items():
        rp = renders / (sid + '.png')
        if rp.exists():
            a = np.asarray(Image.open(rp).convert('RGB').resize((TW, TH), Image.LANCZOS), dtype=np.float64).copy()
            im = Image.fromarray(np.uint8(np.clip(terrain(a), 0, 255)))
        else:
            im = thumb(kind)
        entries.append(('sky.' + sid, im))
    if stills:
        for p in sorted(stills.glob('*.png')):
            im = Image.open(p).convert('RGB')
            # centre crop to 2:1, then thumbnail size
            w, h = im.size
            ch = min(h, w // 2)
            im = im.crop(((w - ch * 2) // 2, (h - ch) // 2, (w - ch * 2) // 2 + ch * 2, (h - ch) // 2 + ch)).resize((TW, TH), Image.LANCZOS)
            entries.append(('preset.' + p.stem, im))
    for p in sorted(icons_dir.glob('*.png')):
        im = Image.open(p).convert('RGBA')
        name = ('logo.' if p.stem in ('spotify', 'youtube') else 'icon.') + p.stem
        entries.append((name, im))
    entries.append(('logo.apple_music', apple_logo()))

    W = 2048
    x = y = rowh = 0
    pad = 2
    placed = []
    for name, im in entries:
        w, h = im.size
        if x + w + pad > W:
            x, y, rowh = 0, y + rowh + pad, 0
        placed.append((name, im, x, y))
        x += w + pad
        rowh = max(rowh, h)
    H = 1 << math.ceil(math.log2(y + rowh + pad))
    atlas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    rects = {}
    for name, im, px, py in placed:
        atlas.paste(im.convert('RGBA'), (px, py))
        w, h = im.size
        rects[name] = [(px + 0.5) / W, (py + 0.5) / H, (px + w - 0.5) / W, (py + h - 0.5) / H]
    out = repo / 'resources/lyricsflow/ui'
    out.mkdir(parents=True, exist_ok=True)
    atlas.save(out / 'ui_atlas.png', optimize=True)
    (out / 'ui_atlas.json').write_text(json.dumps({"width": W, "height": H, "rects": rects}, indent=1))
    print('ui atlas', W, 'x', H, len(rects), 'entries')


if __name__ == '__main__':
    main()
