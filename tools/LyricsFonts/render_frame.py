#!/usr/bin/env python3
"""CPU stand-in for RF's lyric pass (shaders/glyph.hlsl): renders a frame dump written by the core tests
(LYRICS_DUMP_DIR=<dir> LyricsCoreTests) so presets and the top bar can be looked at without the game.
World glyphs are projected with the camera in the dump (yaw/pitch/fov; positions are already camera-relative),
screen glyphs / solid shapes use the screen mapping. Same SDF, outline, glow and rounded-rect maths as the shader.
usage: python3 tools/LyricsFonts/render_frame.py dump.json out.png [width]"""
import json
import math
import pathlib
import sys

import numpy as np
from PIL import Image

F_SCREEN, F_BITMAP, F_MSDF, F_GRADIENT, F_SOLID = 1, 2, 4, 16, 32


def unpack(c):
    return np.array([c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255], np.float32) / 255.0


def camera(yaw, pitch):
    y, p = math.radians(yaw), math.radians(pitch)
    f = np.array([-math.sin(y) * math.cos(p), -math.sin(p), math.cos(y) * math.cos(p)])
    r = np.array([-math.cos(y), 0.0, -math.sin(y)])
    u = np.cross(r, f)
    return r, u, f


def homography(src, dst):
    A = []
    for (x, y), (u, v) in zip(src, dst):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y, -u])
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y, -v])
    _, _, vt = np.linalg.svd(np.array(A))
    return vt[-1].reshape(3, 3)


def main():
    d = json.loads(pathlib.Path(sys.argv[1]).read_text())
    W = int(sys.argv[3]) if len(sys.argv) > 3 else 1280
    H = int(round(W / d['aspect']))
    atlases = [np.asarray(Image.open(p).convert('L'), np.float32) / 255.0 for p in d['atlases']]
    r, u, f = camera(d['yaw'], d['pitch'])
    t = math.tan(math.radians(d['fov']) / 2)
    img = np.zeros((H, W, 3), np.float32)
    yy = np.linspace(0, 1, H)[:, None, None]
    img[:] = np.concatenate([0.05 + 0.10 * yy, 0.05 + 0.08 * yy, 0.13 + 0.12 * yy], -1)  # dusk-sky gradient backdrop

    def to_px(p, screen):
        if screen:
            return np.array([p[0] / d['aspect'] * W, p[1] * H]), True
        z = p @ f
        if z < 0.05:
            return None, False
        x, y = p @ r, p @ u
        return np.array([W / 2 + x / (z * t) * H / 2, H / 2 - y / (z * t) * H / 2]), True

    for g in d['glyphs']:
        pos, R, U = np.array(g[0:3]), np.array(g[3:6]), np.array(g[6:9])
        u0, v0, u1, v1 = g[9:13]
        fill, fill2, outl, glow = unpack(g[13]), unpack(g[14]), unpack(g[15]), unpack(g[16])
        alpha, outW, glowW, glowK, atlas, flags, pxr = g[17], g[18], g[19], g[20], g[21], g[22], g[23]
        screen = bool(flags & F_SCREEN)
        corners = [pos - R + U, pos + R + U, pos + R - U, pos - R - U]  # TL TR BR BL (uv 0,0 1,0 1,1 0,1)
        pts = []
        for c in corners:
            q, ok = to_px(c, screen)
            if not ok:
                break
            pts.append(q)
        if len(pts) < 4:
            continue
        pts = np.array(pts)
        x0, y0 = np.floor(pts.min(0)).astype(int)
        x1, y1 = np.ceil(pts.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, W), min(y1, H)
        if x1 <= x0 or y1 <= y0 or (x1 - x0) * (y1 - y0) > W * H:
            continue
        Hm = homography(pts, [(0, 0), (1, 0), (1, 1), (0, 1)])
        xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        P = Hm @ np.stack([xs.ravel(), ys.ravel(), np.ones(xs.size)])
        lu, lv = (P[0] / P[2]).reshape(xs.shape), (P[1] / P[2]).reshape(xs.shape)
        inside = (lu >= 0) & (lu <= 1) & (lv >= 0) & (lv <= 1)
        if not inside.any():
            continue
        qw = np.linalg.norm(pts[1] - pts[0]); qh = np.linalg.norm(pts[3] - pts[0])
        if flags & F_SOLID:  # rounded rect, radius = outW * min half extent, softness glowW px
            hx, hy = qw / 2, qh / 2
            rad = min(hx, hy) * min(max(outW, 0), 1)
            px, py = np.abs(lu - 0.5) * 2 * hx, np.abs(lv - 0.5) * 2 * hy
            qx, qy = px - (hx - rad), py - (hy - rad)
            dist = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - rad
            a = np.clip(0.5 - dist / max(1.0, glowW), 0, 1) * fill[3]
            rgb, aa = fill[:3] * a[..., None], a
        else:
            uu, vv = u0 + lu * (u1 - u0), v0 + lv * (v1 - v0)
            A = atlases[atlas]
            ah, aw = A.shape
            sx = np.clip((uu * aw - 0.5), 0, aw - 1); sy = np.clip((vv * ah - 0.5), 0, ah - 1)
            if flags & F_BITMAP:
                s = A[np.clip((vv * ah).astype(int), 0, ah - 1), np.clip((uu * aw).astype(int), 0, aw - 1)]
                col = fill[None, None, :] * (1 - lv[..., None]) + fill2[None, None, :] * lv[..., None] if flags & F_GRADIENT \
                    else np.broadcast_to(fill, s.shape + (4,))
                aa = s * col[..., 3]
                rgb = col[..., :3] * aa[..., None]
            else:
                ix, iy = sx.astype(int), sy.astype(int)
                fx, fy = sx - ix, sy - iy
                ix1, iy1 = np.minimum(ix + 1, aw - 1), np.minimum(iy + 1, ah - 1)
                v = (A[iy, ix] * (1 - fx) + A[iy, ix1] * fx) * (1 - fy) + (A[iy1, ix] * (1 - fx) + A[iy1, ix1] * fx) * fy
                spr = max(0.5 * pxr * (qw / max((u1 - u0) * aw, 1e-6) + qh / max((v1 - v0) * ah, 1e-6)) / 1.0, 1.0)
                sd = (v - 0.5) * spr
                fA = np.clip(sd + 0.5, 0, 1)
                oA = np.clip(sd + outW * spr + 0.5, 0, 1) if outW > 0 else fA
                col = fill if not (flags & F_GRADIENT) else None
                if col is None:
                    col = fill[None, None, :] * (1 - lv[..., None]) + fill2[None, None, :] * lv[..., None]
                else:
                    col = np.broadcast_to(col, v.shape + (4,))
                gA = glowK * glow[3] * np.clip(1 + (v - 0.5) / max(glowW, 1e-3), 0, 1) ** 2 if glowK > 0 else 0 * v
                rgb = col[..., :3] * (fA * col[..., 3])[..., None] + outl[:3] * ((oA - fA) * outl[3])[..., None]
                aa = fA * col[..., 3] + (oA - fA) * outl[3]
                rgb = rgb + glow[:3] * (gA * (1 - aa))[..., None]
                aa = aa + gA * (1 - aa)
        k = alpha * inside
        rgb, aa = rgb * k[..., None], aa * k
        reg = img[y0:y1, x0:x1]
        reg[:] = rgb + reg * (1 - aa[..., None])
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(sys.argv[2])
    print('wrote', sys.argv[2], f'({len(d["glyphs"])} glyphs)')


if __name__ == '__main__':
    main()
