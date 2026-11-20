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
import finishes                    # noqa: E402
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
PRE = kn_anims.PRE + kn_anims.swinging_expr()

def tex_name(knife, finish):
    return knife if finish == "factory" else f"{knife}_{finish}"


RC = {"format_version": "1.8.0", "render_controllers": {"controller.render.bfk_knife": {
    "arrays": {"geometries": {"Array.kgeo": [f"Geometry.{n}" for n, _ in KNIVES]},
               # knife-major: (knife - 1) * 4 + finish (finishes.NAMES; Cosmetics.cpp picks / randomises v.bfkfin)
               "textures": {"Array.ktex": [f"Texture.{tex_name(n, f)}" for n, _ in KNIVES for f in finishes.NAMES]}},
    "geometry": "Array.kgeo[v.bfk - 1]",
    "materials": [{"*": "(v.is_enchanted && v.bfk_glint) ? Material.enchanted : Material.default"}],
    "textures": [f"Array.ktex[(v.bfk - 1) * {len(finishes.NAMES)} + v.bfk_fin]", "Texture.enchanted"]}}}


# afterimage: the ghost copies of the knife (anims.GHOSTS, see anims.build), drawn see-through over the knife
# (Fernando 2026-11-19: "ours is just grey - theirs keeps the weapon's colour, and it should be a lot more noticeable")
# each copy now wears the knife's own texture (its finish), from a stack of fading copies of it
RC["render_controllers"]["controller.render.bfk_ghost"] = {
    "arrays": {"geometries": {"Array.ggeo": [f"Geometry.{n}_ghost" for n, _ in KNIVES]},
               "textures": {"Array.gtex": [f"Texture.{tex_name(n, f)}_ghost" for n, _ in KNIVES for f in finishes.NAMES]}},
    "geometry": "Array.ggeo[v.bfk - 1]",
    "materials": [{"*": "Material.ghost"}],
    "textures": [f"Array.gtex[(v.bfk - 1) * {len(finishes.NAMES)} + v.bfk_fin]"]}
GHOST_ON = ("v.bfk > 0 && v.bfk_ghost && (v.bfk_swinging || v.bfk_d * v.bfk_spd < 1.2 || v.bfk_i * v.bfk_spd < 2.8)")


def ghost_geo(geo, ident):
    """the knife's geometry -> GHOSTS copies of its bone tree (bfg{k}, bfg{k}_pose, ...) under bfk_fix; copy k's faces
    use the knife's own uvs moved into slice k of the ghost texture (GHOSTS copies of the knife's texture stacked top
    to bottom, each fading more - ghost_stack)"""
    g0 = geo["minecraft:geometry"][0]
    src = g0["bones"]
    N = kn_anims.GHOSTS
    bones = [b for b in src if b["name"] in ("wfx_item", "bfk_fix")]
    for k in range(1, N + 1):
        bones.append({"name": f"bfg{k}", "parent": "bfk_fix", "pivot": [0, 24, 0]})
        s = k - 1                                           # slice (0 = the top rows)
        for b in src:
            if b["name"] in ("wfx_item", "bfk_fix"):
                continue
            nb = {kk: v for kk, v in b.items() if kk not in ("poly_mesh", "locators")}
            nb["name"] = kn_anims.ghost_bone(b["name"], k)
            nb["parent"] = f"bfg{k}" if b.get("parent") == "bfk_fix" else kn_anims.ghost_bone(b["parent"], k)
            if "poly_mesh" in b:
                pm = b["poly_mesh"]
                uvs = [[u, (N - s - 1 + v) / N] for u, v in pm["uvs"]]   # (poly_mesh v runs up from the bottom)
                nb["poly_mesh"] = {"normalized_uvs": True, "positions": pm["positions"], "normals": pm["normals"], "uvs": uvs,
                                   "polys": pm["polys"]}
            bones.append(nb)
    d = dict(g0["description"], identifier=ident)
    d["texture_height"] = d.get("texture_height", 16) * N
    return {"format_version": "1.16.0", "minecraft:geometry": [{"description": d, "bones": bones}]}


