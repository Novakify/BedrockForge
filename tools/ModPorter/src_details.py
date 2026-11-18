#!/usr/bin/env python3
"""Bedrock Forge ModPorter - stage 1 (SOURCE mode): extract entity data from a Forge mod's Java source tree.

For open-source mods (GitHub) there is no need for a jar or javap: the source uses official Mojang names,
so we read it with regexes. Produces the same entities.json as extract.py, plus a `<modid>-assets.zip`
built from src/main/resources so generate.py can be used unchanged:

  python extract_source.py path/to/repo -o out/
  python generate.py out/<modid>-assets.zip out/entities.json -o build/

Handles: DeferredRegister EntityType.Builder registrations (direct or through a helper like registerMob),
string-constant ids, createAttributes() with literals or static-final constants, GeckoLib
DefaultedEntityGeoModel / GeoModel paths, goal payload arrays, sounds via a ModSounds registry,
getMobType, overridden lifecycle methods (reported as events), GeckoLib `_glowmask` textures.
"""
from __future__ import annotations
import argparse, json, re, sys, zipfile
from pathlib import Path

ATTR = {"MAX_HEALTH": "max_health", "FOLLOW_RANGE": "follow_range", "KNOCKBACK_RESISTANCE": "knockback_resistance",
        "MOVEMENT_SPEED": "movement_speed", "FLYING_SPEED": "flying_speed", "ATTACK_DAMAGE": "attack_damage",
        "ATTACK_KNOCKBACK": "attack_knockback", "ATTACK_SPEED": "attack_speed", "ARMOR": "armor",
        "ARMOR_TOUGHNESS": "armor_toughness", "LUCK": "luck"}
EVENTS = {"tick": "tick", "aiStep": "tick", "customServerAiStep": "custom_server_ai_step", "hurt": "hurt", "die": "die",
          "mobInteract": "interact", "doHurtTarget": "do_hurt_target", "finalizeSpawn": "finalize_spawn",
          "addAdditionalSaveData": "add_save_data", "checkDespawn": "despawn", "removeWhenFarAway": "remove_when_far"}
SOUNDS = {"getAmbientSound": "ambient", "getHurtSound": "hurt", "getDeathSound": "death"}
NUM = r"-?\d+(?:\.\d+)?(?:[eE]-?\d+)?[fFdDL]?"

def num(s: str) -> float: return float(s.rstrip("fFdDL"))

def body_of(src: str, name: str) -> str:
    """Text of the first method called `name` (brace matched)."""
    m = re.search(rf"\b{name}\s*\([^)]*\)\s*(?:throws [\w., ]+)?\{{", src)
    if not m: return ""
    i, depth = m.end(), 1
    while i < len(src) and depth:
        depth += {"{": 1, "}": -1}.get(src[i], 0); i += 1
    return src[m.end():i - 1]

