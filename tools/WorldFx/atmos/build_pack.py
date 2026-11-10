#!/usr/bin/env python3
"""Build an atmosphere's resource pack (by Novak).

    python3 -I tools/WorldFx/atmos/build_pack.py cloudsea [--sheet review.png]

Writes packs/WorldFx<Name>/: the atmosphere's block textures (with the World FX material id in alpha for opaque
blocks, see make_material_pack.py), its equipment (attachables, models, item icons) and a manifest. The pack goes
ABOVE World FX Materials in global_resource_packs.json: blocks it doesn't repaint keep their material ids from there.
"""
from __future__ import annotations

import argparse
import importlib
import json
import zlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
ROOT = HERE.parent.parent.parent

from make_material_pack import STEP, classify  # noqa: E402
import tex  # noqa: E402

ATMOS = {
    "cloudsea": {"name": "Cloud Sea", "module": "cloudsea", "pack": "WorldFxCloudSea",
                 "uuid": "c10d5ea0-6a1e-4b8e-9d3a-2f6b8e1c4a01", "module_uuid": "c10d5ea0-6a1e-4b8e-9d3a-2f6b8e1c4a02"},
}


def blocks(m):
    """name -> (rgb, alpha or None). alpha None = opaque block (gets its material id)."""
    out = {}
    for i, (dye, col) in enumerate(m.DYES.items()):
        out[f"wool_colored_{dye}"] = (m.wool(col, seed=100 + i), None)
        out[f"concrete_{dye}"] = (m.concrete(col, seed=200 + i), None)
        out[f"hardened_clay_stained_{dye}"] = (m.terracotta(col, seed=300 + i), None)
        rgb, a = m.glass(col, seed=400 + i)
        out[f"glass_{dye}"] = (rgb, a)
    out["hardened_clay"] = (m.terracotta((152, 94, 66), seed=399), None)
    out["glass"] = m.glass(None)
    for k, col in m.PLANKS.items():
        out[f"planks_{k}"] = (m.planks(col, seed=500 + (zlib.crc32(k.encode()) % 50)), None)
    for k, (bark, wood) in m.LOGS.items():
        out[f"log_{k}"] = (m.log_side(bark, seed=600 + (zlib.crc32(k.encode()) % 50), birch=(k == "birch")), None)
        out[f"log_{k}_top"] = (m.log_top(wood, bark, seed=700 + (zlib.crc32(k.encode()) % 50)), None)
    out["end_stone"] = (m.end_stone(), None)
    out["obsidian"] = (m.obsidian(), None)
    out["stone"] = (m.stone(), None)
    out["cobblestone"] = (m.cobblestone(), None)
    out["stonebrick"] = (m.stone_bricks(), None)
    out["sand"] = (m.sand(), None)
    out["sandstone_normal"] = (m.sandstone(), None)
    out["sandstone_top"] = (m.sand((220, 204, 152), seed=31), None)
    out["sandstone_bottom"] = (m.sand((214, 198, 146), seed=32), None)
    out["dirt"] = (m.dirt(), None)
    out["grass_top"] = (m.grass_top(), "keep")
    out["grass_side"] = m.grass_side()
    out["leaves_oak"] = m.leaves()
    if hasattr(m, "quartz"):
        q = m.quartz()
        for n in ("quartz_block_side", "quartz_block_top", "quartz_block_bottom"):
            out[n] = (q, None)
        out["brick"] = (m.bricks(), None)
        out["nether_brick"] = (m.bricks((70, 30, 36), (40, 22, 28), seed=55), None)
        out["red_nether_brick"] = (m.bricks((112, 20, 22), (52, 10, 14), seed=56), None)
        out["clay"] = (m.clay(), None)
        out["snow"] = (m.snow(), None)
        out["purpur_block"] = (m.purpur(), None)
    if hasattr(m, "MINERALS"):
        M = m.MINERALS
        out["iron_block"] = (m.metal_block(M["iron"], kind="iron"), None)
        out["gold_block"] = (m.metal_block(M["gold"], kind="gold"), None)
        out["diamond_block"] = (m.gem_block(M["diamond"]), None)
        out["emerald_block"] = (m.gem_block(M["emerald"], seed=43), None)
        out["diamond_ore"] = (m.ore(M["diamond"], seed=44), None)
        out["emerald_ore"] = (m.ore(M["emerald"], seed=45), None)
        out["gold_ore"] = (m.ore(M["gold"], seed=46), None)
        out["iron_ore"] = (m.ore([(120, 90, 70), (170, 130, 100), (206, 170, 136), (230, 204, 176)], seed=47), None)
    return out


EXT = {"grass_side": ".tga", "leaves_oak": ".tga"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("atmosphere", choices=sorted(ATMOS))
    ap.add_argument("--sheet", type=Path)
    a = ap.parse_args()
    cfg = ATMOS[a.atmosphere]
    m = importlib.import_module(cfg["module"])
    pack = ROOT / "packs" / cfg["pack"]
    tdir = pack / "textures" / "blocks"
    tdir.mkdir(parents=True, exist_ok=True)
    items = []
    ids = {}
    for name, (rgb, alpha) in blocks(m).items():
        if alpha is None:
            cid = classify(name) or (5 if name.startswith("log_") else 0)
            al = np.full(rgb.shape[:2], 255 - STEP * cid if cid else 255)
            ids[name] = cid
        elif isinstance(alpha, str):
            al = np.full(rgb.shape[:2], 255)
        else:
            al = alpha
        ext = EXT.get(name, ".png")
        img = Image.fromarray(np.dstack([rgb.astype(np.uint8), np.asarray(al, np.uint8)]), "RGBA")
        img.save(tdir / f"{name}{ext}")
        items.append((name, np.dstack([rgb, np.asarray(al) if alpha is not None and not isinstance(alpha, str) else np.full(rgb.shape[:2], 255)])))
    manifest = {
        "format_version": 2,
        "header": {"name": f"{cfg['name']} by Novak", "description": f"World FX atmosphere: {cfg['name']}",
                   "uuid": cfg["uuid"], "version": [0, 1, 0], "min_engine_version": [1, 21, 0]},
        "modules": [{"type": "resources", "uuid": cfg["module_uuid"], "version": [0, 1, 0]}],
    }
    (pack / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if hasattr(m, "equipment"):
        m.equipment(pack)
    if a.sheet:
        tex.sheet(items, a.sheet, zoom=3, tile=2, cols=12)
    untagged = [n for n, c in ids.items() if not c]
    print(f"{len(items)} block textures -> {pack}")
    if untagged:
        print("no material id:", ", ".join(untagged))


if __name__ == "__main__":
    main()
