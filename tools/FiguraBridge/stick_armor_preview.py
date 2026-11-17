#!/usr/bin/env python3
"""Offline preview of stick_armor.py output: front / three-quarter / side / back, one material per row.
Simple z-buffered rasterizer with Lambert + rim light; the light rails are drawn unlit at sweep phase `phase`.
Inspection pose only swings the arms out 25 degrees; the head parts (p17 helmet + ball) face the camera like the
in-game billboard. NOT an in-game render: no Bedrock lighting, no bloom, no culling.

usage: stick_armor_preview.py STICK_GEO OUT_DIR(with models/ textures/) out.png [materials=iron,diamond,netherite] [phase=0.1]
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HIDE = {"p6", "p62", "p68", "p9", "p11", "p13", "p15"}
ARMS = {"p30": 1, "p32": 1, "p37": -1, "p39": -1}
PIV = {1: np.array([.825, 25.9875, .25]), -1: np.array([-.825, 25.9875, 0])}


def pose(P, bone, az):
    P = np.array(P, float)
    if bone in ARMS:
        sg = ARMS[bone]
        a = math.radians(25 * sg)
        R = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])
        P = (P - PIV[sg]) @ R.T + PIV[sg]
    if bone == "p17":   # billboard: front (-z) turns toward the camera
        a = math.radians(az)
        R = np.array([[math.cos(a), 0, -math.sin(a)], [0, 1, 0], [math.sin(a), 0, math.cos(a)]])
        piv = np.array([0, 29.7, -.165])
        P = (P - piv) @ R.T + piv
    return P


def tris_of(pm):
    pos = np.array(pm["positions"], float)
    uv = np.array(pm["uvs"], float)
    out = []
    for poly in pm["polys"]:
        idx = [c[0] for c in poly]
        uvi = [c[2] for c in poly]
        out.append((pos[idx[:3]], uv[uvi[:3]]))
        if poly[2] != poly[3]:
            out.append((pos[[idx[0], idx[2], idx[3]]], uv[[uvi[0], uvi[2], uvi[3]]]))
    return out


def render(items, az, w=420, h=820, scale=21.0):
    """items: list of (tris world-space Nx3x3, uvs, texture array or colour, unlit flag)"""
    rgb = np.zeros((h, w, 3), float)
    rgb[:] = (14, 16, 26)
    depth = np.full((h, w), -1e9)
    a = math.radians(az)
    view = np.array([math.sin(a), 0, -math.cos(a)])          # camera looks along -view (camera at +view*inf)
    right = np.array([math.cos(a), 0, math.sin(a)])
    up = np.array([0, 1.0, 0])
    L = np.array([-0.4, 0.75, -0.55])
    L /= np.linalg.norm(L)
    for P, UV, tex, unlit in items:
        q = np.c_[P @ right * scale + w / 2, h - 40 - P @ up * scale * 1.0 + 0 * P[:, 0], P @ view]
        q[:, 1] = h - 30 - P[:, 1] * scale
        x0, x1 = max(0, int(q[:, 0].min())), min(w - 1, int(q[:, 0].max()) + 1)
        y0, y1 = max(0, int(q[:, 1].min())), min(h - 1, int(q[:, 1].max()) + 1)
        if x0 > x1 or y0 > y1:
            continue
        den = (q[1, 1] - q[2, 1]) * (q[0, 0] - q[2, 0]) + (q[2, 0] - q[1, 0]) * (q[0, 1] - q[2, 1])
        if abs(den) < 1e-9:
            continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        l0 = ((q[1, 1] - q[2, 1]) * (X - q[2, 0]) + (q[2, 0] - q[1, 0]) * (Y - q[2, 1])) / den
        l1 = ((q[2, 1] - q[0, 1]) * (X - q[2, 0]) + (q[0, 0] - q[2, 0]) * (Y - q[2, 1])) / den
        l2 = 1 - l0 - l1
        z = l0 * q[0, 2] + l1 * q[1, 2] + l2 * q[2, 2]
        old = depth[y0:y1 + 1, x0:x1 + 1]
        mask = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6) & (z > old)
        if not mask.any():
            continue
        old[mask] = z[mask]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        n /= (np.linalg.norm(n) + 1e-12)
        if n @ view < 0:
            n = -n
        shade = 1.0 if unlit else 0.28 + 0.62 * max(0, n @ L) + 0.35 * (1 - abs(n @ view)) ** 2
        if isinstance(tex, np.ndarray):
            U = l0 * UV[0, 0] + l1 * UV[1, 0] + l2 * UV[2, 0]
            V = l0 * UV[0, 1] + l1 * UV[1, 1] + l2 * UV[2, 1]
            th, tw = tex.shape[:2]
            c = tex[np.clip((V * th).astype(int), 0, th - 1), np.clip((U * tw).astype(int), 0, tw - 1), :3]
        else:
            c = np.broadcast_to(np.array(tex, float), mask.shape + (3,))
        rgb[y0:y1 + 1, x0:x1 + 1][mask] = np.clip(c[mask] * shade, 0, 255)
    return rgb


def main():
    geo_path, outd, png = sys.argv[1:4]
    opts = dict(a.split("=") for a in sys.argv[4:])
    mats = opts.get("materials", "iron,diamond,netherite").split(",")
    phase = float(opts.get("phase", "0.1"))
    slots = opts.get("slots", "head,chest,legs,feet").split(",")
    outd = Path(outd)
    g = json.loads(Path(geo_path).read_text())["minecraft:geometry"][0]
    body = {b["name"]: tris_of(b["poly_mesh"]) for b in g["bones"] if "poly_mesh" in b and b["name"] in
            ("p17", "p22", "p30", "p32", "p37", "p39", "p44", "p46", "p48", "p52", "p54", "p56")}
    A = json.loads((outd / "models/entity/fb_armor_stick.geo.json").read_text())["minecraft:geometry"]
    armor = {}
    for geo in A:
        ident = geo["description"]["identifier"].split(".")[-1]
        armor[ident] = {b["name"]: tris_of(b["poly_mesh"]) for b in geo["bones"] if "poly_mesh" in b}
    views = [("FRONT", 0), ("THREE-QUARTER", 35), ("SIDE", 90), ("BACK", 180)]
    W, Hh = 420, 820
    sheet = Image.new("RGB", (W * len(views), (Hh + 40) * len(mats)), (14, 16, 26))
    dr = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20)
    except OSError:
        font = ImageFont.load_default()
    for r, mat in enumerate(mats):
        metal = np.array(Image.open(outd / f"textures/figura/fb_armor/{mat}.png").convert("RGB"), float)
        fx = np.array(Image.open(outd / f"textures/figura/fb_armor/{mat}_fx.png").convert("RGB"), float)
        fx = np.roll(fx, -int(round(phase * 0.5 * fx.shape[0])), axis=0)  # uv offset +0.5*phase
        for c, (label, az) in enumerate(views):
            items = []
            for bone, T in body.items():
                for P, UV in T:
                    items.append((pose(P, bone, az), UV, (30, 30, 34) if bone != "p17" else (24, 24, 28), False))
            for s in slots:
                for layer, tex, unlit in ((s, metal, False), (s + "_fx", fx, True)):
                    for bone, T in armor.get(layer, {}).items():
                        for P, UV in T:
                            items.append((pose(P, bone, az), UV, tex, unlit))
            img = render(items, az, W, Hh)
            sheet.paste(Image.fromarray(img.astype(np.uint8)), (c * W, r * (Hh + 40) + 40))
            dr.text((c * W + 12, r * (Hh + 40) + 10), f"{mat.upper()}  {label}", font=font, fill=(220, 220, 240))
    sheet.save(png)


if __name__ == "__main__":
    main()
