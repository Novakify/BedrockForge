"""World FX armor: repaints the vanilla 64x32 armor layers per tier, keeping the vanilla coverage mask (by Novak).

Every face of every armor box is painted as a plate: brushed or hammered body, a lit top/left lip and a shaded
bottom/right edge (so plates read as separate pieces in the low sun), plus tier details - rivets on iron, filigree
trim on gold, cut facets on diamond, hammered dark steel on netherite, stitched panels on leather.
"""
from __future__ import annotations

import numpy as np
from PIL import Image

from tex import rng, value_noise

# box UV: (u0, v0, w, h, d) -> faces
BOXES = {1: {"head": (0, 0, 8, 8, 8), "body": (16, 16, 8, 12, 4), "arm": (40, 16, 4, 12, 4), "leg": (0, 16, 4, 12, 4)},
         2: {"body": (16, 16, 8, 12, 4), "leg": (0, 16, 4, 12, 4)}}


def faces(layer):
    out = []
    for box, (u0, v0, w, h, d) in BOXES[layer].items():
        out += [(box, "top", u0 + d, v0, w, d), (box, "bottom", u0 + d + w, v0, w, d),
                (box, "right", u0, v0 + d, d, h), (box, "front", u0 + d, v0 + d, w, h),
                (box, "left", u0 + d + w, v0 + d, d, h), (box, "back", u0 + 2 * d + w, v0 + d, w, h)]
    return out


def _ramp(f, pal):
    i = np.clip((np.clip(f, 0, 0.9999) * len(pal)).astype(int), 0, len(pal) - 1)
    return np.array(pal, np.uint8)[i]


def paint_layer(vanilla_rgba, layer, tier, pal, seed=0):
    a = np.asarray(vanilla_rgba).copy()
    H, W = a.shape[:2]
    mask = a[..., 3] > 0
    f = np.full((H, W), -1.0)
    r = rng(seed)
    ham = value_noise(64, 6, seed + 5, 2)[:H, :W]
    streak = value_noise(64, 8, seed + 1, 1)[:1, :W].repeat(H, 0)
    for box, face, u, v, w, h in faces(layer):
        ys, xs = np.mgrid[v:v + h, u:u + w]
        fx, fy = xs - u, ys - v
        if tier == "netherite":
            g = 0.36 + (ham[v:v + h, u:u + w] - 0.5) * 0.4
        elif tier == "leather":
            g = 0.50 + (ham[v:v + h, u:u + w] - 0.5) * 0.18
        else:
            g = 0.50 + (streak[v:v + h, u:u + w] - 0.5) * 0.22
        if tier == "diamond":                              # cut facets: each face split on its diagonal
            g = np.where(fx * h > fy * w, 0.58, 0.40) + (streak[v:v + h, u:u + w] - 0.5) * 0.08
        g = np.where((fy == 0) | (fx == 0), g + 0.24, g)
        g = np.where((fy == h - 1) | (fx == w - 1), g - 0.24, g)
        if tier == "iron" and face in ("front", "back") and w >= 4 and h >= 4:
            for cx, cy in ((1, 1), (w - 2, 1), (1, h - 2), (w - 2, h - 2)):
                g = np.where((fx == cx) & (fy == cy), 0.97, g)
        if tier in ("iron", "netherite") and box == "body" and face in ("front", "back"):
            g = np.where((fy >= 6) & (fy % 3 == 0) & (fy < h - 1), g - 0.2, g)            # lames on the belly
        if tier == "gold":
            trim = (fy == 1) | (fx == 1) | (fy == h - 2) | (fx == w - 2)
            g = np.where(trim & (w > 3) & (h > 3), 0.90, g)
            if face == "front" and w >= 6:
                g = np.where((fx == w // 2) & (fy > 1) & (fy < h - 2), 0.82, g)
        front = face in ("front", "back")
        if tier in ("iron", "diamond", "netherite", "gold") and front and box in ("body", "head") and w >= 6:
            mid = w // 2
            g = np.where(fx == mid - 1, g + 0.20, g)                                   # central ridge: lit side
            g = np.where(fx == mid, g - 0.16, g)                                       # and its shadow side
        if tier in ("iron", "netherite", "diamond") and box == "arm" and face != "bottom":
            g = np.where(fy <= 2, g + 0.10, g)                                          # pauldron band
            g = np.where(fy == 3, g - 0.22, g)
        if tier in ("iron", "netherite") and box == "leg" and face != "top" and layer == 1:
            g = np.where(fy >= h - 4, g + 0.08, g)                                      # sabaton cap
            g = np.where(fy == h - 5, g - 0.2, g)
        if tier == "diamond":
            sp = (r.random((h, w)) < 0.06) & (fx > 0) & (fy > 0) & (fx < w - 1) & (fy < h - 1)
            g = np.where(sp, 0.95, g)
        if tier == "gold" and box == "body" and face == "front":
            cx, cy = w // 2, 4
            dd = np.abs(fx - cx + 0.5) + np.abs(fy - cy)
            g = np.where(dd < 1.6, 0.97, g)                                             # a small sun on the chest
            g = np.where((dd >= 1.6) & (dd < 2.6), 0.30, g)
        if tier == "leather":
            g = np.where(fy >= h - 3, g - 0.08, g)                                      # worn, darker hems
        if tier == "leather":                              # stitched seam just inside the panel edge
            st = ((fy == 1) | (fx == 1)) & (((fx + fy) % 2) == 0) & (w > 3) & (h > 3)
            g = np.where(st, 0.80, g)
        f[v:v + h, u:u + w] = g
    paint = f >= 0
    rgb = _ramp(f, pal)
    out = a.copy()
    sel = mask & paint
    if tier == "leather":                                 # only the dyed (alpha 255) pixels; trims keep vanilla
        sel = sel & (a[..., 3] == 255)
    out[..., :3] = np.where(sel[..., None], rgb, a[..., :3])
    mid = {"iron": 12, "gold": 12, "netherite": 12, "diamond": 13}.get(tier, 0)
    if mid:                                               # material id for the Cloud Sea actor shader (leather keeps its tint mask)
        out[..., 3] = np.where(out[..., 3] > 0, 255 - 4 * mid, 0)
    return out


LEATHER = [(70, 70, 70), (104, 104, 104), (138, 138, 138), (170, 170, 170), (204, 204, 204), (236, 236, 236)]
TIERS = {"iron": "steel", "gold": "gold", "diamond": "crystal", "netherite": "netherite", "leather": None}


def export(pack, vanilla_dir, palettes):
    out = pack / "textures" / "models" / "armor"
    out.mkdir(parents=True, exist_ok=True)
    for tier, palname in TIERS.items():
        pal = LEATHER if palname is None else palettes[palname]
        for layer in (1, 2):
            ext = ".tga" if tier == "leather" else ".png"
            src = vanilla_dir / f"{tier}_{layer}{ext}"
            if not src.exists():
                continue
            im = np.array(Image.open(src).convert("RGBA"))
            res = paint_layer(im, layer, tier, pal, seed=layer * 10 + len(tier))
            Image.fromarray(res, "RGBA").save(out / f"{tier}_{layer}{ext}")
