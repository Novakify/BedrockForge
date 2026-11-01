#!/usr/bin/env python3
"""Compile the FiguraBridge test avatars and check invariants.

    python3 -I tools/FiguraBridge/test_compile.py [--vanilla RP] [--out DIR]

Avatars (read-only inputs): /home/claude/untrusted/stick, .../avatars/StrawberryMiku, .../avatars/FiguraAvatarTemplate
Output: <out>/<name>/ (default /home/claude/out/compiled). Exit code 1 if any check fails.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compile as fbc  # noqa: E402
import figura_nbt as fn  # noqa: E402

AVATARS = {
    "stick": Path("/home/claude/untrusted/stick"),
    "StrawberryMiku": Path("/home/claude/untrusted/avatars/StrawberryMiku"),
    "FiguraAvatarTemplate": Path("/home/claude/untrusted/avatars/FiguraAvatarTemplate"),
    "CatMaid": Path("/home/claude/untrusted/avatars/CatMaid"),
    "SenkoSan": Path("/home/claude/untrusted/avatars/SenkoSan"),
}
KNOWN_ELEMENTS = ("cube", "mesh", "locator", "null_object")


def expected_model_parts(bb: dict) -> tuple[int, set]:
    """Independent count of the parts Figura makes from one .bbmodel (root + exported groups + known elements),
    and the uuids of the exported groups (the only parts that receive 'anim')."""
    els = {e.get("uuid"): e for e in bb.get("elements", [])}
    v5 = str(bb["meta"]["format_version"]).startswith("5")
    groups = {g["uuid"]: g for g in bb.get("groups", [])} if v5 else {}
    count, group_uuids = 1, set()

    def walk(items):
        nonlocal count
        for it in items:
            if isinstance(it, str):
                e = els.get(it)
                if e and e.get("type") in KNOWN_ELEMENTS and e.get("export") is not False:
                    count += 1
            else:
                g = groups.get(it["uuid"]) if v5 else it
                if g is None or g.get("export") is False or (v5 and it["uuid"] in els and it["uuid"] not in groups):
                    continue
                count += 1
                group_uuids.add(it["uuid"])
                walk(it.get("children", []))

    walk(bb.get("outliner", []))
    return count, group_uuids


def check(name: str, src: Path, out: Path, vanilla: Path) -> tuple[dict, list]:
    errs = []
    summary = fbc.build(src, out, vanilla, "00000000-0000-0000-0000-000000000000")

    # every JSON file parses
    for p in out.rglob("*.json"):
        try:
            json.loads(p.read_text(encoding="utf-8"))
        except Exception as e:  # noqa: BLE001
            errs.append(f"invalid JSON {p}: {e}")
    nbt = json.loads((out / "avatar/figura_avatar.json").read_text(encoding="utf-8"))
    rig = json.loads((out / "rig.json").read_text(encoding="utf-8"))
    geo_file = next((out / "rp/models/entity").glob("*.geo.json"))
    geos = json.loads(geo_file.read_text())["minecraft:geometry"]
    rcs = json.loads((out / "rp/render_controllers/figura_player.render_controllers.json").read_text())["render_controllers"]
    ent = json.loads((out / "rp/entity/player.entity.json").read_text())["minecraft:client_entity"]["description"]

    # side files referenced by the NBT exist
    def files(v):
        if isinstance(v, dict):
            if "__file" in v and len(v) == 1:
                yield v["__file"]
            else:
                for x in v.values():
                    yield from files(x)
        elif isinstance(v, list):
            for x in v:
                yield from files(x)
    for rel in files(nbt):
        if not (out / "avatar" / rel).is_file():
            errs.append(f"missing side file {rel}")

    # part count vs elements + groups (+ folders + "models" root)
    bbs = [p for p in fn.files_by_extension(src.resolve(), ".bbmodel")]
    exp, group_uuids, n_anim_groups = 1, set(), 0
    folders = {p.parent for p in bbs if p.parent != src.resolve()}
    exp += len(folders)
    for p in bbs:
        bb = json.loads(p.read_text(encoding="utf-8-sig"))
        c, gu = expected_model_parts(bb)
        exp += c
        group_uuids |= gu
        n_anim_groups += sum(1 for a in bb.get("animations", []) for k in (a.get("animators") or {}) if k in gu)
    if len(rig["parts"]) != exp:
        errs.append(f"part count {len(rig['parts'])} != expected {exp}")

    # walk NBT: texture ids valid, animation attachments valid
    n_sets = len(nbt.get("textures", {}).get("data", []))
    n_anims = len(nbt.get("animations", []))
    anim_attach = 0
    animated_parts = set()

    def walk(t, path):
        nonlocal anim_attach
        for f in (t.get("cube_data") or {}).values():
            if not 0 <= f["tex"] < n_sets:
                errs.append(f"{path}: cube face tex {f['tex']} out of range")
        md = t.get("mesh_data")
        if md:
            for packed in md["tex"]:
                if not 0 <= (packed >> 4) < n_sets:
                    errs.append(f"{path}: mesh face tex {packed >> 4} out of range")
            if sum(p & 0xF for p in md["tex"]) != len(md["fac"]) or len(md["fac"]) * 2 != len(md["uvs"]):
                errs.append(f"{path}: mesh_data sizes inconsistent")
        for a in t.get("anim", []):
            anim_attach += 1
            animated_parts.add(path)
            if not 0 <= a["id"] < n_anims:
                errs.append(f"{path}: anim id {a['id']} out of range")
        for c in t.get("chld", []):
            walk(c, path + "." + c["name"])
    walk(nbt["models"], "models")
    rig_paths = {p["path"] for p in rig["parts"]}
    for ap in animated_parts:
        if ap not in rig_paths:
            errs.append(f"animated part {ap} missing from rig")
    # every animator (except "effects") that targets an exported group is attached to that group
    if anim_attach > n_anim_groups:
        errs.append(f"{anim_attach} anim attachments > {n_anim_groups} group animators")

    # texture files / sets
    for s in rig["textureSets"]:
        for t in ("d", "e", "n", "s"):
            if s[t] is not None and not 0 <= s[t] < len(rig["textureFiles"]):
                errs.append(f"set {s['i']} {t} -> bad file index")

    # geometry chunks: each <= BONE_LIMIT bones, unique names, parents exist in the same chunk
    nameset, chunk_sets = set(), []
    for g in geos:
        names = [b["name"] for b in g["bones"]]
        cs = set(names)
        chunk_sets.append(cs)
        nameset |= cs
        if len(cs) != len(names):
            errs.append(f"{g['description']['identifier']}: duplicate bone names")
        if len(names) > fbc.BONE_LIMIT:
            errs.append(f"{g['description']['identifier']}: {len(names)} bones > {fbc.BONE_LIMIT}")
        for b in g["bones"]:
            if "parent" in b and b["parent"] not in cs:
                errs.append(f"bone {b['name']} parent {b['parent']} missing in its chunk")
    seen = set()
    for g in geos:
        for b in g["bones"]:
            if b["name"] in seen and (b.get("cubes") or b.get("poly_mesh")):
                errs.append(f"geometry bone {b['name']} in two chunks")
            if b.get("cubes") or b.get("poly_mesh"):
                seen.add(b["name"])
    geo = {"bones": [b for g in geos for b in g["bones"]]}
    for b in geo["bones"]:
        if "rotation" in b:
            errs.append(f"bone {b['name']} has a bind rotation")
        for c in b.get("cubes", []):
            if "rotation" in c:
                errs.append(f"cube in {b['name']} has a rotation")
        pm = b.get("poly_mesh")
        if pm:
            for poly in pm["polys"]:
                if len(poly) != 4 or any(v[0] >= len(pm["positions"]) or v[2] >= len(pm["uvs"]) for v in poly):
                    errs.append(f"bad poly in {b['name']}")
    for p in rig["parts"]:
        for s in p.get("subs", []):
            if s["bone"] not in nameset:
                errs.append(f"sub bone {s['bone']} missing")
        if p["parent"] >= p["i"]:
            errs.append(f"part {p['i']} not in pre-order")

    # render controllers: bones exist, textures declared, all controllers referenced by the entity
    ent_rcs = {next(iter(r)) if isinstance(r, dict) else r for r in ent["render_controllers"]}
    for k, rc in rcs.items():
        gk = rc["geometry"].split(".", 1)[1]
        cidx = int(gk[len("figura"):] or 0)
        for entry in rc["part_visibility"]:
            for b in entry:
                if b != "*" and b not in chunk_sets[cidx]:
                    errs.append(f"{k}: bone {b} not in its geometry chunk")
        tex_key = rc["textures"][0].split(".", 1)[1]
        if tex_key not in ent["textures"]:
            errs.append(f"{k}: texture {tex_key} not in entity")
        if not (out / "rp" / (ent["textures"][tex_key] + ".png")).is_file():
            errs.append(f"{k}: texture file missing")
        if k not in ent_rcs:
            errs.append(f"{k} not in player.entity.json")
    anim = json.loads((out / "rp/animations/figura_drive.animation.json").read_text())
    drive = set(anim["animations"]["animation.figura.drive"]["bones"])
    part_bones = {p["bone"] for p in rig["parts"]} | {f"p{d['i']}" for d in rig.get("dups", [])}
    fp = {b for b in drive if b.startswith("f")}
    if drive - fp != part_bones:
        errs.append("drive animation bones != part bones")
    if any(f"p{b[1:]}" not in part_bones for b in fp):
        errs.append("first-person bone without a part")
    if "figura_drive" not in ent["scripts"]["animate"]:
        errs.append("drive animation not animated")

    # sounds
    sd = out / "rp/sounds/sound_definitions.json"
    if rig["sounds"]:
        defs = json.loads(sd.read_text())["sound_definitions"]
        for ev in rig["sounds"].values():
            for s in defs[ev]["sounds"]:
                if not (out / "rp" / (s["name"] + ".ogg")).is_file():
                    errs.append(f"sound file for {ev} missing")

    types = {}
    for p in rig["parts"]:
        types[p["type"]] = types.get(p["type"], 0) + 1
    summary.update({"partTypes": types, "animAttachments": anim_attach, "expectedParts": exp,
                    "geometryBones": len(names), "polyMeshBones": sum(1 for b in geo["bones"] if "poly_mesh" in b),
                    "cubes": sum(len(b.get("cubes", [])) for b in geo["bones"]),
                    "splitParts": sum(1 for p in rig["parts"] if "subs" in p)})
    return summary, errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vanilla", default="/home/claude/ref/bedrock-samples/resource_pack")
    ap.add_argument("--out", default="/home/claude/out/compiled")
    a = ap.parse_args()
    ok = True
    for name, src in AVATARS.items():
        summary, errs = check(name, src, Path(a.out) / name, Path(a.vanilla))
        s = summary
        print(f"[{'OK' if not errs else 'FAIL'}] {name}: parts={s['parts']} (expected {s['expectedParts']}) "
              f"types={s['partTypes']} bones={s['geometryBones']} poly_mesh={s['polyMeshBones']} cubes={s['cubes']} "
              f"split={s['splitParts']} textureFiles={s['textureFiles']} sets={s['textureSets']} "
              f"RCs={s['renderControllers']} animations={s['animations']} anim_attachments={s['animAttachments']} "
              f"sounds={s['sounds']} scripts={s['scripts']} warnings={len(s['warnings'])}")
        for e in errs[:30]:
            print("   -", e)
        ok &= not errs
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
