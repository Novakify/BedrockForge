#!/usr/bin/env python3
"""Build the World FX Materials resource pack (packs/WorldFxMaterials).

Every fully opaque vanilla block texture of a known material class gets an invisible MATERIAL ID in its alpha
channel: alpha = 255 - 4 * id (opaque blocks ignore texture alpha, so the game looks exactly vanilla; the World FX
probe proved the atlas keeps alpha bit-exact). A World FX block shader reads it back with
    id = round((1 - a) * 255 / 4)
and gives the material its own look (wool soft, concrete clean, stone deep, metals reflective...).
Textures with any transparency (glass, leaves, plants) and tinted overlays (grass sides) are never touched.

    python3 -I tools/WorldFx/make_material_pack.py --vanilla <bedrock-samples>/resource_pack/textures/blocks

Writes packs/WorldFxMaterials/ and tools/WorldFx/material_ids.json (id -> class, textures). The class table below
is THE list: the block shaders' MAT_* constants (shaders/src/CloudSea/common.hlsli) must match it.
"""
from __future__ import annotations

import argparse
import json
import re
import uuid
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PACK = ROOT / "packs" / "WorldFxMaterials"

# id -> (class name, regex on the texture path relative to textures/blocks, without extension)
CLASSES: list[tuple[int, str, str]] = [
    (1, "wool", r"^wool_colored_\w+$"),
    (2, "concrete", r"^concrete_(?!powder)\w+$"),
    (3, "concrete_powder", r"^concrete_powder_\w+$"),
    (4, "planks", r"^(planks_\w+|\w+_planks)$"),
    (5, "log", r"^(log_(acacia|big_oak|birch|jungle|oak|spruce)|stripped_\w+_log|\w+_log|\w+_stem|\w+_hyphae|"
               r"huge_fungus/\w+_stem\w*|bamboo_block)$"),
    (6, "stone", r"^(stone|stone_(andesite|diorite|granite)|cobblestone|cobblestone_mossy|deepslate(/deepslate)?(_top)?|"
                 r"(deepslate/)?cobbled_deepslate|tuff|blackstone(_top)?|netherrack|basalt_(side|top)|calcite|"
                 r"dripstone_block|smooth_basalt|stone_slab_(side|top)|soul_sand|soul_soil|prismarine_rough|"
                 r"moss_block|pale_moss_block|gilded_blackstone)$"),
    (7, "cut_stone", r"^(stonebrick(_carved|_cracked|_mossy)?|stone_(andesite|diorite|granite)_smooth|"
                     r"(deepslate/)?(polished_deepslate|deepslate_bricks|deepslate_tiles|cracked_deepslate_\w+|"
                     r"chiseled_deepslate)|polished_\w+|chiseled_\w*(tuff|blackstone|nether_bricks|stone_bricks)\w*|"
                     r"cracked_\w+_bricks|tuff_bricks|purpur_\w+|prismarine_bricks|prismarine_dark|end_bricks|"
                     r"quartz_bricks|resin_bricks|cinnabar_bricks)$"),
    (8, "end_stone", r"^end_stone$"),
    (9, "obsidian", r"^(obsidian|crying_obsidian)$"),
    (10, "terracotta", r"^hardened_clay(_stained_\w+)?$"),
    (11, "glazed", r"^glazed_terracotta_\w+$"),
    (12, "metal", r"^((exposed_|weathered_|oxidized_)?(copper_block|cut_copper|chiseled_copper)|"
                  r"(iron|gold|netherite)_block|raw_(iron|gold|copper)_block|heavy_core)$"),
    (13, "gem", r"^(diamond|emerald|lapis|redstone|amethyst|coal)_block$"),
    (14, "ore", r"^(deepslate/)?(deepslate_)?\w*_ore$"),
    (15, "sand", r"^(sand|red_sand|(red_)?sandstone_(bottom|carved|smooth|top|normal)|red_sandstone(_normal)?|"
                 r"sandstone)$"),
    (16, "soil", r"^(dirt|coarse_dirt|grass_path_top|grass_path_side|dirt_podzol_top|dirt_with_roots|mud|packed_mud|clay|gravel|farmland_dry|"
                 r"muddy_mangrove_roots_(side|top))$"),
    (17, "bricks", r"^(brick|nether_brick|red_nether_brick|mud_bricks)$"),
    (18, "quartz", r"^quartz_block_(bottom|chiseled|chiseled_top|lines|lines_top|side|top)$"),
    (19, "snow", r"^(snow|powder_snow)$"),
    # living blocks (2026-11-11): ice with depth, grass and leaves in the wind, lights that breathe, flowers that glow.
    # Leaves / plants / lamps keep their cut-out: only their opaque texels get the id (alpha 255 - 4 * id stays > 0.5).
    (20, "ice", r"^(ice_packed|blue_ice)$"),
    (21, "grass", r"^(grass_top|grass_carried|moss_block|pale_moss_block)$"),
    (22, "lamp", r"^(glowstone|sea_lantern|shroomlight|redstone_lamp_on|\w*froglight\w*|pumpkin_face_on|lantern|soul_lantern|"
                 r"torch_on|soul_torch|end_rod|magma|beacon|campfire_log_lit|soul_campfire_log_lit)$"),
    (23, "leaves", r"^(leaves_\w+|\w+_leaves(_opaque)?)$"),
    (24, "plant", r"^(tallgrass|fern|double_plant_(grass|fern)_(bottom|top)|vine|seagrass\w*|deadbush|sweet_berry_bush_stage\d|"
                  r"azalea_plant|waterlily)$"),
    (25, "flower", r"^(flower_\w+|red_flower\w*|double_plant_(paeonia|rose|syringa|sunflower)_\w+|torchflower|"
                   r"azalea_leaves_flowers\w*|flowering_azalea_\w+|pink_petals|wildflowers)$"),
]
CUTOUT = {"leaves", "plant", "flower", "lamp"}   # tagged texel by texel (their transparent texels stay transparent)
STEP = 4


