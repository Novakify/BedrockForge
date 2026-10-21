#!/usr/bin/env python3
"""Bedrock Forge ModPorter - stage 1 (SOURCE mode): a mod's Java source tree -> entities.json.

  python extract_src.py <repo root> -o out/ [--modid sculkhorde]

For open-source mods with no jar handy (or no way to build one). Produces the same entities.json as
extract.py, plus `<out>/assets.zip` laid out like a jar (assets/<modid>/...) so generate.py can run unchanged:

  python generate.py out/assets.zip out/entities.json -o build/

What it reads (plain-text Java, Mojang/official names):
  - entity registrations:  registerXxx("id", Cls::new, w, h, ...)  and  ENTITY_TYPES.register("id", ... Cls::new ... sized(w, h))
  - attributes:            .add(Attributes.MAX_HEALTH, CONST_OR_LITERAL)   (constants resolved through the class + parents)
  - movement kind:         FlyingMob / FlyingPathNavigation / NoGravity -> fly, WaterAnimal / WaterBound -> swim, else walk
  - GeckoLib model paths:  DefaultedEntityGeoModel("name") -> geo|animations|textures/entity/name, or explicit literals
Never runs the mod.
"""
from __future__ import annotations
import argparse, json, re, zipfile
from pathlib import Path

ATTR = {"MAX_HEALTH": "max_health", "FOLLOW_RANGE": "follow_range", "KNOCKBACK_RESISTANCE": "knockback_resistance",
        "MOVEMENT_SPEED": "movement_speed", "FLYING_SPEED": "flying_speed", "ATTACK_DAMAGE": "attack_damage",
        "ATTACK_KNOCKBACK": "attack_knockback", "ATTACK_SPEED": "attack_speed", "ARMOR": "armor", "ARMOR_TOUGHNESS": "armor_toughness"}
# vanilla parents that bring their own attributes when the mod class doesn't override createAttributes
VANILLA = {"Ravager": {"max_health": 100, "movement_speed": 0.3, "attack_damage": 12, "knockback_resistance": 0.75, "follow_range": 32},
           "Creeper": {"max_health": 20, "movement_speed": 0.25, "follow_range": 16},
           "Salmon": {"max_health": 3, "movement_speed": 0.7}, "WaterAnimal": {"max_health": 10, "movement_speed": 0.7},
           "FlyingMob": {"max_health": 10, "movement_speed": 0.7}}
