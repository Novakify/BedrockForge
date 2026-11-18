"""Offline contact sheets of the knife animations (by Novak): samples the pack's own keyframes (Catmull-Rom), poses
the knife's bones (bfk_pose > bfk_spin > parts, balisong handles on their pins) and renders frames in the item frame.
For judging motion, timing and arcs before the game is reachable - not the game's exact transform conventions."""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import knives      # noqa: E402
import preview     # noqa: E402
from mesh import Mesh  # noqa: E402

PARENT = {"bfk_pose": None, "bfk_spin": "bfk_pose", "bfk_blade": "bfk_spin", "bfk_body": "bfk_spin",
          "bfb_safe": "bfk_spin", "bfb_bite": "bfk_spin"}


def sample(track, t):
    ks = sorted((float(k), v["post"] if isinstance(v, dict) else v) for k, v in track.items())
    ts = [k for k, _ in ks]
    vs = [np.array(v, float) for _, v in ks]
    if t <= ts[0]:
        return vs[0]
    if t >= ts[-1]:
        return vs[-1]
    i = max(j for j in range(len(ts)) if ts[j] <= t)
    u = (t - ts[i]) / (ts[i + 1] - ts[i])
    p0, p1, p2, p3 = vs[max(i - 1, 0)], vs[i], vs[i + 1], vs[min(i + 2, len(vs) - 1)]
    return 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3)


def rot(deg):
    x, y, z = np.radians(deg)
    Rx = np.array([[1, 0, 0], [0, math.cos(x), -math.sin(x)], [0, math.sin(x), math.cos(x)]])
    Ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]])
    Rz = np.array([[math.cos(z), -math.sin(z), 0], [math.sin(z), math.cos(z), 0], [0, 0, 1]])
    return Rz @ Ry @ Rx


def posed(kind, anim, t):
    mesh, atlas = getattr(knives, kind)()
    pivots = {b["name"]: np.array(b["pivot"], float) for b in knives.bones(kind)}
    xf = {}
    for b in PARENT:
        ch = anim.get("bones", {}).get(b, {})
        R = rot(sample(ch["rotation"], t)) if "rotation" in ch else np.eye(3)
        T = sample(ch["position"], t) if "position" in ch else np.zeros(3)
        xf[b] = (R, T)

    def apply(b, p):
        while b:
            R, T = xf.get(b, (np.eye(3), np.zeros(3)))
            pv = pivots.get(b, np.array([0, 24, 0.0]))
            p = R @ (p - pv) + pv + T
            b = PARENT.get(b)
        return p
    out = Mesh()
    for bone, polys in mesh.parts.items():
        for poly in polys:
            R_ = xf.get(bone, (np.eye(3), 0))[0]
            out.parts.setdefault(bone, []).append([(apply(bone, p), n, uv) for p, n, uv in poly])
    return out, atlas


def sheet(kind, name, path, frames=8, size=220, anim_file=None):
    anim_file = anim_file or f"packs/BFCosmetics/animations/bfk_{kind}.animation.json"
    anims = json.load(open(anim_file))["animations"]
    a = anims[f"animation.bfk.{kind}.{name}"]
    L = a["animation_length"]
    ims = []
    for i in range(frames):
        t = L * i / (frames - 1)
        m, at = posed(kind, a, t)
        im = preview.render(m, at, size, 0.3, -0.15, light="studio", emissive_bones=(), centre=(0, 28, 0), extent=34)
        ImageDraw.Draw(im).text((6, 6), f"{t:.2f}s", fill=(220, 220, 230))
        ims.append(im)
    o = Image.new("RGB", (size * frames, size))
    for i, im in enumerate(ims):
        o.paste(im, (i * size, 0))
    o.save(path)
    return o


if __name__ == "__main__":
    sheet(sys.argv[1], sys.argv[2], sys.argv[3])