def ghost_stack(img):
    """the knife's texture -> GHOSTS copies stacked top to bottom: copy 1 (newest) most solid, fading to the oldest;
    the knife's own colours, lifted a little so the trail glows"""
    N = kn_anims.GHOSTS
    out = []
    h, w = img.shape[:2]
    small = np.asarray(Image.fromarray(img, "RGBA").resize((w // 2, h // 2), Image.LANCZOS))   # half size: 12 copies stay light
    for k in range(N):
        a = 0.72 * (1 - k / N) ** 1.2
        c = small.astype(np.float32).copy()
        c[..., :3] = np.minimum(255.0, c[..., :3] * 1.12 + 18.0)
        c[..., 3] = c[..., 3] * a
        out.append(c.astype(np.uint8))
    return np.concatenate(out, axis=0)


def add_knives(desc):
    """patch a sword attachable description (dict) so it shows our knife (with its tricks, swings, fx and sounds)
    while v.bfk > 0"""
    desc.setdefault("materials", {}).update({"default": desc["materials"].get("default", "entity_alphatest"),
                                             "enchanted": desc["materials"].get("enchanted", "entity_alphatest_glint")})
    desc.setdefault("textures", {}).setdefault("enchanted", "textures/misc/enchanted_item_glint")
    for n, _ in KNIVES:
        for f in finishes.NAMES:
            desc["textures"][tex_name(n, f)] = f"textures/bf_cosmetics/{tex_name(n, f)}"
        desc["geometry"][n] = f"geometry.bfk_{n}"
        desc["geometry"][f"{n}_ghost"] = f"geometry.bfk_{n}_ghost"
        for f in finishes.NAMES:
            desc["textures"][f"{tex_name(n, f)}_ghost"] = f"textures/bf_cosmetics/{tex_name(n, f)}_ghost"
    desc["materials"]["ghost"] = "entity_alphablend"
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
            if r in ("controller.render.bfk_knife", "controller.render.bfk_ghost"):
                continue
            rcs.append({r: "v.bfk == 0"})
        else:
            k, cond = next(iter(r.items()))
            if k not in ("controller.render.bfk_knife", "controller.render.bfk_ghost"):
                rcs.append({k: f"({cond}) && v.bfk == 0"})
    desc["render_controllers"] = rcs + [{"controller.render.bfk_knife": "v.bfk > 0"}, {"controller.render.bfk_ghost": GHOST_ON}]
    return desc


def write_shared(pack: Path):
    """the knife models, textures, animations and render controller (every pack that calls add_knives needs them)"""
    for d in ("models/entity", "textures/bf_cosmetics", "animations", "render_controllers"):
        (pack / d).mkdir(parents=True, exist_ok=True)
    for n, fn in KNIVES:
        mesh, atlas = fn()                       # every knife has its own model AND its own finishes / atlas
        geo = export_poly_mesh(mesh, atlas, knives.bones(n), f"geometry.bfk_{n}")
        (pack / "models" / "entity" / f"bfk_{n}.geo.json").write_text(json.dumps(geo, separators=(",", ":")))
        (pack / "models" / "entity" / f"bfk_{n}_ghost.geo.json").write_text(
            json.dumps(ghost_geo(geo, f"geometry.bfk_{n}_ghost"), separators=(",", ":")))
        for f in finishes.NAMES:
            img = finishes.apply(n, atlas.img, atlas.rect, f)
            Image.fromarray(img, "RGBA").save(pack / "textures" / "bf_cosmetics" / f"{tex_name(n, f)}.png")
            Image.fromarray(ghost_stack(img), "RGBA").save(pack / "textures" / "bf_cosmetics" / f"{tex_name(n, f)}_ghost.png")
    # one file per knife (+ the shared holds / hits): a bad clip then only costs its own knife, not every knife
    groups = {}
    for name, a in ANIMS["animations"].items():
        k = name.split(".")[2]
        groups.setdefault(k if k in kn_anims.KNIVES else "hits" if k.startswith("hit_") else "hold", {})[name] = a
    for old in (pack / "animations").glob("bfk*.animation.json"):
        old.unlink()
    for g, al in groups.items():
        (pack / "animations" / f"bfk_{g}.animation.json").write_text(
            json.dumps({"format_version": "1.10.0", "animations": al}, indent=1))
    stale = pack / "animation_controllers" / "bfk.animation_controllers.json"
    if stale.exists():
        stale.unlink()
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
