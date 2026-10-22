#!/usr/bin/env python3
"""Build the generated parts of the From The Fog (Bedrock) packs from the Java assets.

Usage: python3 tools/build_rp.py <java_repo_root> <ftfb_root>

Generates:
  RP textures/entity/ftf/skins/<id>/{skin,skin_emissive}.png   (64x64 Java skins, reused 1:1)
  RP textures/ftf/paintings/*.png, textures/ftf/ui/icons/*.png, textures/items/ftf_*.png
  RP sounds/ftf/**.ogg + sounds/sound_definitions.json
  RP models/entity/ftf_humanoid*.geo.json
  RP render_controllers/ftf_herobrine.render_controllers.json (skin texture arrays)
  RP entity/ftf_herobrine.entity.json
  BP scripts/skins.js (skin table: id, credit, slim, hidden parts)
  RP/BP texts/en_US.lang (merged with the hand-written base lang in tools/lang_base.lang)
"""
import json
import os
import re
import shutil
import sys

from PIL import Image

SRC, DST = sys.argv[1], sys.argv[2]
A = os.path.join(SRC, "assets", "lunareclipse.watching")
RP = os.path.join(DST, "packs", "FromTheFogRP")
BP = os.path.join(DST, "packs", "FromTheFogBP")
SKIN_DIR = os.path.join(A, "textures", "item", "skins")

# ---------------------------------------------------------------- skin table
def parse_skins():
    t = open(os.path.join(SRC, "data/lunareclipse.watching/function/sightings/skin_library/define_skins.mcfunction")).read().replace("\\\n", "")
    out = {}
    for m in re.finditer(r"define_skin_info \{(.*)", t):
        s = m.group(1)
        sid = re.search(r'id: *"([^"]+)"', s).group(1)
        credit = re.search(r'credit: *"([^"]*)"', s).group(1)
        parts = re.search(r"parts: *\[([^\]]*)\]", s).group(1).replace('"', "").replace(" ", "").split(",")
        out[sid] = (credit, parts)
    return out

# Skins whose Java model is not a plain player model (custom geometry, animated textures, 128px, animals)
EXCLUDE = {
    "freddy_fazbear", "lonely", "trench_diver", "white_pumpkin", "werewolf_brown", "werewolf_brown_clothed",
    "werewolf_gray", "werewolf_gray_clothed", "cursed_oak", "distorted", "farlander", "glitch", "jack_black",
    "nowhere", "slenderman", "the_man_from_the_fog", "white_eyes", "hoebrine",
}
SLIM = {"alex", "alex_classic", "comicalbonzo", "notvixios", "sqannky_duck", "the_librarian", "tokimi"}
# Config pages as in the Java skin library (only ids we ship are kept, order preserved)
PAGE_ORDER = (
    "default default_shaved classic classic_shaved lunar_eclipse_version millerbrine millerbrine_classic him glitch "
    "lunar_eclipse apparition zombie nullified fallen_kingdom sculk_infested crimson_infested smile shattered birthday jack_black "
    "tennis_herobrine tuxedo_herobrine athlete_herobrine scottish_herobrine prisoner_herobrine cyclist_herobrine boxer_herobrine "
    "developer_herobrine lonely roseate trench_diver frozen_wanderer entity_303 entity_303_classic null black_eyed black_eyed_classic "
    "faceless distorted distorted_classic alex alex_classic lick lick_classic white_eyes hat_man nowhere werewolf the_man_from_the_fog "
    "slenderman cursed_oak freddy_fazbear the_librarian comicalbonzo tokimi notvixios pixelnaut derpblock vivilly mackheaththeknife "
    "farlander lumbering_jack grimreaper white_pumpkin ghost_costume the_headless mr_boots scarecrow invisible_man notch notch_v2 "
    "notch_classic notch_v2_classic swayle sqannky_duck white_eyes_classic"
).split()


