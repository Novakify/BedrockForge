#!/usr/bin/env python3
"""Render sample text from the generated atlases on the CPU with the same maths as RF's shaders/glyph.hlsl
(SDF edge + outline + glow, or nearest-sampled bitmap), so atlas quality can be checked without the game.
usage: python3 tools/LyricsFonts/preview.py resources/lyricsflow/fonts out.png [font ...]"""
import json
import pathlib
import sys

import numpy as np
from PIL import Image


def sample_bilinear(img, u, v):
    h, w = img.shape
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x0c, x1c = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
    y0c, y1c = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
    a = img[y0c, x0c] * (1 - fx) + img[y0c, x1c] * fx
    b = img[y1c, x0c] * (1 - fx) + img[y1c, x1c] * fx
    return a * (1 - fy) + b * fy


def render(fontdir, fid, text, px, fill=(1.0, 0.89, 0.96), glow=(1.0, 0.35, 0.78), outline=(0.25, 0.1, 0.2),
           outline_w=0.0, glow_w=0.25, glow_k=0.6):
    meta = json.loads((fontdir / f'{fid}.json').read_text())
    atlas = np.asarray(Image.open(fontdir / f'{fid}.png').convert('L'), np.float32) / 255.0
    A, W, H = meta['atlas'], meta['atlas']['width'], meta['atlas']['height']
    g = {e['unicode']: e for e in meta['glyphs']}
    m = meta['metrics']
    width = sum(g.get(ord(c), {'advance': 0.5})['advance'] for c in text)
    cw, ch = int(width * px + px), int(px * 1.6)
    out = np.zeros((ch, cw, 4), np.float32)
    pen, base = px * 0.5, ch * 0.5 + (m['capHeight'] + m['xHeight']) * 0.25 * px
    for c in text:
        e = g.get(ord(c))
        if not e:
            pen += 0.5 * px
            continue
        if 'planeBounds' in e:
            pb, ab = e['planeBounds'], e['atlasBounds']
            x0, x1 = int(pen + pb['left'] * px), int(np.ceil(pen + pb['right'] * px))
            y0, y1 = int(base - pb['top'] * px), int(np.ceil(base - pb['bottom'] * px))
            xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            fu = (xs - (pen + pb['left'] * px)) / ((pb['right'] - pb['left']) * px)
            fv = (ys - (base - pb['top'] * px)) / ((pb['top'] - pb['bottom']) * px)
            u = (ab['left'] + fu * (ab['right'] - ab['left'])) / W
            v = (ab['top'] + fv * (ab['bottom'] - ab['top'])) / H
            if A['type'] == 'bitmap':
                iu = np.clip((u * W).astype(int), 0, W - 1)
                iv = np.clip((v * H).astype(int), 0, H - 1)
                a = atlas[iv, iu]
                col = np.stack([a * fill[0], a * fill[1], a * fill[2], a], -1)
            else:
                d = (sample_bilinear(atlas, u, v) - 0.5) * A['distanceRange']   # atlas px, + inside
                scale = px / A['size']                                           # screen px per atlas px
                sd = d * scale
                fillA = np.clip(sd + 0.5, 0, 1)
                olA = np.clip(sd + outline_w * A['distanceRange'] * scale + 0.5, 0, 1) if outline_w > 0 else fillA
                gl = glow_k * np.clip(1 + d / (glow_w * A['distanceRange'] * 2 + 1e-6), 0, 1) ** 2
                rgb = np.array(outline) * (olA - fillA)[..., None] + np.array(fill) * fillA[..., None]
                a = np.maximum(olA, gl)
                rgb = rgb + np.array(glow) * (gl * (1 - olA))[..., None]
                col = np.concatenate([rgb, a[..., None]], -1)
            ys0, xs0 = max(y0, 0), max(x0, 0)
            sub = col[ys0 - y0:ch - y0 if y1 > ch else None, xs0 - x0:cw - x0 if x1 > cw else None]
            region = out[ys0:ys0 + sub.shape[0], xs0:xs0 + sub.shape[1]]
            region[:] = sub + region * (1 - sub[..., 3:4])                     # premultiplied over
        pen += e['advance'] * px
    return out


def main():
    fontdir, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    fonts = sys.argv[3:] or sorted(p.stem for p in fontdir.glob('*.json'))
    rows = []
    for f in fonts:
        for px in (28, 96):
            rows.append(render(fontdir, f, 'Lights are dancing - ' + f, px, outline_w=0.12 if px > 50 else 0.0))
    w = max(r.shape[1] for r in rows)
    canvas = np.zeros((sum(r.shape[0] for r in rows), w, 3), np.float32)
    canvas[:] = (0.06, 0.05, 0.12)
    y = 0
    for r in rows:
        canvas[y:y + r.shape[0], :r.shape[1]] = r[..., :3] + canvas[y:y + r.shape[0], :r.shape[1]] * (1 - r[..., 3:4])
        y += r.shape[0]
    Image.fromarray((np.clip(canvas, 0, 1) * 255).astype(np.uint8)).save(dst)
    print('wrote', dst)


if __name__ == '__main__':
    main()
