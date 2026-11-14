#!/usr/bin/env python3
"""Cloud Sea by Novak - living block textures.

Writes animated versions (Bedrock flipbooks) of the blocks a BedWars map is built and fought with, into the Cloud Sea
pack: wool, concrete, terracotta, planks, end stone, glass, obsidian and the four mineral blocks. The art stays
Faithful 32x (by the Faithful team, faithfulpack.net - see the pack's CREDITS.txt): nothing is repainted or blurred.
Every frame is the original texture with its own pixels relit:

  * one slow band of light rolls across each face. Its wave fits the tile exactly, so across a wall or a bridge of
    the same block it is ONE continuous movement, not a blink per block
  * each material answers in its own way - wool lifts its light fibres, planks their grain, terracotta warms,
    concrete barely moves but has a few grains that catch it, obsidian's violet veins breathe, glass throws its two
    glints along the pane, minerals flash along their cut, end stone keeps a few flecks that twinkle like the sky
  * 32 frames, blended by the game (smooth at any frame rate), one loop every 4.8 s

Usage: python3 tools/WorldFx/make_flipbooks.py <bedrock-samples dir> [--preview out.png]
(bedrock-samples is only read for terrain_texture.json: which atlas tiles use which texture.)
"""
import json
import math
import pathlib
import re
import sys

import numpy as np
from PIL import Image

root = pathlib.Path(__file__).resolve().parents[2]
pack = root / "packs/WorldFxCloudSea"
blocks = pack / "textures/blocks"
out_dir = blocks / "wfx_live"
samples = pathlib.Path(sys.argv[1])
N = 32          # frames
TICKS = 3       # game ticks per frame -> 4.8 s a loop

COLOURS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "silver", "cyan", "purple", "blue",
           "brown", "green", "red", "black"]


def lum(a):
    return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]


def rank(a):
    """0..1 rank of each pixel's luminance within the texture (so 'the light fibres' means the same on any colour)"""
    l = lum(a).ravel()
    order = l.argsort(kind="stable").argsort(kind="stable")
    return (order / max(len(l) - 1, 1)).reshape(a.shape[:2])


def wave(h, w, f, kx, ky, phase=0.0, sharp=1.0):
    """a band of light travelling over the tile: 0..1, tile-periodic (kx, ky whole numbers), frame f of N"""
    y, x = np.mgrid[0:h, 0:w]
    a = 2 * math.pi * ((kx * x + ky * y) / w - f / N + phase)
    return (0.5 + 0.5 * np.cos(a)) ** sharp


def relight(base, gain, tint=(1.0, 1.0, 1.0)):
    """multiply rgb by (1 + gain * tint), keep alpha (the Cloud Sea material id lives there)"""
    o = base.astype(np.float32).copy()
    for c in range(3):
        o[..., c] = o[..., c] * (1.0 + gain * tint[c])
    o[..., :3] = np.clip(np.rint(o[..., :3]), 0, 255)
    return o.astype(np.uint8)


def hash01(i, salt):
    return (math.sin(i * 12.9898 + salt * 78.233) * 43758.5453) % 1.0


def picks(mask, n, salt):
    """n well-spread pixels of a mask (deterministic)"""
    ys, xs = np.nonzero(mask)
    idx = sorted(range(len(ys)), key=lambda i: hash01(int(ys[i]) * 64 + int(xs[i]), salt))
    got = []
    for i in idx:
        p = (int(ys[i]), int(xs[i]))
        if all(abs(p[0] - q[0]) + abs(p[1] - q[1]) >= 6 for q in got):
            got.append(p)
        if len(got) == n:
            break
    return got


# ---- the materials: frame f (0..N-1) of a texture a (h x w x 4 uint8)
def wool(a, f, salt):
    r = rank(a)
    w = wave(*a.shape[:2], f, 1, 1, salt * 0.13, 2.0)
    fibre = np.clip((r - 0.45) / 0.4, 0, 1)                       # the light fibres answer, the dark weave holds
    return relight(a, (w - 0.45) * (0.07 + 0.17 * fibre))      # centred: bright wool has no headroom, so it also dips


def concrete(a, f, salt):
    w = wave(*a.shape[:2], f, 1, -1, salt * 0.17, 2.0)
    o = relight(a, (w - 0.4) * 0.10)
    r = rank(a)
    for (y, x) in picks(r > 0.55, 5, salt + 3):                    # a few grains catch the light as it passes
        k = float(w[y, x]) ** 6
        o[y, x, :3] = np.clip(o[y, x, :3].astype(np.float32) * (1 + 0.30 * k) + 10 * k, 0, 255)
    return o


