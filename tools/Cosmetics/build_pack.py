"""Build the BF Cosmetics resource pack (by Novak): 3D knives on the sword attachables.

    python3 -I tools/Cosmetics/build_pack.py <vanilla items dir> [out = packs/BFCosmetics]

<vanilla items dir> = a folder with the game's textures/items/*_sword.png (bedrock-samples or the instance's own
vanilla pack); only their pixel SHAPES are used, for the "knife off" geometry (the vanilla sprite with depth). The
texture itself is referenced by path at run time, so whatever pack is active (Hive's, an atmosphere's) still paints it.

Each sword attachable gets two render controllers: the item's own look while v.bfk = 0, our knife while v.bfk > 0.
v.bfk comes from the holder's public Molang variable v.bfknife, written by Bedrock Forge (src/client/Cosmetics.cpp):
no Bedrock Forge, nothing written, the sword looks like a sword. add_knives() is shared with the World FX
atmosphere packs so their swords become knives the same way (one pipeline).
"""
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "WorldFx" / "atmos"))

import knives                      # noqa: E402
from mesh import export_poly_mesh  # noqa: E402

PACK_UUID = ("5e7b1c2a-9d4f-4a61-8c3e-b0f2d6a9e471", "5e7b1c2a-9d4f-4a61-8c3e-b0f2d6a9e472")
VERSION = [0, 1, 0]
SWORDS = {"wooden_sword": "wood_sword", "stone_sword": "stone_sword", "copper_sword": "copper_sword",
          "iron_sword": "iron_sword", "golden_sword": "gold_sword", "diamond_sword": "diamond_sword",
          "netherite_sword": "netherite_sword"}
KNIVES = [("karambit", knives.karambit), ("balisong", knives.balisong), ("dagger", knives.dagger)]   # index + 1 = v.bfknife

import anims as kn_anims   # noqa: E402
import sounds as kn_sounds  # noqa: E402

HOLD = {
    # same holds as the World FX weapons (tools/WorldFx/atmos/equip.py HOLD_ANIM: picked in game 2026-11-11)
    "animation.bfk.hold_first_person": {"loop": True, "bones": {"wfx_item": {
        "position": [-4.65, 0.8, -1.0], "rotation": [150.0, 0.0, -12.0], "scale": 0.55}}},
    "animation.bfk.hold_third_person": {"loop": True, "bones": {"wfx_item": {
        "position": [0.0, -1.0, 0.0], "rotation": [90.0, 15.0, 90.0]}}},
}
ANIMS, CTRLS, AMAP, ANIMATE, SOUND_FX, PARTICLE_FX = kn_anims.build()
ANIMS["animations"].update(HOLD)
PRE = kn_anims.PRE

RC = {"format_version": "1.8.0", "render_controllers": {"controller.render.bfk_knife": {
    "arrays": {"geometries": {"Array.kgeo": [f"Geometry.{n}" for n, _ in KNIVES]},
               "textures": {"Array.ktex": [f"Texture.{n}" for n, _ in KNIVES]}},
    "geometry": "Array.kgeo[v.bfk - 1]",
    "materials": [{"*": "v.is_enchanted ? Material.enchanted : Material.default"}],
    "textures": ["Array.ktex[v.bfk - 1]", "Texture.enchanted"]}}}


def add_knives(desc):
    """patch a sword attachable description (dict) so it shows our knife (with its tricks, swings, fx and sounds)
    while v.bfk > 0"""
    desc.setdefault("materials", {}).update({"default": desc["materials"].get("default", "entity_alphatest"),
                                             "enchanted": desc["materials"].get("enchanted", "entity_alphatest_glint")})
    desc.setdefault("textures", {}).setdefault("enchanted", "textures/misc/enchanted_item_glint")
    for n, _ in KNIVES:
        desc["textures"][n] = f"textures/bf_cosmetics/{n}"
        desc["geometry"][n] = f"geometry.bfk_{n}"
    desc.setdefault("animations", {}).update(AMAP)
    desc["sound_effects"] = dict(desc.get("sound_effects", {}), **SOUND_FX)
    desc["particle_effects"] = dict(desc.get("particle_effects", {}), **PARTICLE_FX)
    sc = desc.setdefault("scripts", {})
    pre = sc.get("pre_animation", [])
    pre = pre if isinstance(pre, list) else [pre]
    sc["pre_animation"] = [PRE] + [l for l in pre if "v.bfk " not in l and "v.bfk =" not in l]
    an = [a for a in sc.get("animate", []) if not (isinstance(a, dict) and any(k.startswith("bfk_") for k in a))]
    sc["animate"] = an + ANIMATE
    rcs = []
    for r in desc.get("render_controllers", []):
        if isinstance(r, str):
            if r == "controller.render.bfk_knife":
                continue
            rcs.append({r: "v.bfk == 0"})
        else:
            k, cond = next(iter(r.items()))
            if k != "controller.render.bfk_knife":
                rcs.append({k: f"({cond}) && v.bfk == 0"})
    desc["render_controllers"] = rcs + [{"controller.render.bfk_knife": "v.bfk > 0"}]
    return desc


