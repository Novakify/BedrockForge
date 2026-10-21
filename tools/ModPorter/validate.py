#!/usr/bin/env python3
"""Sanity-check a generated Bedrock RP+BP pair: JSON parses, every client entity's geometry/texture/animation
exists, animated bones exist in the geometry, UVs fit the texture, BP and RP identifiers match."""
import json, sys
from pathlib import Path
from PIL import Image
root = Path(sys.argv[1]); rp = next(root.glob("* RP")); bp = next(root.glob("* BP"))
errs, warns = [], []
def load(p):
    try: return json.loads(p.read_text())
    except Exception as e: errs.append(f"{p.relative_to(root)}: bad JSON {e}"); return {}
for p in list(rp.rglob("*.json")) + list(bp.rglob("*.json")): load(p)
rcs = {"controller.render.default"}
for p in (rp / "render_controllers").glob("*.json") if (rp / "render_controllers").exists() else []: rcs |= set(load(p).get("render_controllers", {}))
bp_manifest = load(bp / "manifest.json")
for mod in bp_manifest.get("modules", []):
    if mod.get("type") == "script" and not (bp / mod.get("entry", "")).exists(): errs.append(f"BP script entry {mod.get('entry')} missing")
geos = {}
for p in (rp / "models/entity").glob("*.json"):
    for g in load(p).get("minecraft:geometry", []):
        d = g["description"]; bones = {b["name"]: b for b in g.get("bones", [])}
        geos[d["identifier"]] = (d.get("texture_width", 64), d.get("texture_height", 64), bones, p.name)
anims = {}
for p in (rp / "animations").glob("*.json"):
    for k, v in load(p).get("animations", {}).items(): anims[k] = set((v.get("bones") or {}).keys())
ctrls = set()
for p in (rp / "animation_controllers").glob("*.json"): ctrls |= set(load(p).get("animation_controllers", {}).keys())
rp_ids = set()
for p in (rp / "entity").glob("*.json"):
    d = load(p)["minecraft:client_entity"]["description"]; rp_ids.add(d["identifier"])
    for rc in d.get("render_controllers", []):
        rcn = rc if isinstance(rc, str) else next(iter(rc))
        if rcn not in rcs: errs.append(f"{p.name}: render controller {rcn} missing")
    gid = d["geometry"]["default"]
    if gid != "geometry.humanoid.custom" and gid not in geos: errs.append(f"{p.name}: geometry {gid} missing")
    for k, t in d["textures"].items():
        if not (rp / (t + ".png")).exists(): errs.append(f"{p.name}: texture '{k}' {t} missing")
    for k, m in d.get("materials", {}).items():
        if not isinstance(m, str): errs.append(f"{p.name}: material {k} not a string")
    tex = rp / (d["textures"]["default"] + ".png")
    if not tex.exists(): pass
    elif gid in geos:
        tw, th, bones, gname = geos[gid]; im = Image.open(tex)
        if im.size[0] * th != im.size[1] * tw: warns.append(f"{p.name}: texture {im.size} aspect differs from geo {tw}x{th}")
        for k, a in (d.get("animations") or {}).items():
            if a.startswith("controller."):
                if a not in ctrls: errs.append(f"{p.name}: controller {a} missing")
                continue
            if a not in anims: errs.append(f"{p.name}: animation {a} missing"); continue
            missing = anims[a] - set(bones)
            if missing: warns.append(f"{p.name}: {a} animates bones not in {gname}: {sorted(missing)[:5]}")
bp_ids = {load(p)["minecraft:entity"]["description"]["identifier"] for p in (bp / "entities").glob("*.json")}
if bp_ids != rp_ids: errs.append(f"BP/RP identifier mismatch: {sorted(bp_ids ^ rp_ids)[:5]}")
print(f"{len(rp_ids)} entities, {len(geos)} geometries, {len(anims)} animations, {len(ctrls)} controllers")
print(f"ERRORS ({len(errs)}):", *errs[:30], sep="\n  "); print(f"WARNINGS ({len(warns)}):", *warns[:30], sep="\n  ")
sys.exit(1 if errs else 0)
