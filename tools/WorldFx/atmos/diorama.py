"""Offline Bed Wars island diorama for art review (by Novak): the atmosphere's real block textures on a small island,
lit with the atmosphere's key/fill. Not the game - for judging cohesion before in-game frames."""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image

import preview3d as pv

PACK = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3] / "packs" / "WorldFxCloudSea"
T = PACK / "textures" / "blocks"


def load(name):
    for ext in (".png", ".tga"):
        p = T / f"{name}{ext}"
        if p.exists():
            a = np.array(Image.open(p).convert("RGBA"))
            a[..., 3] = 255
            return a
    raise FileNotFoundError(name)


def scene():
    blocks = {}
    def put(x, y, z, top, side=None, bottom=None):
        blocks[(x, y, z)] = (top, side or top, bottom or side or top)
    for x in range(-4, 5):
        for z in range(-3, 4):
            if x * x + z * z * 1.6 < 22:
                put(x, 0, z, "grass_tint", "grass_side_c", "dirt")
                put(x, -1, z, "end_stone")
    for x in range(5, 12):
        put(x, 0, 0, "wool_colored_red")
    for (x, z) in ((0, 0), (1, 0)):
        put(x, 1, z, "bedtop")
    for x in range(-2, 4):
        for z in range(-2, 3):
            if abs(x - 0.5) >= 2 or abs(z) >= 2:
                if (x + z) % 3 == 0: put(x, 1, z, "obsidian")
                elif (x + z) % 3 == 1: put(x, 1, z, "glass_red")
                else: put(x, 1, z, "planks_oak")
    put(-3, 1, 2, "diamond_block"); put(-3, 1, -2, "emerald_block"); put(3, 1, 3, "iron_block"); put(3, 1, -3, "gold_block")
    put(-3, 1, 0, "concrete_red"); put(-3, 2, 0, "hardened_clay_stained_red"); put(-4, 1, 1, "stonebrick"); put(-4, 1, -1, "cobblestone")
    put(4, 1, 1, "log_oak_top", "log_oak"); put(4, 2, 1, "log_oak_top", "log_oak")
    return blocks


def main(out):
    texnames = set()
    blocks = scene()
    for v in blocks.values():
        texnames |= set(v)
    S = 32
    names = sorted(texnames)
    atlas = np.zeros((S, S * len(names), 4), np.uint8)
    for i, n in enumerate(names):
        if n == "grass_tint":
            a = load("grass_top").astype(float); a[..., :3] *= np.array([0.55, 0.78, 0.36]); a = a.astype(np.uint8)
        elif n == "grass_side_c":
            g = np.array(Image.open(T / "grass_side.tga").convert("RGBA")).astype(float)
            m = g[..., 3:4] / 255.0
            g[..., :3] = g[..., :3] * (m * np.array([0.55, 0.78, 0.36]) + (1 - m)); g[..., 3] = 255; a = g.astype(np.uint8)
        elif n == "bedtop":
            a = load("wool_colored_red")
        else:
            a = load(n)
        atlas[:, i * S:(i + 1) * S] = a
    idx = {n: i for i, n in enumerate(names)}
    cubes = []
    for (x, y, z), (top, side, bot) in blocks.items():
        s = 16
        uv = {f: (idx[side] * S, 0, S, S) for f in ("north", "south", "east", "west")}
        uv["up"] = (idx[top] * S, 0, S, S)
        uv["down"] = (idx[bot] * S, 0, S, S)
        # hide faces touching another block
        for f, d in (("north", (0, 0, -1)), ("south", (0, 0, 1)), ("east", (1, 0, 0)), ("west", (-1, 0, 0)), ("up", (0, 1, 0)), ("down", (0, -1, 0))):
            if (x + d[0], y + d[1], z + d[2]) in blocks:
                uv.pop(f, None)
        cubes.append({"origin": [x * s, y * s, z * s], "size": [s, s, s], "uv": uv})
    img = pv.render(cubes, atlas, size=900, yaw=0.75, pitch=0.55, bg=(186, 120, 140))
    img.save(out)


if __name__ == "__main__":
    main(sys.argv[2] if len(sys.argv) > 2 else "/home/claude/review/diorama.png")