def write_shared(pack: Path):
    """the knife models, textures, animations and render controller (every pack that calls add_knives needs them)"""
    for d in ("models/entity", "textures/bf_cosmetics", "animations", "render_controllers"):
        (pack / d).mkdir(parents=True, exist_ok=True)
    for n, fn in KNIVES:
        mesh, atlas = fn()                       # every knife has its own model AND its own finishes / atlas
        geo = export_poly_mesh(mesh, atlas, knives.bones(n), f"geometry.bfk_{n}")
        (pack / "models" / "entity" / f"bfk_{n}.geo.json").write_text(json.dumps(geo, separators=(",", ":")))
        Image.fromarray(atlas.img, "RGBA").save(pack / "textures" / "bf_cosmetics" / f"{n}.png")
    (pack / "animations" / "bfk.animation.json").write_text(json.dumps(ANIMS, indent=1))
    (pack / "animation_controllers").mkdir(parents=True, exist_ok=True)
    (pack / "animation_controllers" / "bfk.animation_controllers.json").write_text(json.dumps(CTRLS, indent=1))
    (pack / "render_controllers" / "bfk.render_controllers.json").write_text(json.dumps(RC, indent=1))
    (pack / "particles").mkdir(parents=True, exist_ok=True)
    for fn, js in kn_anims.particle_files().items():
        (pack / "particles" / fn).write_text(json.dumps(js, indent=1))
    Image.fromarray(kn_anims.fx_texture(), "RGBA").save(pack / "textures" / "bf_cosmetics" / "fx.png")
    kn_sounds.write(pack / "sounds" / "bf_cosmetics")
    (pack / "sounds" / "sound_definitions.json").write_text(json.dumps(kn_sounds.definitions(), indent=1))


def build(vanilla_items: Path, out: Path):
    import iconmodel
    if out.exists():
        shutil.rmtree(out)
    (out / "attachables").mkdir(parents=True)
    write_shared(out)
    iconmodel.PX = 1.0            # 16 px vanilla sprite -> 16 units, the same size as the 32 px World FX icons
    for item, icon in SWORDS.items():
        src = vanilla_items / f"{icon}.png"
        if not src.exists():
            print("skip (no vanilla texture):", item)
            continue
        im = np.array(Image.open(src).convert("RGBA"))
        geo = iconmodel.model(im, f"geometry.bfk_{item}", grip=(2.75, 13.25))
        (out / "models" / "entity" / f"bfk_{item}.geo.json").write_text(json.dumps(geo, separators=(",", ":")))
        desc = {"identifier": f"minecraft:{item}",
                "materials": {"default": "entity_alphatest", "enchanted": "entity_alphatest_glint"},
                "textures": {"default": f"textures/items/{icon}", "enchanted": "textures/misc/enchanted_item_glint"},
                "geometry": {"default": f"geometry.bfk_{item}"},
                "animations": {"hold_first_person": "animation.bfk.hold_first_person",
                               "hold_third_person": "animation.bfk.hold_third_person"},
                "scripts": {"animate": [{"hold_first_person": "c.is_first_person"}, {"hold_third_person": "!c.is_first_person"}]},
                "render_controllers": ["controller.render.item_default"]}
        add_knives(desc)
        (out / "attachables" / f"{item}.json").write_text(json.dumps({"format_version": "1.10.0", "minecraft:attachable": {"description": desc}}, indent=1))
    manifest = {"format_version": 2, "header": {
        "name": "BF Cosmetics", "description": "Bedrock Forge 3D cosmetics by Novak: Knives (switched in the Right Shift menu, Cosmetics tab)",
        "uuid": PACK_UUID[0], "version": VERSION, "min_engine_version": [1, 21, 0]},
        "modules": [{"type": "resources", "uuid": PACK_UUID[1], "version": VERSION}]}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print("built", out)


if __name__ == "__main__":
    build(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parent.parent / "packs" / "BFCosmetics")