def terracotta(a, f, salt):
    w = wave(*a.shape[:2], f, 1, 1, 0.5 + salt * 0.11, 1.6)
    return relight(a, (w - 0.4) * 0.15, (1.0, 0.80, 0.58))         # it warms as the light crosses it


def planks(a, f, salt):
    r = rank(a)
    w = wave(*a.shape[:2], f, 1, 0, salt * 0.21, 2.2)              # along the grain
    grain = np.clip((r - 0.35) / 0.45, 0, 1)
    seam = r < 0.12                                                # the gaps between boards stay dark
    g = (w - 0.4) * (0.05 + 0.15 * grain)
    g[seam] = 0.0
    return relight(a, g, (1.0, 0.92, 0.78))


def end_stone(a, f, salt):
    r = rank(a)
    w = wave(*a.shape[:2], f, 1, 1, 0.25, 1.5)
    o = relight(a, (w - 0.45) * 0.10)
    for i, (y, x) in enumerate(picks(r > 0.72, 9, 7)):             # flecks that twinkle, each in its own time
        t = (f / N + hash01(i, 5)) % 1.0
        k = max(0.0, 1.0 - abs(t - 0.5) * 7.0) ** 2                # a short, soft blink once a loop
        px = o[y, x, :3].astype(np.float32)
        o[y, x, :3] = np.clip(px * (1 + 0.12 * k) + np.array([30, 36, 46]) * k, 0, 255)   # cool, like starlight
    return o


def obsidian(a, f, salt):
    l = lum(a)
    vein = np.clip((l - np.percentile(l, 55)) / max(np.percentile(l, 97) - np.percentile(l, 55), 1), 0, 1)
    w = wave(*a.shape[:2], f, 1, 1, 0.6, 1.4)
    o = a.astype(np.float32).copy()
    k = (w * 0.85 + 0.05) * vein
    o[..., 0] += k * 46
    o[..., 1] += k * 12
    o[..., 2] += k * 84                                            # the violet in it breathes
    o[..., :3] = np.clip(np.rint(o[..., :3]), 0, 255)
    return o.astype(np.uint8)


def mineral(a, f, salt):
    r = rank(a)
    w = wave(*a.shape[:2], f, 1, 1, salt * 0.19, 5.0)              # a tight flash running along the cut
    edge = np.zeros(a.shape[:2], bool)
    edge[:2, :] = edge[-2:, :] = edge[:, :2] = edge[:, -2:] = True
    g = w * (0.05 + 0.30 * np.clip((r - 0.4) / 0.5, 0, 1)) - 0.05
    g[edge] *= 0.35
    return relight(a, g)


def glass(a, f, salt):
    """the pane keeps its frame; a glint slides along the diagonal across the clear middle"""
    h, w_ = a.shape[:2]
    o = a.copy()
    al = a[..., 3].astype(np.float32)
    inner = np.zeros((h, w_), bool)
    inner[2:-2, 2:-2] = True
    frame_rgb = a[0, 0, :3].astype(np.float32)
    y, x = np.mgrid[0:h, 0:w_]
    # two short strokes (like the two the art already has), travelling up-right, crossing the pane once a loop
    for j, off in enumerate((0.0, 0.47)):
        t = (f / N + off) % 1.0
        c = -18 + t * (h + w_ + 36)                                # the stroke sits on x + y = c ... moving along it
        along = (x - y) / 2.0
        centre = (j * 2 - 1) * 5.0
        d_line = np.abs((x + (h - 1 - y)) - c)
        seg = np.clip(1.0 - np.abs(along - centre + (t - 0.5) * 6) / 4.5, 0, 1)
        k = np.clip(1.0 - d_line / 1.0, 0, 1) * seg * inner
        k = k * math.sin(math.pi * t) ** 0.7                       # it fades in and out at the pane's edges
        light = np.clip(frame_rgb * 1.15 + 40, 0, 255)
        for ch in range(3):
            o[..., ch] = np.clip(np.rint(o[..., ch] * (1 - k) + light[ch] * k), 0, 255)
        o[..., 3] = np.clip(np.rint(np.maximum(al, k * 215)), 0, 255)
        al = o[..., 3].astype(np.float32)
    return o


