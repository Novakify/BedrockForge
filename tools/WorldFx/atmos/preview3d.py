"""Offline preview of Bedrock box models (equipment review before the game is reachable) - by Novak.

Rasterises the cubes of a geometry (per-face UV, our generator's format) with the atmosphere's light: warm key from
the sun side, violet fill, a little rim. Not the game's renderer - it is for judging shape, proportion and texture.
"""
from __future__ import annotations

import numpy as np
from PIL import Image

FACES = {  # face: (normal, corner order as (axis offsets) for u right / v down when looking at the face)
    "north": (np.array([0, 0, -1.0]),),
    "south": (np.array([0, 0, 1.0]),),
    "east": (np.array([1.0, 0, 0]),),
    "west": (np.array([-1.0, 0, 0]),),
    "up": (np.array([0, 1.0, 0]),),
    "down": (np.array([0, -1.0, 0]),),
}


def face_quad(o, s, f):
    x0, y0, z0 = o
    x1, y1, z1 = o[0] + s[0], o[1] + s[1], o[2] + s[2]
    # corners: top-left, top-right, bottom-right, bottom-left in texture space (Bedrock per-face conventions)
    if f == "north":
        return [(x1, y1, z0), (x0, y1, z0), (x0, y0, z0), (x1, y0, z0)]
    if f == "south":
        return [(x0, y1, z1), (x1, y1, z1), (x1, y0, z1), (x0, y0, z1)]
    if f == "east":
        return [(x1, y1, z1), (x1, y1, z0), (x1, y0, z0), (x1, y0, z1)]
    if f == "west":
        return [(x0, y1, z0), (x0, y1, z1), (x0, y0, z1), (x0, y0, z0)]
    if f == "up":
        return [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]
    return [(x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0)]


def rot(yaw, pitch, roll=0.0):
    cy, sy = np.cos(yaw), np.sin(yaw)
    cp, sp = np.cos(pitch), np.sin(pitch)
    cr, sr = np.cos(roll), np.sin(roll)
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    Rz = np.array([[cr, -sr, 0], [sr, cr, 0], [0, 0, 1]])
    return Rx @ Ry @ Rz


def render(cubes, texture, size=420, yaw=0.6, pitch=-0.35, roll=0.0, scale=None, bg=(52, 46, 70), alpha=False, flat=False):
    """cubes: list of dict(origin, size, uv={face: (u, v, w, h)}) in texel units; texture: HxWx4 uint8"""
    tex = np.asarray(texture).astype(float)
    th, tw = tex.shape[:2]
    R = rot(yaw, pitch, roll)
    pts = np.array([c for cb in cubes for c in (cb["origin"], np.add(cb["origin"], cb["size"]))], float)
    ctr = (pts.min(0) + pts.max(0)) / 2
    ext = np.abs((pts - ctr) @ R.T).max() * 2.2
    sc = scale or size / ext
    img = np.zeros((size, size, 3)) + np.array(bg)
    zb = np.full((size, size), -1e9)
    cov = np.zeros((size, size), bool)
    key = np.array([0.55, 0.45, 0.70]); key /= np.linalg.norm(key)
    for cb in cubes:
        for f, (u, v, w, h) in cb["uv"].items():
            n = R @ FACES[f][0]
            if n[2] <= 0:
                continue
            q = (np.array(face_quad(cb["origin"], cb["size"], f)) - ctr) @ R.T
            sx = q[:, 0] * sc + size / 2
            sy = -q[:, 1] * sc + size / 2
            # bilinear param over the quad (affine is fine for a parallelogram)
            ax, ay = sx[1] - sx[0], sy[1] - sy[0]
            bx, by = sx[3] - sx[0], sy[3] - sy[0]
            det = ax * by - ay * bx
            if abs(det) < 1e-6:
                continue
            xmin, xmax = int(max(sx.min(), 0)), int(min(sx.max() + 1, size))
            ymin, ymax = int(max(sy.min(), 0)), int(min(sy.max() + 1, size))
            if xmin >= xmax or ymin >= ymax:
                continue
            yy, xx = np.mgrid[ymin:ymax, xmin:xmax] + 0.5
            dx, dy = xx - sx[0], yy - sy[0]
            a = (dx * by - dy * bx) / det
            b = (ax * dy - ay * dx) / det
            inside = (a >= 0) & (a <= 1) & (b >= 0) & (b <= 1)
            z = q[0, 2] + a * (q[1, 2] - q[0, 2]) + b * (q[3, 2] - q[0, 2])
            tu = np.clip((u + a * w).astype(int), 0, tw - 1)
            tv = np.clip((v + b * h).astype(int), 0, th - 1)
            texel = tex[tv, tu]
            ok = inside & (texel[..., 3] > 128) & (z > zb[ymin:ymax, xmin:xmax])
            ndl = max(float(n @ key), 0.0)
            light = np.array([0.30, 0.29, 0.46]) * (0.75 + 0.25 * n[1]) + np.array([1.0, 0.62, 0.40]) * ndl * 0.95
            if flat:
                light = np.array([1.0, 1.0, 1.0]) * (0.62 + 0.38 * max(float(n @ np.array([-0.5, 0.6, 0.62])), 0.0))
            col = texel[..., :3] * light
            col = np.clip(col, 0, 255)
            sub = img[ymin:ymax, xmin:xmax]
            sub[ok] = col[ok]
            zb[ymin:ymax, xmin:xmax][ok] = z[ok]
            cov[ymin:ymax, xmin:xmax][ok] = True
    if alpha:
        return Image.fromarray(np.dstack([img.astype(np.uint8), (cov * 255).astype(np.uint8)]), 'RGBA')
    return Image.fromarray(img.astype(np.uint8))