def strip_comments(t: str) -> str:
    return re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", t), flags=re.S)

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("repo", type=Path); ap.add_argument("-o", "--out", type=Path, default=Path("."))
    a = ap.parse_args(argv)
    res = a.repo / "src/main/resources"; java = a.repo / "src/main/java"
    modid = next(p.name for p in (res / "assets").iterdir() if p.is_dir() and p.name != "minecraft")
    files = {p.stem: p for p in java.rglob("*.java")}
    srcs = {k: strip_comments(p.read_text(errors="replace")) for k, p in files.items()}

    # string constants anywhere (ids are often `public static final String X_ID = "x";`)
    sconst = {m.group(1): m.group(2) for s in srcs.values() for m in re.finditer(r"\bString\s+(\w+)\s*=\s*\"([^\"]+)\"", s)}
    def sval(tok: str):
        tok = tok.strip()
        return tok.strip('"') if tok.startswith('"') else sconst.get(tok.split(".")[-1])

    # ---- registry ----
    reg = {}   # class -> {id, size, category}
    for k, s in srcs.items():
        if "EntityType" not in s or "register" not in s: continue
        helpers = set(re.findall(r"static\s+<[^>]+>\s+\S+\s+(\w+)\s*\(\s*String\s+\w+\s*,\s*EntityType\.EntityFactory<T>\s+\w+\s*,\s*float\s+\w+\s*,\s*float", s))
        for h in helpers:
            for m in re.finditer(rf"\b{h}\(\s*(\"[^\"]+\"|[\w.]+)\s*,\s*(\w+)::new\s*,\s*({NUM})\s*,\s*({NUM})", s):
                reg[m.group(2)] = {"id": sval(m.group(1)), "size": [num(m.group(3)), num(m.group(4))], "kind": "mob"}
        for m in re.finditer(rf"\.register\(\s*(\"[^\"]+\"|[\w.]+)\s*,\s*\(\)\s*->\s*EntityType\.Builder\.(?:<\w+>)?of\(\s*(\w+)::new\s*,\s*([\w.]+)\)"
                             rf"(?:\.sized\(\s*({NUM})\s*,\s*({NUM})\s*\))?", s):
            reg[m.group(2)] = {"id": sval(m.group(1)), "size": [num(m.group(4)), num(m.group(5))] if m.group(4) else None,
                               "kind": "misc" if "MISC" in m.group(3) else "mob"}

    # ---- sounds registry: CONST -> modid:name ----
    snd = {}
    for s in srcs.values():
        for m in re.finditer(r"RegistryObject<SoundEvent>\s+(\w+)\s*=\s*\w+\(\s*\"([\w./]+)\"", s): snd[m.group(1)] = f"{modid}:{m.group(2)}"

    # ---- client models: entity class -> (geo, anim, texture) ----
    models = {}
    for k, s in srcs.items():
        m = re.search(r"extends\s+(DefaultedEntityGeoModel|GeoModel|DefaultedGeoModel)<\s*(\w+)\s*>", s)
        if not m: continue
        ent = m.group(2)
        if m.group(1).startswith("Defaulted"):
            r = re.search(r"new\s+ResourceLocation\(\s*[\w.]+\s*,\s*\"([\w/]+)\"\s*\)", s)
            if r:
                n = r.group(1); models[ent] = {"geo": f"assets/{modid}/geo/entity/{n}.geo.json",
                                              "animation_file": f"assets/{modid}/animations/entity/{n}.animation.json",
                                              "texture": f"assets/{modid}/textures/entity/{n}.png", "model_class": k}
        else:
            g = re.search(r"\"(geo/[\w/.]+\.geo\.json)\"", s); an = re.search(r"\"(animations/[\w/.]+\.animation\.json)\"", s)
            t = re.search(r"\"(textures/[\w/.]+\.png)\"", s)
            models[ent] = {"geo": f"assets/{modid}/{g.group(1)}" if g else None,
                           "animation_file": f"assets/{modid}/{an.group(1)}" if an else None,
                           "texture": f"assets/{modid}/{t.group(1)}" if t else None, "model_class": k}

    assets = {str(p.relative_to(res)).replace("\\", "/") for p in res.rglob("*") if p.is_file()}
    entities = []
    for cls, r in sorted(reg.items(), key=lambda kv: kv[1]["id"] or kv[0]):
        s = srcs.get(cls, "")
        sup = re.search(rf"class\s+{cls}\b[^{{]*?extends\s+([\w.]+)", s)
        e = {"class": cls, "id": r["id"], "size": r["size"], "kind": r["kind"], "extends": sup.group(1) if sup else None,
             "geckolib": "GeoEntity" in s or "GeoAnimatable" in s}
        consts = {m.group(1): num(m.group(2)) for m in re.finditer(rf"\b(?:float|double|int)\s+(\w+)\s*=\s*({NUM})\s*;", s)}
        attrs, raw = {}, {}
        for m in re.finditer(r"\.add\(\s*Attributes\.(\w+)\s*,\s*([^)]+?)\s*\)", body_of(s, "createAttributes")):
            key, expr = ATTR.get(m.group(1), m.group(1).lower()), m.group(2)
            if re.fullmatch(NUM, expr): attrs[key] = num(expr)
            elif expr.split(".")[-1] in consts: attrs[key] = consts[expr.split(".")[-1]]
            else: raw[key] = expr    # config value etc.: kept so nothing is silently lost
        e["attributes"] = attrs
        if raw: e["attributes_unresolved"] = raw
        goals = []
        for sel, meth in (("goal", "goalSelectorPayload"), ("target", "targetSelectorPayload")):
            for i, g in enumerate(re.findall(r"new\s+([\w.]+)(?:<[^>]*>)?\s*\(", body_of(s, meth))):
                goals.append({"selector": sel, "priority": i, "goal": g, "custom": True})
        for m in re.finditer(r"(goalSelector|targetSelector)\.addGoal\(\s*(\d+)\s*,\s*new\s+([\w.]+)", s):
            goals.append({"selector": "goal" if m.group(1) == "goalSelector" else "target", "priority": int(m.group(2)), "goal": m.group(3), "custom": False})
        e["goals"] = goals
        e["sounds"] = {}
        for meth, kind in SOUNDS.items():
            m = re.search(r"ModSounds\.(\w+)", body_of(s, meth))
            if m and m.group(1) in snd: e["sounds"][kind] = snd[m.group(1)]
        mt = re.search(r"MobType\.(\w+)", body_of(s, "getMobType")); e["mob_type"] = mt.group(1).lower() if mt else None
        e["events"] = {v: [k] for k, v in EVENTS.items() if re.search(rf"\b(?:public|protected)\s+[\w<>]+\s+{k}\s*\(", s)}
        e["persistent"] = "removeWhenFarAway" in s and "return false" in body_of(s, "removeWhenFarAway")
        mdl = models.get(cls) or {}
        for k in ("geo", "animation_file", "texture"):   # trust paths only if the file exists
            e[k] = mdl.get(k) if mdl.get(k) in assets else None
        if not e["geo"] and r["id"] and f"assets/{modid}/geo/entity/{r['id']}.geo.json" in assets:     # renderer-less fallback by id
            e["geo"] = f"assets/{modid}/geo/entity/{r['id']}.geo.json"
        if not e["texture"] and r["id"] and f"assets/{modid}/textures/entity/{r['id']}.png" in assets:
            e["texture"] = f"assets/{modid}/textures/entity/{r['id']}.png"
        if not e["animation_file"] and r["id"] and f"assets/{modid}/animations/entity/{r['id']}.animation.json" in assets:
            e["animation_file"] = f"assets/{modid}/animations/entity/{r['id']}.animation.json"
        e["model_class"] = mdl.get("model_class")
        if e["texture"] and e["texture"].replace(".png", "_glowmask.png") in assets: e["glowmask"] = e["texture"].replace(".png", "_glowmask.png")
        e["controller_animations"] = sorted(set(re.findall(r"thenPlay\(\s*\"([\w.]+)\"", s)) | set(re.findall(r"thenLoop\(\s*\"([\w.]+)\"", s)))
        entities.append(e)

    a.out.mkdir(parents=True, exist_ok=True)
    zpath = a.out / f"{modid}-assets.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for n in sorted(assets): z.write(res / n, n)
    geo = {Path(n).name[:-9]: n for n in assets if n.endswith(".geo.json")}
    anim = {Path(n).name[:-15]: n for n in assets if n.endswith(".animation.json")}
    out = {"mod_id": modid, "jar": f"{a.repo.name} (source)", "entity_count": len(entities), "geo_files": geo, "animation_files": anim,
           "procedure_count": len(files), "java_lines": sum(s.count("\n") for s in srcs.values()), "entities": entities}
    (a.out / "entities.json").write_text(json.dumps(out, indent=2))
    mobs = [e for e in entities if e["kind"] == "mob"]
    print(f"{modid}: {len(entities)} entities ({len(mobs)} mobs), {sum(1 for e in entities if e['geo'])} with geo, "
          f"{sum(1 for e in entities if e['texture'])} with texture, {sum(1 for e in mobs if e['attributes'])} mobs with stats "
          f"-> {a.out/'entities.json'}, {zpath}")

if __name__ == "__main__":
    sys.exit(main())