def main():
    skins = parse_skins()
    lang = json.load(open(os.path.join(A, "lang", "en_us.json"), encoding="utf-8"))
    table = []
    for sid in PAGE_ORDER:
        if sid in EXCLUDE or sid not in skins:
            continue
        p = os.path.join(SKIN_DIR, sid, "skin.png")
        if not os.path.isfile(p) or Image.open(p).size != (64, 64):
            continue
        credit, parts = skins[sid]
        dst = os.path.join(RP, "textures", "entity", "ftf", "skins", sid)
        os.makedirs(dst, exist_ok=True)
        shutil.copy(p, os.path.join(dst, "skin.png"))
        em = os.path.join(SKIN_DIR, sid, "skin_emissive.png")
        if os.path.isfile(em) and Image.open(em).size == (64, 64):
            shutil.copy(em, os.path.join(dst, "skin_emissive.png"))
        else:
            Image.new("RGBA", (64, 64), (0, 0, 0, 0)).save(os.path.join(dst, "skin_emissive.png"))
        table.append({
            "id": sid,
            "credit": credit,
            "name": lang.get(f"entity.nametag.{sid}.value", "Herobrine"),
            "slim": sid in SLIM,
            "arms": "left_arm" in parts,
            "legs": "left_leg" in parts,
            "body": "body" in parts,
            "hidden": sid in ("swayle", "sqannky_duck", "white_eyes_classic"),
        })
    print(f"skins: {len(table)}")

    # ------------------------------------------------ BP skin table
    with open(os.path.join(BP, "scripts", "skins.js"), "w", encoding="utf-8") as f:
        f.write("// Built by tools/build_rp.py from the Java skin library. Index = ftf:skin property value.\n")
        f.write("export const SKINS = " + json.dumps(table, indent=1, ensure_ascii=False) + ";\n")

    # ------------------------------------------------ render controllers
    tex = [f"Texture.s{i}" for i in range(len(table))]
    emi = [f"Texture.e{i}" for i in range(len(table))]
    geos = ["Geometry.slim" if s["slim"] else "Geometry.default" for s in table]
    vis = [
        {"*": True},
        {"leftArm": "!q.property('ftf:hide_arms')"}, {"rightArm": "!q.property('ftf:hide_arms')"},
        {"leftLeg": "!q.property('ftf:hide_legs')"}, {"rightLeg": "!q.property('ftf:hide_legs')"},
        {"body": "!q.property('ftf:hide_body')"},
        {"head": "!q.property('ftf:hide_head')"},
    ]
    rc = {
        "format_version": "1.10.0",
        "render_controllers": {
            "controller.render.ftf.herobrine": {
                "arrays": {"textures": {"Array.skins": tex}, "geometries": {"Array.geos": geos}},
                "geometry": "Array.geos[q.property('ftf:skin')]",
                "materials": [{"*": "Material.default"}],
                "textures": ["Array.skins[q.property('ftf:skin')]"],
                "part_visibility": vis,
            },
            "controller.render.ftf.herobrine_emissive": {
                "arrays": {"textures": {"Array.emissive": emi}, "geometries": {"Array.geos": geos}},
                "geometry": "Array.geos[q.property('ftf:skin')]",
                "materials": [{"*": "Material.emissive"}],
                "textures": ["Array.emissive[q.property('ftf:skin')]"],
                "ignore_lighting": True,
                "part_visibility": vis,
            },
        },
    }
    os.makedirs(os.path.join(RP, "render_controllers"), exist_ok=True)
    json.dump(rc, open(os.path.join(RP, "render_controllers", "ftf_herobrine.render_controllers.json"), "w"), indent=1)

    # ------------------------------------------------ client entity
    textures = {f"s{i}": f"textures/entity/ftf/skins/{s['id']}/skin" for i, s in enumerate(table)}
    textures.update({f"e{i}": f"textures/entity/ftf/skins/{s['id']}/skin_emissive" for i, s in enumerate(table)})
    ce = {
        "format_version": "1.10.0",
        "minecraft:client_entity": {
            "description": {
                "identifier": "ftf:herobrine",
                "materials": {"default": "entity_alphatest", "emissive": "entity_alphatest"},
                "textures": textures,
                "geometry": {"default": "geometry.ftf.humanoid", "slim": "geometry.ftf.humanoid_slim"},
                "animations": {
                    "look_at": "animation.ftf.herobrine.look_at",
                    "breathe": "animation.ftf.herobrine.breathe",
                    "walk": "animation.ftf.herobrine.walk",
                    "punch": "animation.ftf.herobrine.punch",
                    "holding": "animation.ftf.herobrine.holding",
                    "tilt": "animation.ftf.herobrine.tilt",
                    "ctrl": "controller.animation.ftf.herobrine",
                },
                "scripts": {
                    "animate": [
                        "look_at", "ctrl",
                        {"holding": "q.property('ftf:holding')"},
                        {"tilt": "q.property('ftf:tilt')"},
                    ]
                },
                "render_controllers": [
                    "controller.render.ftf.herobrine",
                    {"controller.render.ftf.herobrine_emissive": "q.property('ftf:emissive')"},
                ],
                "enable_attachables": True,
                "hide_armor": True,
                "spawn_egg": {"base_color": "#3a2a1a", "overlay_color": "#ffffff"},
            }
        },
    }
    os.makedirs(os.path.join(RP, "entity"), exist_ok=True)
    json.dump(ce, open(os.path.join(RP, "entity", "ftf_herobrine.entity.json"), "w"), indent=1)

    # ------------------------------------------------ geometry
    def geo(ident, slim):
        aw = 3 if slim else 4
        def cube(o, s, uv, inflate=0.0):
            c = {"origin": o, "size": s, "uv": uv}
            if inflate: c["inflate"] = inflate
            return c
        bones = [
            {"name": "root", "pivot": [0, 0, 0]},
            {"name": "body", "parent": "root", "pivot": [0, 24, 0], "cubes": [cube([-4, 12, -2], [8, 12, 4], [16, 16]), cube([-4, 12, -2], [8, 12, 4], [16, 32], 0.25)]},
            {"name": "head", "parent": "root", "pivot": [0, 24, 0], "cubes": [cube([-4, 24, -4], [8, 8, 8], [0, 0]), cube([-4, 24, -4], [8, 8, 8], [32, 0], 0.5)]},
            {"name": "rightArm", "parent": "root", "pivot": [-5, 22, 0], "cubes": [cube([-4 - aw, 12, -2], [aw, 12, 4], [40, 16]), cube([-4 - aw, 12, -2], [aw, 12, 4], [40, 32], 0.25)]},
            {"name": "rightItem", "parent": "rightArm", "pivot": [-6, 15, 1]},
            {"name": "leftArm", "parent": "root", "pivot": [5, 22, 0], "cubes": [cube([4, 12, -2], [aw, 12, 4], [32, 48]), cube([4, 12, -2], [aw, 12, 4], [48, 48], 0.25)]},
            {"name": "rightLeg", "parent": "root", "pivot": [-1.9, 12, 0], "cubes": [cube([-3.9, 0, -2], [4, 12, 4], [0, 16]), cube([-3.9, 0, -2], [4, 12, 4], [0, 32], 0.25)]},
            {"name": "leftLeg", "parent": "root", "pivot": [1.9, 12, 0], "cubes": [cube([-0.1, 0, -2], [4, 12, 4], [16, 48]), cube([-0.1, 0, -2], [4, 12, 4], [0, 48], 0.25)]},
        ]
        return {"description": {"identifier": ident, "texture_width": 64, "texture_height": 64, "visible_bounds_width": 2, "visible_bounds_height": 3, "visible_bounds_offset": [0, 1.5, 0]}, "bones": bones}
    os.makedirs(os.path.join(RP, "models", "entity"), exist_ok=True)
    json.dump({"format_version": "1.12.0", "minecraft:geometry": [geo("geometry.ftf.humanoid", False), geo("geometry.ftf.humanoid_slim", True)]},
              open(os.path.join(RP, "models", "entity", "ftf_humanoid.geo.json"), "w"), indent=1)

    # ------------------------------------------------ paintings, discs, icons
    pdst = os.path.join(RP, "textures", "ftf", "paintings")
    os.makedirs(os.path.join(pdst, "twisted_tapestries"), exist_ok=True)
    for n in ("a_distant_memory", "redstone_halls", "watching"):
        shutil.copy(os.path.join(A, "textures", "painting", n + ".png"), os.path.join(pdst, n + ".png"))
    for fn in os.listdir(os.path.join(A, "textures", "painting", "twisted_tapestries")):
        if fn.endswith(".png"):
            shutil.copy(os.path.join(A, "textures", "painting", "twisted_tapestries", fn), os.path.join(pdst, "twisted_tapestries", fn))
    idst = os.path.join(RP, "textures", "items")
    os.makedirs(idst, exist_ok=True)
    for n in ("deathbed", "herobrine_lullaby", "hes_from_the_fog"):
        shutil.copy(os.path.join(A, "textures", "item", n + ".png"), os.path.join(idst, f"ftf_disc_{n}.png"))
    icon_src = os.path.join(A, "textures", "item", "ui", "config", "icons")
    icon_dst = os.path.join(RP, "textures", "ftf", "ui", "icons")
    os.makedirs(icon_dst, exist_ok=True)
    for d in sorted(os.listdir(icon_src)):
        files = [f for f in sorted(os.listdir(os.path.join(icon_src, d))) if f.endswith(".png")]
        if not files:
            continue
        back = "backdrop.png" if "backdrop.png" in files else files[0]
        base = Image.open(os.path.join(icon_src, d, back)).convert("RGBA")
        h = base.size[0]
        # crop animated strips to the first frame
        base = base.crop((0, 0, h, h))
        for f in files:
            if f == back or "old" in f or "day" in f:
                continue
            im = Image.open(os.path.join(icon_src, d, f)).convert("RGBA")
            w = im.size[0]
            im = im.crop((0, 0, w, w))
            if im.size == base.size:
                base.alpha_composite(im)
        base.save(os.path.join(icon_dst, d + ".png"))
    # journal item icon = skin_library icon
    shutil.copy(os.path.join(icon_dst, "sightings.png"), os.path.join(idst, "ftf_fog_journal.png"))
    json.dump({"resource_pack_name": "ftf", "texture_name": "atlas.items", "texture_data": {
        "ftf_fog_journal": {"textures": "textures/items/ftf_fog_journal"},
        **{f"ftf_disc_{n}": {"textures": f"textures/items/ftf_disc_{n}"} for n in ("deathbed", "herobrine_lullaby", "hes_from_the_fog")},
    }}, open(os.path.join(RP, "textures", "item_texture.json"), "w"), indent=1)

    # ------------------------------------------------ sounds
    sj = json.load(open(os.path.join(A, "sounds.json")))
    sdefs = {}
    used = set()
    for key, v in sj.items():
        cat = "ui" if key.startswith("config.") else ("record" if key.startswith("music_disc") else "hostile")
        sounds = []
        for s in v["sounds"]:
            name = s["name"] if isinstance(s, str) else s["name"]
            rel = name.split(":", 1)[1]
            used.add(rel)
            e = {"name": "sounds/ftf/" + rel}
            if isinstance(s, dict):
                if s.get("volume", 1) != 1: e["volume"] = s["volume"]
                if s.get("pitch", 1) != 1: e["pitch"] = s["pitch"]
                if s.get("stream"): e["stream"] = True
            if cat == "record": e["stream"] = True
            sounds.append(e)
        sdefs["ftf." + key] = {"category": cat, "sounds": sounds}
        if cat == "record":
            sdefs["ftf." + key]["max_distance"] = 64
    for rel in used:
        src = os.path.join(A, "sounds", rel + ".ogg")
        dst = os.path.join(RP, "sounds", "ftf", rel + ".ogg")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(src, dst)
    json.dump({"format_version": "1.20.20", "sound_definitions": sdefs}, open(os.path.join(RP, "sounds", "sound_definitions.json"), "w"), indent=1)
    print(f"sounds: {len(sdefs)} events, {len(used)} files")

    # ------------------------------------------------ lang
    base = open(os.path.join(DST, "tools", "lang_base.lang"), encoding="utf-8").read()
    lines = [base.rstrip("\n"), "", "## Converted from assets/lunareclipse.watching/lang/en_us.json"]
    for k, v in lang.items():
        if k.startswith(("sinister_signs.", "caution_caption.", "dreadful_donation.", "jukebox_song.", "painting.", "eerie_entrance.", "entity.nametag.")):
            lines.append(f"ftf.{k}={v}")
    text = "\n".join(lines) + "\n"
    for pack in (RP, BP):
        os.makedirs(os.path.join(pack, "texts"), exist_ok=True)
        open(os.path.join(pack, "texts", "en_US.lang"), "w", encoding="utf-8").write(text)
        json.dump(["en_US"], open(os.path.join(pack, "texts", "languages.json"), "w"))


if __name__ == "__main__":
    main()
