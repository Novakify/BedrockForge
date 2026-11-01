#!/usr/bin/env python3
"""Check a GeckoBridge RP + BP before it goes near the game (what the content log would complain about).

    python -I tools/GeckoBridge/validate.py "OUT/GB Mod RP" "OUT/GB Mod BP"

Every client entity: geometry ids exist, textures exist, every animation/controller key resolves, controller states
only use keys the entity defines, animated bones exist in the entity's geometry, render controllers exist and only
use Geometry./Texture./Material. keys the entity defines; all Molang parses (molang.py); BP <-> RP ids match;
sound effects resolve to sound_definitions with files present.
"""
import json, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import molang  # noqa: E402


def jl(p):
    t = re.sub(r"(?m)^\s*//[^\n]*", "", p.read_text("utf-8-sig"))
    return json.loads(t)


def main(rp, bp):
    rp, bp = Path(rp), Path(bp); errs, warns = [], []
    geos, anims, acs, rcs = {}, {}, {}, {}
    for p in rp.rglob("*.json"):
        try: d = jl(p)
        except Exception as e: errs.append(f"{p.relative_to(rp)}: bad JSON {e}"); continue
        for g in d.get("minecraft:geometry", []) if isinstance(d, dict) else []: geos[g["description"]["identifier"]] = g
        if isinstance(d, dict) and "animations" in d and "format_version" in d and "minecraft:client_entity" not in d: anims.update(d["animations"])
        if isinstance(d, dict): acs.update(d.get("animation_controllers", {})); rcs.update(d.get("render_controllers", {}))
    sdefs = {}
    sd = rp / "sounds" / "sound_definitions.json"
    if sd.exists():
        sdefs = jl(sd).get("sound_definitions", {})
        for k, v in sdefs.items():
            for s in v.get("sounds", []):
                n = s["name"] if isinstance(s, dict) else s
                if not (rp / (n + ".ogg")).exists(): errs.append(f"sound {k}: missing file {n}.ogg")
    bp_ids = {jl(p)["minecraft:entity"]["description"]["identifier"] for p in (bp / "entities").rglob("*.json")}
    n_ent = 0
    for p in (rp / "entity").rglob("*.json"):
        d = jl(p)["minecraft:client_entity"]["description"]; n_ent += 1; eid = d["identifier"]
        if eid not in bp_ids: errs.append(f"{eid}: no BP entity")
        for k, gid in d.get("geometry", {}).items():
            if gid not in geos: errs.append(f"{eid}: geometry {gid} missing")
        for k, t in d.get("textures", {}).items():
            if not (rp / (t + ".png")).exists(): errs.append(f"{eid}: texture {t}.png missing")
        bones = set()
        for gid in d.get("geometry", {}).values():
            bones |= {b["name"].lower() for b in geos.get(gid, {}).get("bones", [])}
        keys = d.get("animations", {})
        for k, a in keys.items():
            if a.startswith("controller.animation."):
                if a not in acs: errs.append(f"{eid}: controller {a} missing"); continue
                for sn, st in acs[a]["states"].items():
                    for an in st.get("animations", []):
                        name = an if isinstance(an, str) else next(iter(an))
                        if name not in keys: errs.append(f"{eid}: controller {a} state {sn} uses '{name}' not defined on the entity")
                    if "animations" in st and not st["animations"]: errs.append(f"{eid}: {a}/{sn} empty animations list")
                    for tr in st.get("transitions", []):
                        for target, expr in tr.items():
                            if target not in acs[a]["states"]: errs.append(f"{a}/{sn}: transition to unknown state {target}")
                            r = molang.check(expr)
                            if r: errs.append(f"{a}/{sn}->{target}: Molang {r}: {expr[:80]}")
            else:
                if a not in anims: errs.append(f"{eid}: animation {a} missing"); continue
                for b in anims[a].get("bones", {}):
                    if b.lower() not in bones: warns.append(f"{eid}: {a} animates bone '{b}' not in its geometry (Bedrock ignores it)")
                for t, se in (anims[a].get("sound_effects") or {}).items():
                    for one in (se if isinstance(se, list) else [se]):
                        if one["effect"] not in d.get("sound_effects", {}): errs.append(f"{eid}: {a} sound '{one['effect']}' not in sound_effects")
        if "animations" in d and not d["animations"]: errs.append(f"{eid}: empty animations map")
        for short, se in d.get("sound_effects", {}).items():
            if not isinstance(se, str): errs.append(f"{eid}: sound_effects.{short} must be a string (sound id)"); continue
            if se not in sdefs: errs.append(f"{eid}: sound {se} not defined")
        for s in d.get("scripts", {}).get("animate", []):
            name = s if isinstance(s, str) else next(iter(s))
            if name not in keys: errs.append(f"{eid}: animate '{name}' not defined")
        for s in d.get("scripts", {}).get("pre_animation", []):
            r = molang.check(s)
            if r: errs.append(f"{eid}: pre_animation Molang {r}: {s}")
        for rc in d.get("render_controllers", []):
            name = rc if isinstance(rc, str) else next(iter(rc))
            if name == "controller.render.default": continue
            if name not in rcs: errs.append(f"{eid}: render controller {name} missing"); continue
            r = rcs[name]
            refs = re.findall(r"(Geometry|Texture|Material)\.(\w+)", json.dumps(r))
            for kind, k in refs:
                pool = {"Geometry": d.get("geometry", {}), "Texture": d.get("textures", {}), "Material": d.get("materials", {})}[kind]
                if k not in pool and k != "*": errs.append(f"{eid}: {name} uses {kind}.{k} the entity doesn't define")
            for expr in [r.get("geometry", "")] + r.get("textures", []) + [v for pv in r.get("part_visibility", []) for v in pv.values()]:
                if isinstance(expr, str) and not re.fullmatch(r"(Geometry|Texture)\.\w+", expr):
                    m = molang.check(expr)
                    if m: errs.append(f"{eid}: {name} Molang {m}: {expr[:80]}")
    for a, ad in anims.items():
        if "bones" in ad and not ad["bones"]: errs.append(f"{a}: empty bones map")
        for bone, chans in (ad.get("bones") or {}).items():
            for ch, v in chans.items():
                vals = [v] if isinstance(v, list) else list(v.values()) if isinstance(v, dict) else []
                for kf in vals:
                    for x in (kf if isinstance(kf, list) else (kf.get("pre", []) + kf.get("post", []) if isinstance(kf, dict) else [])):
                        if isinstance(x, str) and molang.check(x): errs.append(f"{a}/{bone}/{ch}: Molang {molang.check(x)}: {x[:80]}")
    print(f"{n_ent} entities, {len(geos)} geometries, {len(anims)} animations, {len(acs)} controllers, {len(rcs)} render controllers")
    print(f"ERRORS ({len(errs)}):"); [print("  " + e) for e in errs[:40]]
    print(f"WARNINGS ({len(warns)}):"); [print("  " + w) for w in warns[:15]]
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