def classify(rel: str) -> int:
    for cid, _name, rx in CLASSES:
        if re.match(rx, rel):
            return cid
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vanilla", type=Path, required=True)
    a = ap.parse_args()
    src = a.vanilla
    out_tex = PACK / "textures" / "blocks"
    found: dict[int, list[str]] = {cid: [] for cid, *_ in CLASSES}
    skipped_alpha = []
    seen = set()
    for f in sorted(src.rglob("*")):
        if f.suffix.lower() not in (".png", ".tga") or not f.is_file():
            continue
        stem = f.stem
        if stem.endswith(("_mers", "_mer", "_normal", "_heightmap")) and not stem.endswith("sandstone_normal"):
            continue
        rel = f.relative_to(src).with_suffix("").as_posix()
        cid = classify(rel)
        if not cid:
            continue
        im = Image.open(f)
        rgba = np.array(im.convert("RGBA"))
        if rgba[..., 3].min() < 255:
            if CLASSES[cid - 1][1] not in CUTOUT:
                skipped_alpha.append(rel)
                continue
            if rgba[..., 3].max() < 255 or ((rgba[..., 3] > 0) & (rgba[..., 3] < 255)).any():
                skipped_alpha.append(rel)   # soft edges: not a clean cut-out
                continue
            rgba[..., 3] = np.where(rgba[..., 3] > 0, 255 - STEP * cid, 0)
            key = (rel, f.suffix.lower())
            if key in seen:
                continue
            seen.add(key)
            dst = out_tex / f.relative_to(src)
            dst.parent.mkdir(parents=True, exist_ok=True)
            Image.fromarray(rgba, "RGBA").save(dst)
            found[cid].append(rel)
            continue
        key = (rel, f.suffix.lower())
        if key in seen:
            continue
        seen.add(key)
        rgba[..., 3] = 255 - STEP * cid
        dst = out_tex / f.relative_to(src)
        dst.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(rgba, "RGBA").save(dst)
        found[cid].append(rel)

    uf = HERE / "material_pack_uuids.json"
    if uf.exists():
        u = json.loads(uf.read_text())
    else:
        u = {"header": str(uuid.uuid4()), "module": str(uuid.uuid4())}
        uf.write_text(json.dumps(u, indent=2) + "\n")
    manifest = {
        "format_version": 2,
        "header": {"name": "World FX Materials", "description": "by Novak - Bedrock Forge World FX material ids (looks vanilla)",
                   "uuid": u["header"], "version": [0, 1, 0], "min_engine_version": [1, 21, 0]},
        "modules": [{"type": "resources", "uuid": u["module"], "version": [0, 1, 0]}],
    }
    (PACK / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    icon = HERE.parent.parent / "packs" / "WorldFxTagTest" / "pack_icon.png"
    if icon.exists():
        (PACK / "pack_icon.png").write_bytes(icon.read_bytes())
    table = {"step": STEP, "encode": "alpha = 255 - step * id",
             "classes": {str(cid): {"name": n, "textures": found[cid]} for cid, n, _ in CLASSES}}
    (HERE / "material_ids.json").write_text(json.dumps(table, indent=1) + "\n")
    total = sum(len(v) for v in found.values())
    print(f"{total} textures tagged")
    for cid, n, _ in CLASSES:
        print(f"  {cid:2d} {n:16s} {len(found[cid]):3d}  {', '.join(found[cid][:6])}{' ...' if len(found[cid]) > 6 else ''}")
    if skipped_alpha:
        print(f"skipped (has transparency): {', '.join(skipped_alpha[:12])}")


if __name__ == "__main__":
    main()
