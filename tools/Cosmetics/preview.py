"""Offline turntable preview for cosmetic meshes (by Novak) - for judging shape and texture before the game.

Two light models: 'game' approximates Bedrock's entity shading on Fancy (fixed two-light diffuse + ambient, no
specular - the honest view of what the pack alone gives), 'studio' adds a key/rim/spec to read the facets.
Emissive texels (alpha 0, entity_emissive_alpha) are drawn unlit.
"""
from __future__ import annotations

import math

import numpy as np
from PIL import Image


def rot(yaw, pitch, roll=0.0):
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cr, sr = math.cos(roll), math.sin(roll)
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    Rz = np.array([[cr, -sr, 0], [sr, cr, 0], [0, 0, 1]])
    return Rx @ Ry @ Rz


def render(mesh, atlas, size=640, yaw=0.5, pitch=-0.25, roll=0.0, light="game", bg=(30, 27, 40), ssaa=2,
           centre=None, extent=None, emissive_bones=("bfk_neon",)):
    S = size * ssaa
    tex = atlas.img.astype(float)
    th, tw = tex.shape[:2]
    R = rot(yaw, pitch, roll)
    tris = []
    for bone, polys in mesh.parts.items():
        em = bone in emissive_bones
        for poly in polys:
            pts = [p for p, _, _ in poly]
            ns = [n for _, n, _ in poly]
            uvs = [atlas.texel(r, u, v) for _, _, (r, u, v) in poly]
            for a, b, c in ((0, 1, 2), (0, 2, 3)):
                if b == c or np.allclose(pts[b], pts[c]) and c == 3:
                    continue
                tris.append((pts[a], pts[b], pts[c], ns[a], ns[b], ns[c], uvs[a], uvs[b], uvs[c], em))
    allp = np.array([t[i] for t in tris for i in range(3)])
    ctr = np.asarray(centre if centre is not None else (allp.min(0) + allp.max(0)) / 2, float)
    q = (allp - ctr) @ R.T
    ext = extent or np.abs(q[:, :2]).max() * 2.25
    sc = S / ext
    img = np.zeros((S, S, 3)) + np.array(bg, float)
    # soft vertical background gradient (studio backdrop)
    img *= np.linspace(1.15, 0.75, S)[:, None, None]
    zb = np.full((S, S), -1e9)
    l1 = R @ nrm3([0.35, 0.8, 0.5])   # world-fixed lights rotate with the model view
    l1 = nrm3([0.45, 0.75, 0.55])
    l2 = nrm3([-0.6, 0.25, 0.4])
    view = np.array([0, 0, 1.0])
    for (p0, p1, p2, n0, n1, n2, t0, t1, t2, em) in tris:
        P = (np.array([p0, p1, p2]) - ctr) @ R.T
        N = np.array([n0, n1, n2]) @ R.T
        sx = P[:, 0] * sc + S / 2
        sy = -P[:, 1] * sc + S / 2
        xmin, xmax = int(max(math.floor(sx.min()), 0)), int(min(math.ceil(sx.max()) + 1, S))
        ymin, ymax = int(max(math.floor(sy.min()), 0)), int(min(math.ceil(sy.max()) + 1, S))
        if xmin >= xmax or ymin >= ymax:
            continue
        d = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
        if abs(d) < 1e-9:
            continue
        yy, xx = np.mgrid[ymin:ymax, xmin:xmax] + 0.5
        w1 = ((xx - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (yy - sy[0])) / d
        w2 = ((sx[1] - sx[0]) * (yy - sy[0]) - (xx - sx[0]) * (sy[1] - sy[0])) / d
        w0 = 1 - w1 - w2
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            continue
        z = w0 * P[0, 2] + w1 * P[1, 2] + w2 * P[2, 2]
        sub = zb[ymin:ymax, xmin:xmax]
        vis = inside & (z > sub)
        if not vis.any():
            continue
        u = w0 * t0[0] + w1 * t1[0] + w2 * t2[0]
        v = w0 * t0[1] + w1 * t1[1] + w2 * t2[1]
        ui = np.clip(u.astype(int), 0, tw - 1)
        vi = np.clip(v.astype(int), 0, th - 1)
        col = tex[vi, ui, :3]
        n = w0[..., None] * N[0] + w1[..., None] * N[1] + w2[..., None] * N[2]
        n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-9
        n = np.where((n @ view)[..., None] < 0, -n, n)      # two-sided (entity_alphatest has no culling)
        if em:
            shade = np.ones(n.shape[:2] + (1,)) * 1.0
            out = col * shade
        elif light == "game":
            dif = 0.45 + 0.42 * np.clip(n @ l1, 0, 1) + 0.18 * np.clip(n @ l2, 0, 1)
            out = col * dif[..., None]
        else:
            dif = 0.30 + 0.65 * np.clip(n @ l1, 0, 1) + 0.22 * np.clip(n @ l2, 0, 1)
            h = nrm3(l1 + view)
            spec = np.clip(n @ h, 0, 1) ** 40 * 0.7
            rim = (1 - np.clip(n @ view, 0, 1)) ** 3 * 0.25
            out = col * dif[..., None] + (spec + rim)[..., None] * 255 * np.array([0.92, 0.9, 1.0])
        sub[vis] = z[vis]
        blk = img[ymin:ymax, xmin:xmax]
        blk[vis] = out[vis]
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    return im.resize((size, size), Image.LANCZOS)


def nrm3(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def sheet(mesh, atlas, path, views=((0.0, 0.0), (0.6, -0.3), (1.57, 0.0), (2.6, -0.35)), light="game", size=420):
    ims = [render(mesh, atlas, size, y, p, light=light) for y, p in views]
    out = Image.new("RGB", (size * len(ims), size))
    for i, im in enumerate(ims):
        out.paste(im, (i * size, 0))
    out.save(path)
    return out