FAMILIES = []   # (texture file stem, function, salt, tile-name test)
for i, c in enumerate(COLOURS):
    FAMILIES.append((f"wool_colored_{c}", wool, i, lambda t, c=c: t in ("wool", f"wool_colored_{c}")))
    FAMILIES.append((f"concrete_{c}", concrete, i, lambda t, c=c: t == "concrete" or re.fullmatch(r"\w+_concrete", t)))
    FAMILIES.append((f"hardened_clay_stained_{c}", terracotta, i, lambda t: t == "stained_clay" or re.fullmatch(r"\w+_terracotta", t)))
    FAMILIES.append((f"glass_{c}", glass, i, lambda t: t == "stained_glass" or re.fullmatch(r"\w+_stained_glass", t)))
FAMILIES.append(("hardened_clay", terracotta, 20, lambda t: t == "hardened_clay"))
FAMILIES.append(("glass", glass, 20, lambda t: t == "glass"))
for i, w in enumerate(["planks_oak", "planks_spruce", "planks_birch", "planks_jungle", "planks_acacia", "planks_big_oak"]):
    FAMILIES.append((w, planks, i, lambda t: t == "planks" or re.fullmatch(r"\w+_planks", t)))
FAMILIES.append(("end_stone", end_stone, 0, lambda t: t == "end_stone"))
FAMILIES.append(("obsidian", obsidian, 0, lambda t: t == "obsidian"))
for i, m in enumerate(["diamond_block", "emerald_block", "gold_block", "iron_block"]):
    FAMILIES.append((m, mineral, i, lambda t, m=m: t == m))

terrain = json.loads(re.sub(r"//.*", "", (samples / "resource_pack/textures/terrain_texture.json").read_text()))["texture_data"]
uses = {}   # texture path -> [(tile, index or None)]
for tile, v in terrain.items():
    tx = v["textures"]
    lst = tx if isinstance(tx, list) else [tx]
    for i, e in enumerate(lst):
        p = e if isinstance(e, str) else e.get("path", "")
        uses.setdefault(p, []).append((tile, i if isinstance(tx, list) and len(lst) > 1 else None))

out_dir.mkdir(parents=True, exist_ok=True)
book = []
strips = {}
for stem, fn, salt, ok in FAMILIES:
    src = blocks / f"{stem}.png"
    if not src.exists():
        print("skip (not in the pack):", stem)
        continue
    a = np.array(Image.open(src).convert("RGBA"))
    a = a[:a.shape[1]]                                            # the first frame if it is already a strip
    frames = [fn(a, f, salt) for f in range(N)]
    strip = np.concatenate(frames, 0)
    Image.fromarray(strip, "RGBA").save(out_dir / f"{stem}.png", optimize=True)
    strips[stem] = frames
    for tile, idx in uses.get(f"textures/blocks/{stem}", []):
        if not ok(tile):
            continue
        e = {"flipbook_texture": f"textures/blocks/wfx_live/{stem}", "atlas_tile": tile, "ticks_per_frame": TICKS, "blend_frames": True}
        if idx is not None:
            e["atlas_index"] = idx
        book.append(e)
(pack / "textures/flipbook_textures.json").write_text(json.dumps(book, indent=1) + "\n")
print(f"{len(strips)} living textures, {len(book)} flipbook entries -> {out_dir.relative_to(root)}")

if "--preview" in sys.argv:
    dst = sys.argv[sys.argv.index("--preview") + 1]
    show = ["wool_colored_white", "wool_colored_red", "concrete_blue", "hardened_clay_stained_red", "planks_oak", "end_stone", "obsidian",
            "glass", "glass_red", "diamond_block", "gold_block", "emerald_block"]
    S, T = 4, 2                                                   # scale, tiles per side (to see the wave cross block edges)
    fr = [0, 8, 16, 24]
    W = 32 * T * S
    sheet = Image.new("RGB", (W * len(show), W * len(fr)), (38, 22, 58))
    for i, stem in enumerate(show):
        if stem not in strips:
            continue
        for j, f in enumerate(fr):
            t = Image.fromarray(np.tile(strips[stem][f], (T, T, 1)), "RGBA")
            bg = Image.new("RGBA", t.size, (38, 22, 58, 255))
            bg.alpha_composite(t)
            sheet.paste(bg.convert("RGB").resize((W, W), Image.NEAREST), (i * W, j * W))
    sheet.save(dst)