NUM = r"-?\d+(?:\.\d+)?"

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("root", type=Path); ap.add_argument("-o", "--out", type=Path, default=Path("out"))
    ap.add_argument("--modid"); a = ap.parse_args(argv)
    java = {p.stem: p.read_text("utf-8", "replace") for p in a.root.rglob("src/main/java/**/*.java")}
    res = next(a.root.rglob("src/main/resources"))
    modid = a.modid or next(p.name for p in (res / "assets").iterdir() if p.is_dir() and p.name != "minecraft")

    def consts(cls, seen=()):
        src = java.get(cls, ""); out = {}
        m = re.search(rf"class\s+{cls}\b[^{{]*?extends\s+(\w+)", src)
        if m and m.group(1) in java and m.group(1) not in seen: out.update(consts(m.group(1), seen + (cls,)))
        for n, v in re.findall(rf"(?:static\s+)?final\s+(?:float|double|int)\s+(\w+)\s*=\s*({NUM})[fFdD]?\s*;", src): out[n] = float(v)
        return out

    def chain(cls):
        out = []
        while cls and cls not in out:
            out.append(cls); m = re.search(rf"class\s+{cls}\b[^{{]*?extends\s+(\w+)", java.get(cls, ""))
            cls = m.group(1) if m else None
        return out

    def attributes(cls):
        ch = chain(cls); at = {}
        for c in reversed(ch):
            if c in VANILLA: at.update(VANILLA[c])
            src = java.get(c, ""); k = consts(c)
            for name, expr in re.findall(r"\.add\(\s*Attributes\.(\w+)\s*,\s*([\w.]+)\s*\)", src):
                if name not in ATTR: continue
                if re.fullmatch(rf"{NUM}[fFdD]?", expr): at[ATTR[name]] = float(re.sub(r"[fFdD]$", "", expr))
                elif expr.split(".")[-1] in k: at[ATTR[name]] = k[expr.split(".")[-1]]
        return at

    def movement(cls):
        src = " ".join(java.get(c, "") for c in chain(cls)); ch = chain(cls)
        if any(c in ("WaterAnimal", "Salmon", "AbstractFish") for c in ch) or "WaterBoundPathNavigation" in src: return "swim"
        if "FlyingMob" in ch or re.search(r"FlyingPathNavigation|FlyingMoveControl|setNoGravity\(true\)", src): return "fly"
        return "walk"

    def model_paths(cls):
        for name, src in java.items():
            if not name.endswith("Model") or not re.search(rf"<\s*{cls}\s*>", src): continue
            d = re.search(r'super\(\s*new ResourceLocation\([^,]+,\s*"([^"]+)"\)', src)
            if d and "DefaultedEntityGeoModel" in src:
                n = d.group(1)
                return name, f"assets/{modid}/geo/entity/{n}.geo.json", f"assets/{modid}/animations/entity/{n}.animation.json", f"assets/{modid}/textures/entity/{n}.png"
            lits = re.findall(r'"([^"]+\.(?:geo\.json|animation\.json|png))"', src)
            pick = lambda suf: next((f"assets/{modid}/{l}" for l in lits if l.endswith(suf)), None)
            return name, pick(".geo.json"), pick(".animation.json"), pick(".png")
        return None, None, None, None

    ents = []
    reg = "\n".join(s for n, s in java.items() if "DeferredRegister" in s and "ENTITY_TYPES" in s)
    for m in re.finditer(r'register\w*\(\s*"([a-z0-9_]+)"\s*,\s*(?:\(\)\s*->\s*EntityType\.Builder\.(?:<\w+>)?of\()?(\w+)::new([^;]*)', reg):
        eid, cls, rest = m.groups()
        size = re.search(rf"sized\(\s*({NUM})[fF]?\s*,\s*({NUM})[fF]?", rest) or re.match(rf"\s*,\s*({NUM})[fF]?\s*,\s*({NUM})[fF]?", rest)
        is_mob = bool(re.search(r"\bregisterMob\(", m.group(0))) or any(c in ("Mob", "Monster", "PathfinderMob", "FlyingMob", "WaterAnimal") for c in chain(cls))
        mc, geo, anim, tex = model_paths(cls)
        anims = []
        if anim and (res / anim).exists():
            try: anims = list(json.loads((res / anim).read_text())["animations"])
            except Exception: pass
        ents.append({"class": cls, "extends": (chain(cls) + ["?"])[1], "geckolib": bool(geo), "id": eid,
                     "size": [float(size.group(1)), float(size.group(2))] if size else [0.6, 1.8],
                     "attributes": attributes(cls), "goals": [], "sounds": {}, "mob_type": None, "events": {},
                     "persistent": False, "movement": movement(cls), "is_mob": is_mob, "model_class": mc,
                     "geo": geo, "animation_file": anim, "texture": tex, "controller_animations": anims,
                     "glowmask": tex[:-4] + "_glowmask.png" if tex and (res / (tex[:-4] + "_glowmask.png")).exists() else None})

    # sounds / goals / lifecycle events / mob type come from the detail scanner (src_details.py)
    try:
        import tempfile, importlib.util
        spec = importlib.util.spec_from_file_location("src_details", Path(__file__).with_name("src_details.py"))
        src_details = importlib.util.module_from_spec(spec); spec.loader.exec_module(src_details)
        with tempfile.TemporaryDirectory() as td:
            src_details.main([str(a.root), "-o", td])
            det = {e["id"]: e for e in json.loads((Path(td) / "entities.json").read_text())["entities"]}
        for e in ents:
            d = det.get(e["id"], {})
            for f in ("sounds", "goals", "events", "mob_type"):
                if d.get(f): e[f] = d[f]
    except Exception as ex:
        print(f"(detail scan skipped: {ex})")

    a.out.mkdir(parents=True, exist_ok=True)
    data = {"mod_id": modid, "jar": f"{modid} (source)", "entity_count": len(ents), "geo_files": {}, "animation_files": {},
            "procedure_count": None, "entities": ents}
    (a.out / "entities.json").write_text(json.dumps(data, indent=1))
    with zipfile.ZipFile(a.out / "assets.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for p in res.rglob("*"):
            if p.is_file() and (p.relative_to(res).parts[0] == "assets" or p.name in ("logo.png", "pack.png")):
                z.write(p, p.relative_to(res).as_posix())
    mobs = [e for e in ents if e["is_mob"]]
    print(f"{modid}: {len(ents)} entity types ({len(mobs)} mobs), "
          f"{sum(1 for e in mobs if e['geo'])} with GeckoLib models, {sum(1 for e in mobs if e['attributes'])} with attributes -> {a.out}")

if __name__ == "__main__":
    main()
