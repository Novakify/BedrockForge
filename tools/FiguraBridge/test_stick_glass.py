#!/usr/bin/env python3
"""Checks for the stick armor x Novak Glass mapping (stick_armor.py, glass style).

Renderer Foundation draws the glass outline along the edges of the vanilla texture's painted face areas, so:
  1. every ISLAND is a clean island: painted in every material RF knows (chainmail = iron's shape) and its border is
     an edge in the shader (outside it: unpainted or another face) - glass_data.json is RF's own mask / face data
  2. every plate polygon of the suit lies inside one island, on the right armor layer for its slot
  3. every island a plate uses is covered to its border (the plate's silhouette carries the outline)
Runs on the default style (mix) and on the all-glass style.

usage: test_stick_glass.py [STICK_GEO]   (default: stick_armor_handoff/stick_skeleton.json)
"""
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
import stick_armor as sa   # noqa: E402

DATA = json.loads((HERE / "glass_data.json").read_text())
SHAPES = ["leather", "iron", "copper", "gold", "diamond", "netherite"]   # chain is drawn on iron's shape
fails = []


def face_index(layer):
    idx = [[-1] * 64 for _ in range(32)]
    for i, (x, y, w, h) in reversed(list(enumerate(DATA["faces"][str(layer)]))):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                idx[yy][xx] = i
    return idx


FI = {1: face_index(1), 2: face_index(2)}

# 1. clean islands
for name, (x, y, w, h) in sa.ISLAND.items():
    L = sa.ISLAND_LAYER[name]
    fi = FI[L][y][x]
    for m in SHAPES:
        mk = DATA["masks"][f"{m}_{L}"]
        P = lambda a, b: 0 <= a < 64 and 0 <= b < 32 and mk[b][a]
        if fi < 0 or any(not P(a, b) or FI[L][b][a] != fi for a in range(x, x + w) for b in range(y, y + h)):
            fails.append(f"island {name}: not painted / not one face in {m}_{L}")
        for a in range(x - 1, x + w + 1):
            for b in range(y - 1, y + h + 1):
                inside = x <= a < x + w and y <= b < y + h
                if not inside and P(a, b) and FI[L][b][a] == fi:
                    fails.append(f"island {name}: border not an edge at ({a},{b}) in {m}_{L}")

# 2./3. the suit's plates
geo = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "stick_armor_handoff/stick_skeleton.json"


def check(style):
    sa.STYLE = style
    with tempfile.TemporaryDirectory() as td:
        sa.build(geo, None, None, Path(td))
        G = json.loads((Path(td) / "models/entity/fb_armor_stick.geo.json").read_text())["minecraft:geometry"]
    used = {}
    for g in G:
        parts = g["description"]["identifier"].split(".")
        slot = parts[-2] if len(parts) == 5 else parts[-1]   # tiers: ...<slot>.<material>
        if slot.endswith(("_fx", "_metal")):   # light rails / metal plates (the mix): not glass
            if style == "glass":
                fails.append(f"[{style}] {slot}: emitted in the all-glass style")
            continue
        for b in g["bones"]:
            pm = b.get("poly_mesh")
            if not pm:
                continue
            for poly in pm["polys"]:
                uv = [(pm["uvs"][c[2]][0] * 64, pm["uvs"][c[2]][1] * 32) for c in poly]
                home = [n for n, (x, y, w, h) in sa.ISLAND.items() if sa.ISLAND_LAYER[n] == sa.LAYER[slot]
                        and all(x - 1e-3 <= u <= x + w + 1e-3 and y - 1e-3 <= v <= y + h + 1e-3 for u, v in uv)]
                if not home:   # (islands of the two layers overlap in texel space: only this slot's layer counts)
                    fails.append(f"[{style}] {slot}/{b['name']}: polygon outside every island of its layer {uv}")
                    continue
                lo, hi = used.setdefault((home[0], slot + '.' + parts[-1]), [[99, 99], [-1, -1]])
                for u, v in uv:
                    lo[0], lo[1], hi[0], hi[1] = min(lo[0], u), min(lo[1], v), max(hi[0], u), max(hi[1], v)
    for (n, slot), (lo, hi) in used.items():
        x, y, w, h = sa.ISLAND[n]
        if lo[0] > x + 0.05 or lo[1] > y + 0.05 or hi[0] < x + w - 0.05 or hi[1] < y + h - 0.05:
            fails.append(f"[{style}] {slot} island {n}: plates don't reach its border ({lo} .. {hi})")
    return len(used)


n = {st: check(st) for st in dict.fromkeys(("tiers", "mix", "glass"))}
print("\n".join(fails) if fails else f"ok: {len(sa.ISLAND)} clean islands; islands used edge to edge: {n}")
sys.exit(1 if fails else 0)
