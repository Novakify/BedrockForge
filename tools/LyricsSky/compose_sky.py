#!/usr/bin/env python3
"""Draws sky_frame's quads (stars, constellations, shooting stars, lightning, weather) over a render_sky.js dome.
  python3 compose_sky.py dome.png quads.json out.png yaw pitch vfov
Same camera maths as render_sky.js (Minecraft yaw/pitch, right = -X when facing +Z). Weather sprites are sampled
from resources/lyricsflow/sky/sprites.png. Additive-free "over" blending, like WorldSky's translucent quads."""
import json, math, pathlib, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]


def cam(yaw, pitch):
    y, p = math.radians(yaw), math.radians(pitch)
    f = np.array([-math.sin(y) * math.cos(p), -math.sin(p), math.cos(y) * math.cos(p)])
    r = np.array([-math.cos(y), 0.0, -math.sin(y)])
    u = np.cross(r, f)
    return f, r, u


def main():
    dome, qf, outp = sys.argv[1:4]
    yaw, pitch, vfov = map(float, sys.argv[4:7])
    img = np.asarray(Image.open(dome).convert('RGB'), dtype=np.float64) / 255.0
    H, W = img.shape[:2]
    spr = np.asarray(Image.open(ROOT / 'resources/lyricsflow/sky/sprites.png').convert('RGBA'), dtype=np.float64) / 255.0
    f, r, u = cam(yaw, pitch)
    ty = math.tan(math.radians(vfov) / 2); tx = ty * W / H
    data = json.load(open(qf))

    def proj(p):
        z = p @ f
        if z <= 1e-3:
            return None
        return ((p @ r) / z / tx * 0.5 + 0.5) * W, (0.5 - (p @ u) / z / ty * 0.5) * H

    for q in data['quads']:
        layer, tex = q[0], q[1]
        vs = [q[2 + i * 7: 9 + i * 7] for i in range(4)]
        pts = [proj(np.array(v[:3])) for v in vs]
        if any(p is None for p in pts):
            continue
        for tri in ((0, 1, 2), (0, 2, 3)):
            P = np.array([pts[i] for i in tri])
            x0, y0 = np.floor(P.min(0)).astype(int); x1, y1 = np.ceil(P.max(0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, W - 1), min(y1, H - 1)
            if x1 < x0 or y1 < y0:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            (ax, ay), (bx, by), (cx, cy) = P
            d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(d) < 1e-9:
                continue
            w0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / d
            w1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / d
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            V = [vs[i] for i in tri]
            a = w0 * V[0][6] + w1 * V[1][6] + w2 * V[2][6]
            col = np.array([[(V[k][5] >> 16) & 255, (V[k][5] >> 8) & 255, V[k][5] & 255] for k in range(3)]) / 255.0
            rgb = w0[..., None] * col[0] + w1[..., None] * col[1] + w2[..., None] * col[2]
            if tex == 1:
                uu = w0 * V[0][3] + w1 * V[1][3] + w2 * V[2][3]
                vv = w0 * V[0][4] + w1 * V[1][4] + w2 * V[2][4]
                sx = np.clip((uu * spr.shape[1]).astype(int), 0, spr.shape[1] - 1)
                sy = np.clip((vv * spr.shape[0]).astype(int), 0, spr.shape[0] - 1)
                t = spr[sy, sx]
                rgb = rgb * t[..., :3]; a = a * t[..., 3]
            a = np.clip(a, 0, 1) * m
            reg = img[y0:y1 + 1, x0:x1 + 1]
            reg[:] = reg * (1 - a[..., None]) + rgb * a[..., None]
    Image.fromarray(np.uint8(np.clip(img, 0, 1) * 255)).save(outp)


if __name__ == '__main__':
    main()
