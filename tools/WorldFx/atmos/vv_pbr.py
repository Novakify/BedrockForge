#!/usr/bin/env python3
"""Vibrant Visuals materials for an atmosphere's blocks (by Novak).

    python3 -I tools/WorldFx/atmos/vv_pbr.py cloudsea [--sheet review.png]     (build_pack.py also runs it)

On the classic renderer the Cloud Sea's block look is a RenderChunk shader that reads a material id from the texture's
alpha. Vibrant Visuals draws blocks through its own deferred materials, so there the look is given the way VV wants it:
a texture set per block - a MERS map (metalness, emissive, roughness, subsurface) and a height map VV derives the
surface normals from. Same intent as the classic shader, per material class (tools/WorldFx/make_material_pack.py):

  wool        the cloud blocks: fully rough, deep subsurface (light soaks in and comes back soft, like cloud), big soft
              billows in the height map, a faint glow of their own so they read lit from inside at dusk
  concrete    smooth matte with a shallow subsurface (comfy, not plastic), barely any relief
  planks/log  the grain is the relief; a satin finish
  stone       rough, crisp relief from the texture; end stone a little softer
  terracotta  fired clay: matte, low relief
  metal       real metal (metalness 1) with a brushed finish; gems: glassy, light passes through, a held glow
  obsidian    black glass: very smooth
  lamps       emissive from their own brightness
  ice / snow  light passes through; snow rough, ice smooth

Cut-outs (leaves, plants, flowers) and unclassified textures are left to the game.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
ROOT = HERE.parent.parent.parent
from make_material_pack import CLASSES, classify  # noqa: E402

NAME = {cid: n for cid, n, _ in CLASSES}

# class -> metalness, emissive, roughness, subsurface, relief (height contrast 0..1), billow (share of soft low-frequency shape)
MAT = {
    "wool":            (0.00, 0.020, 0.96, 0.85, 0.55, 0.80),
    "concrete":        (0.00, 0.000, 0.72, 0.30, 0.10, 0.50),
    "concrete_powder": (0.00, 0.000, 0.95, 0.45, 0.30, 0.30),
    "planks":          (0.00, 0.000, 0.62, 0.15, 0.45, 0.00),
    "log":             (0.00, 0.000, 0.80, 0.10, 0.60, 0.00),
    "stone":           (0.00, 0.000, 0.88, 0.05, 0.60, 0.10),
    "cut_stone":       (0.00, 0.000, 0.70, 0.05, 0.50, 0.00),
    "carved":          (0.00, 0.000, 0.66, 0.05, 0.75, 0.00),
    "end_stone":       (0.00, 0.000, 0.80, 0.20, 0.45, 0.25),
    "obsidian":        (0.00, 0.000, 0.08, 0.00, 0.20, 0.00),
    "terracotta":      (0.00, 0.000, 0.82, 0.18, 0.15, 0.30),
    "glazed":          (0.00, 0.000, 0.22, 0.05, 0.20, 0.00),
    "metal":           (1.00, 0.000, 0.28, 0.00, 0.30, 0.00),
    "gem":             (0.00, 0.050, 0.10, 0.60, 0.35, 0.00),
    "ore":             (0.00, 0.000, 0.85, 0.05, 0.60, 0.00),
    "sand":            (0.00, 0.000, 0.92, 0.25, 0.25, 0.30),
    "soil":            (0.00, 0.000, 0.95, 0.10, 0.50, 0.10),
    "bricks":          (0.00, 0.000, 0.80, 0.05, 0.65, 0.00),
    "quartz":          (0.00, 0.000, 0.45, 0.45, 0.25, 0.00),
    "snow":            (0.00, 0.010, 0.90, 0.80, 0.30, 0.70),
    "ice":             (0.00, 0.000, 0.10, 0.85, 0.15, 0.00),
    "grass":           (0.00, 0.000, 0.90, 0.35, 0.45, 0.20),
    "lamp":            (0.00, 1.000, 0.60, 0.30, 0.30, 0.00),
}
SKIP = {"leaves", "plant", "flower"}


def lum(rgb):
    return (rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722) / 255.0


def blur(a, r):
    """wrap-around box blur, three passes (close to a Gaussian): the textures tile, so must their maps"""
    out = a.astype(np.float64)
    for _ in range(3):
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, 0)
        out = acc / (2 * r + 1)
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, 1)
        out = acc / (2 * r + 1)
    return out


def norm01(a):
    lo, hi = float(a.min()), float(a.max())
    return (a - lo) / (hi - lo) if hi - lo > 1e-6 else np.full_like(a, 0.5)


def billows(n, seed):
    """soft tileable puffs: a few wrapped cosine lobes at random phases, rounded like cumulus tops"""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n * 2 * np.pi
    f = np.zeros((n, n))
    for kx, ky in ((1, 1), (2, 1), (1, 2), (2, 2), (3, 1), (1, 3)):
        f += np.cos(kx * x + rng.uniform(0, 6.283)) * np.cos(ky * y + rng.uniform(0, 6.283)) / (kx + ky)
    f = norm01(f)
    return 1.0 - (1.0 - f) ** 2   # round tops, tight creases


def maps(rgb, alpha, cls, seed):
    m, e, r, s, relief, billow = MAT[cls]
    L = lum(rgb)
    n = L.shape[0]
    fine = norm01(L - blur(L, max(1, n // 16)) + 0.5 * (blur(L, 1) - blur(L, max(2, n // 8))))
    h = fine * (1.0 - billow) + billows(n, seed)[: L.shape[0], : L.shape[1]] * billow if L.shape[0] == L.shape[1] else fine
    h = 0.5 + (norm01(h) - 0.5) * relief
    mers = np.zeros(L.shape + (4,), np.float64)
    mers[..., 0] = m
    if cls == "lamp":
        mers[..., 1] = np.clip((L - 0.35) / 0.5, 0, 1) ** 1.5      # only the bright texels glow
    elif cls == "gem":
        mers[..., 1] = e * np.clip(L * 1.4, 0, 1)
    else:
        mers[..., 1] = e
    # rougher in the creases, a touch smoother on what stands proud (worn edges) - never uniform
    mers[..., 2] = np.clip(r + (0.5 - h) * 0.18, 0.04, 1.0)
    mers[..., 3] = np.clip(s * (0.85 + 0.3 * h), 0, 1)
    if cls == "ore":   # the mineral specks: smoother and a little metallic against the rough stone
        sat = (rgb.max(-1) - rgb.min(-1)) / 255.0
        k = np.clip((sat - 0.18) / 0.25, 0, 1)
        mers[..., 2] = mers[..., 2] * (1 - k) + 0.25 * k
        mers[..., 0] = 0.6 * k
    if alpha is not None:   # cut-out texels of lamps (lantern, torch): nothing there
        mers[alpha < 128] = 0
    return (np.clip(mers, 0, 1) * 255 + 0.5).astype(np.uint8), (np.clip(h, 0, 1) * 255 + 0.5).astype(np.uint8)


def write(pack: Path, sheet: Path | None = None) -> int:
    tdir = pack / "textures" / "blocks"
    n = 0
    tiles = []
    for f in sorted(tdir.glob("*.png")):
        if f.stem.endswith(("_mers", "_heightmap", "_normal", "_mer")):
            continue
        cls = NAME.get(classify(f.stem))
        if cls is None or cls in SKIP or cls not in MAT:
            continue
        img = Image.open(f).convert("RGBA")
        if img.size[0] != img.size[1]:   # flipbook strips keep the game's own material
            continue
        a = np.asarray(img)
        mers, h = maps(a[..., :3].astype(np.float64), a[..., 3] if cls == "lamp" else None, cls, seed=sum(map(ord, f.stem)))
        Image.fromarray(mers, "RGBA").save(tdir / f"{f.stem}_mers.png")
        Image.fromarray(h, "L").save(tdir / f"{f.stem}_heightmap.png")
        (tdir / f"{f.stem}.texture_set.json").write_text(json.dumps({
            "format_version": "1.21.30",
            "minecraft:texture_set": {"color": f.stem, "metalness_emissive_roughness_subsurface": f"{f.stem}_mers", "heightmap": f"{f.stem}_heightmap"},
        }, indent=2) + "\n")
        n += 1
        if sheet and cls in ("wool", "concrete", "planks", "stone", "end_stone", "terracotta", "obsidian", "metal", "gem") and len(tiles) < 400:
            tiles.append((f.stem, cls, a[..., :3], mers, h))
    if sheet and tiles:
        pick, seen = [], {}
        for t in tiles:   # two of each class
            if seen.get(t[1], 0) < 2:
                pick.append(t); seen[t[1]] = seen.get(t[1], 0) + 1
        S = 128
        out = Image.new("RGB", (S * 5, S * len(pick)), (20, 20, 24))
        for i, (_stem, _cls, rgb, mers, h) in enumerate(pick):
            up = lambda arr, mode: Image.fromarray(arr, mode).resize((S, S), Image.NEAREST)
            hh = h.astype(np.float64) / 255.0   # lit preview: a low sun from the upper left over the height map
            gx, gy = np.roll(hh, -1, 1) - np.roll(hh, 1, 1), np.roll(hh, -1, 0) - np.roll(hh, 1, 0)
            shade = np.clip(0.75 - gx * 3.0 - gy * 3.0, 0.2, 1.4)[..., None]
            lit = np.clip(rgb * shade, 0, 255).astype(np.uint8)
            for j, im in enumerate((up(rgb.astype(np.uint8), "RGB"), up(h, "L").convert("RGB"), up(mers[..., 2], "L").convert("RGB"),
                                    up(mers[..., 3], "L").convert("RGB"), up(lit, "RGB"))):
                out.paste(im, (j * S, i * S))
        out.save(sheet)
    return n


def equipment(pack: Path) -> int:
    """Held weapons / tools (textures/worldfx/wfx_*) and armor (textures/models/armor): the Cloud Sea's sky-forged
    porcelain and sunstone as VV materials. The classic Actor shader reads a material id from alpha (255 = plain,
    lower = the blade / plate); here the same split decides the texel's material:
      plain texels (grips, straps, cloth)    matte, a little subsurface
      pale texels of the blade / plate       glazed porcelain: smooth, light sinks in a little
      warm saturated texels (gold, amber)    metal edges catching the sun; sunstone holds a faint glow of its own
    """
    n = 0
    for sub, pat in (("worldfx", "wfx_*.png"), ("models/armor", "*.png")):
        d = pack / "textures" / sub
        for f in sorted(d.glob(pat)):
            if f.stem.endswith(("_mers", "_heightmap")):
                continue
            a = np.asarray(Image.open(f).convert("RGBA")).astype(np.float64)
            rgb, al = a[..., :3], a[..., 3]
            if not (al > 0).any():
                continue
            L = lum(rgb)
            mx, mn = rgb.max(-1) / 255.0, rgb.min(-1) / 255.0
            sat = np.where(mx > 1e-3, (mx - mn) / np.maximum(mx, 1e-3), 0.0)
            warm = np.clip((rgb[..., 0] - rgb[..., 2]) / 255.0 * 2.0, 0, 1)
            special = (al > 0) & (al < 252)                      # the blade / plate (material id in alpha)
            metal = special * np.clip((sat - 0.25) / 0.25, 0, 1) * np.clip(warm * 1.5, 0, 1)   # gold / amber edges
            mers = np.zeros(a.shape, np.float64)
            mers[..., 0] = metal
            mers[..., 1] = special * metal * 0.035 * np.clip(L * 1.3, 0, 1)                     # sunstone: a held glow
            mers[..., 2] = np.where(special, 0.22 + 0.10 * (1 - L) - 0.06 * metal, 0.78)
            mers[..., 3] = np.where(special, 0.35 * (1 - metal), 0.15)
            mers[al == 0] = 0
            Image.fromarray((np.clip(mers, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(d / f"{f.stem}_mers.png")
            (d / f"{f.stem}.texture_set.json").write_text(json.dumps({
                "format_version": "1.21.30",
                "minecraft:texture_set": {"color": f.stem, "metalness_emissive_roughness_subsurface": f"{f.stem}_mers"},
            }, indent=2) + "\n")
            n += 1
    return n


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("atmosphere", choices=["cloudsea"])
    ap.add_argument("--sheet", type=Path)
    a = ap.parse_args()
    pack = ROOT / "packs" / "WorldFxCloudSea"
    print(f"{write(pack, a.sheet)} block + {equipment(pack)} equipment texture sets written to {pack}")
