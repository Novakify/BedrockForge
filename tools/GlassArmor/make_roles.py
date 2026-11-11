#!/usr/bin/env python3
"""Glass Armor, fused look: what each texel of a vanilla armor texture IS in the design - 0 the dark line work (rims,
seams, the drawn outline of each plate), 1 shade, 2 the plain fill, 3 the highlights (studs, the gleam on a plate).
The fused shader lays metal where the artist drew lines, crystal where they drew fills and facets where they drew
highlights, so the armour keeps Mojang's design, rebuilt in our materials.

Reads Mojang's bedrock-samples (resource_pack/textures/models/armor) and writes tools/GlassArmor/vanilla_roles.json:
one hex string per variant, 2 bits per texel, row by row (64 x 32). Only the classes are stored, not the art.
Usage: python3 tools/GlassArmor/make_roles.py /path/to/bedrock-samples [preview.png]
"""
import json
import pathlib
import sys

from PIL import Image

here = pathlib.Path(__file__).resolve().parent
src = pathlib.Path(sys.argv[1]) / "resource_pack/textures/models/armor"
masks = json.loads((here / "vanilla_masks.json").read_text())["variants"]
FILE = {"leather": "cloth", "chain": "chain", "iron": "iron", "copper": "copper", "gold": "gold", "diamond": "diamond",
        "netherite": "netherite"}
out = []
prev = []
for v in masks:
    im = Image.open(src / f"{FILE[v['mat']]}_{v['layer']}.png").convert("RGBA")
    assert im.size == (64, 32), im.size
    px = im.load()
    lum = {}
    for y in range(32):
        for x in range(64):
            r, g, b, a = px[x, y]
            if a >= 128:
                lum[(x, y)] = 0.299 * r + 0.587 * g + 0.114 * b
    ys = sorted(lum.values())
    q = lambda f: ys[min(int(f * len(ys)), len(ys) - 1)]
    lo, mid, hi = q(0.24), q(0.52), q(0.86)
    bits = 0
    row = []
    for y in range(32):
        for x in range(64):
            role = 2
            if (x, y) in lum:
                l = lum[(x, y)]
                role = 0 if l <= lo else (1 if l <= mid else (3 if l > hi else 2))
            row.append(role if (x, y) in lum else -1)
            bits = (bits << 2) | role
    out.append({"mat": v["mat"], "layer": v["layer"], "roles": f"{bits:01024x}"})
    prev.append(row)
(here / "vanilla_roles.json").write_text(json.dumps({
    "source": "pixel classes (line / shade / fill / highlight) of Mojang bedrock-samples armor textures", "variants": out}, indent=1))
if len(sys.argv) > 2:
    S = 6
    sheet = Image.new("RGB", (64 * S * 2, 32 * S * 7), (30, 16, 46))
    pal = {-1: (30, 16, 46), 0: (20, 20, 30), 1: (90, 110, 150), 2: (150, 190, 220), 3: (255, 255, 255)}
    for i, row in enumerate(prev):
        t = Image.new("RGB", (64, 32))
        t.putdata([pal[r] for r in row])
        sheet.paste(t.resize((64 * S, 32 * S), Image.NEAREST), ((i % 2) * 64 * S, (i // 2) * 32 * S))
    sheet.save(sys.argv[2])
print("wrote vanilla_roles.json:", len(out), "variants")
