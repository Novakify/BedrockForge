#!/usr/bin/env python3
"""Bedrock Forge ModPorter - stage 1: extract entity data from a Forge (MCreator/GeckoLib) mod jar.

Uses `javap -c -p` (JDK) to read bytecode WITHOUT running it. Produces entities.json:
  per entity: registry id, superclass, size, attributes, AI goals (with priorities and args),
  sounds, which MCreator procedures run on which events, model kind (GeckoLib / Java), texture.

  python extract.py mod.jar -o out/
Notes: Forge 1.20.1 production jars use SRG names (f_22279_ ...). The SRG tables below cover the
members we read; anything unknown is kept raw so nothing is silently lost.
"""
from __future__ import annotations
import argparse, json, re, subprocess, sys, tempfile, zipfile
from collections import defaultdict
from pathlib import Path

# --- SRG -> Mojang names for Forge 1.20.1 (only what we need) -------------------------------------
ATTR = {"f_22276_": "max_health", "f_22277_": "follow_range", "f_22278_": "knockback_resistance",
        "f_22279_": "movement_speed", "f_22280_": "flying_speed", "f_22281_": "attack_damage",
        "f_22282_": "attack_knockback", "f_22283_": "attack_speed", "f_22284_": "armor",
        "f_22285_": "armor_toughness", "f_22286_": "luck"}
# Entity methods MCreator overrides -> event names we care about
EVENTS = {"m_8119_": "tick", "m_6469_": "hurt", "m_6667_": "die", "m_6096_": "interact", "m_7023_": "travel",
          "m_6071_": "interact_at", "m_7334_": "push", "m_6075_": "base_tick", "m_8024_": "custom_server_ai_step",
          "m_7327_": "do_hurt_target", "m_6593_": "spawn", "m_6518_": "finalize_spawn", "m_7380_": "add_save_data",
          "m_6123_": "player_touch", "m_6785_": "remove_when_far"}
SOUND_METHODS = {"m_7515_": "ambient", "m_7975_": "hurt", "m_5592_": "death", "m_7355_": "step"}
MOBTYPE = {"f_21640_": "undefined", "f_21641_": "undead", "f_21642_": "arthropod", "f_21643_": "illager", "f_21644_": "water"}

def javap(classdir: Path, cls: str) -> str:
    return subprocess.run(["javap", "-c", "-p", "-cp", str(classdir), cls], capture_output=True, text=True).stdout

def methods(dis: str) -> dict[str, list[str]]:
    """Split javap output into {method_srg_name: [instruction lines]}."""
    out, cur = {}, None
    for line in dis.splitlines():
        m = re.match(r"^  \S.*?\b([\w$<>]+)\((.*)\);$", line)
        if m:
            cur = m.group(1); out.setdefault(cur, [])
        elif cur and re.match(r"^\s+\d+: ", line):
            out[cur].append(line.strip())
    return out

NUM = re.compile(r"// (?:double|float|int|long) (-?[\d.Ee+-]+)[dfL]?")
def consts(line: str):
    if (m := NUM.search(line)): return float(m.group(1))
    if (m := re.search(r"\b(?:iconst|fconst|dconst)_(m?\d)", line)): return float(m.group(1).replace("m", "-"))
    if (m := re.search(r"\b(?:bipush|sipush)\s+(-?\d+)", line)): return float(m.group(1))
    return None

def parse_attributes(ins: list[str]) -> dict:
    """Follows constants directly or through local variables (dstore_N / dload_N)."""
    attrs, pending, locals_, last = {}, None, {}, None
    for l in ins:
        if (m := re.search(r"\b[df]store(?:_|\s+)(\d+)", l)) and last is not None: locals_[m.group(1)] = last; last = None; continue
        if (m := re.search(r"Attributes\.(f_\d+_)", l)): pending = ATTR.get(m.group(1), m.group(1)); continue
        if (v := consts(l)) is not None:
            if pending: attrs[pending] = v; pending = None
            else: last = v
            continue
        if pending and (m := re.search(r"\b[df]load(?:_|\s+)(\d+)", l)) and m.group(1) in locals_:
            attrs[pending] = locals_[m.group(1)]; pending = None
    return attrs

def parse_goals(ins: list[str]) -> list[dict]:
    """registerGoals: `this.goalSelector/targetSelector.addGoal(priority, new XGoal(args...))`."""
    goals, prio, sel, args, name = [], None, None, [], None
    for l in ins:
        if "f_21345_" in l: sel = "goal"          # goalSelector
        elif "f_21346_" in l: sel = "target"      # targetSelector
        if sel and prio is None and (v := consts(l)) is not None and name is None: prio = int(v); continue
        if (m := re.search(r"new\s+#\d+\s+// class ([\w/$]+)", l)):
            cls = m.group(1)
            if name is None and ("Goal" in cls or "/goal/" in cls or "/target/" in cls): name = cls; args = []
            elif name: args.append(cls.split("/")[-1])
            continue
        if name and (v := consts(l)) is not None: args.append(v); continue
        if name and (m := re.search(r"ldc_w?\s+#\d+\s+// class ([\w/$]+)", l)): args.append(m.group(1).split("/")[-1]); continue
        if "GoalSelector.m_25352_" in l and name:   # addGoal
            goals.append({"selector": sel, "priority": prio, "goal": name.split("/")[-1], "args": args, "custom": "net/mcreator" in name})
            prio, sel, name, args = None, None, None, []
    return goals

def procedures(ins: list[str]) -> list[str]:
    return sorted({m.group(1).split("/")[-1] for l in ins if (m := re.search(r"invokestatic.*// Method ([\w/$]+Procedure)\.", l))})

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("jar", type=Path); ap.add_argument("-o", "--out", type=Path, default=Path("."))
    a = ap.parse_args(argv)
    tmp = Path(tempfile.mkdtemp()); zipfile.ZipFile(a.jar).extractall(tmp)
    names = [str(p.relative_to(tmp)) for p in tmp.rglob("*") if p.is_file()]
    modid = next((p.split("/")[1] for p in names if p.startswith("assets/") and p.count("/") > 2 and p.split("/")[1] != "minecraft"), "?")

    # registry: string id + size from the ModEntities init class
    reg_cls = next((n for n in names if re.search(r"/(init|registry|registries)/\w*(ModEntities|ModEntityTypes|EntityTypes|Entities)\.class$", n)), None)
    registry, sizes = {}, {}
    if reg_cls:
        ins = sum(methods(javap(tmp, reg_cls[:-6].replace("/", "."))).values(), [])
        last_id, fl, ids = None, [], []
        for l in ins:
            if (m := re.search(r"// String ([a-z0-9_]+)$", l)) and (m.group(1) != modid or "register" in l or True):
                if m.group(1) not in ids or m.group(1) != modid: last_id, fl = m.group(1), []
                ids.append(m.group(1))
            elif (v := consts(l)) is not None: fl.append(v)
            elif "m_20699_" in l and last_id and len(fl) >= 2: sizes[last_id] = fl[-2:]
        def id_for(cls):
            base = cls[:-6] if cls.endswith("Entity") else cls
            cands = {base.lower(), re.sub(r"(?<=[A-Za-z])(?=\d)", "_", re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", base)).lower(),
                     re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", base).lower(),
                     re.sub(r"(\d+)", r"_\1_", base.lower()).strip("_").replace("__", "_")}
            for c in cands:
                if c in ids: return c
            return None
    geo = {Path(n).name.replace(".geo.json", ""): n for n in names if n.endswith(".geo.json")}
    anim = {Path(n).name.replace(".animation.json", ""): n for n in names if n.endswith(".animation.json")}

    entity_pkg = None
    def pkg_of(p): return p.rsplit(".", 1)[0]
    entities = []
    for n in sorted(names):
        if not re.search(r"/entit(y|ies)/[A-Za-z0-9]+Entity\.class$", n): continue
        cls = n[:-6].replace("/", "."); entity_pkg = cls.rsplit(".", 1)[0]; dis = javap(tmp, cls); ms = methods(dis)
        sup = re.search(r"extends ([\w.$]+)", dis); impl = re.search(r"implements ([\w.$, ]+)", dis)
        e = {"class": cls.split(".")[-1], "extends": sup.group(1).split(".")[-1] if sup else None,
             "geckolib": bool(impl and "GeoEntity" in impl.group(1))}
        e["id"] = id_for(e["class"]) if reg_cls else None
        e["size"] = sizes.get(e["id"])
        attr_m = next((k for k in ms if k in ("createAttributes", "getAttributeBuilder", "createMobAttributes", "registerAttributes")), "createAttributes")
        e["attributes"] = parse_attributes(ms.get(attr_m, []))
        e["goals"] = parse_goals(ms.get("m_8099_", []))
        e["sounds"] = {}
        for srg, kind in SOUND_METHODS.items():
            s = [m.group(1) for l in ms.get(srg, []) if (m := re.search(r"// String ([\w.:/]+)$", l))]
            if s: e["sounds"][kind] = s[0]
        e["mob_type"] = next((MOBTYPE.get(m.group(1), m.group(1)) for l in ms.get("m_6336_", []) if (m := re.search(r"MobType\.(f_\d+_)", l))), None)
        e["events"] = {EVENTS.get(k, k): p for k, v in ms.items() if (p := procedures(v))}
        e["persistent"] = any("iconst_0" in l for l in ms.get("m_6785_", []))  # removeWhenFarAway -> false
        entities.append(e)

    tex_names = {Path(n).stem: n for n in names if n.startswith(f"assets/{modid}/textures/entities/") and n.endswith(".png")}
    for e in entities:
        if not e["geckolib"]: continue
        base = e['class'][:-6]
        cand = [n for n in names if n.endswith(f"/{base}Model.class")]
        mcls = cand[0][:-6].replace("/", ".") if cand else f"{pkg_of(entity_pkg)}.entity.model.{base}Model"
        mdis = javap(tmp, mcls)
        g = re.search(r"// String (geo/[\w/.-]+\.geo\.json)", mdis); an = re.search(r"// String (animations/[\w/.-]+\.animation\.json)", mdis)
        e["geo"] = f"assets/{modid}/{g.group(1)}" if g else None
        e["animation_file"] = f"assets/{modid}/{an.group(1)}" if an else None
        ms = methods(javap(tmp, f"{entity_pkg}.{e['class']}"))
        strs = [m.group(1) for l in ms.get("m_8097_", []) if (m := re.search(r"// String ([\w-]+)$", l))]
        e["texture"] = next((tex_names[x] for x in strs if x in tex_names), None)
        if not e["texture"]:   # hand-written mods: texture path is a literal in the model or renderer
            for cn in [n for n in names if re.search(rf"/{base}(Model|Renderer)\.class$", n)]:
                t = re.search(r"// String (textures/[\w/.-]+?)(\.png)?$", javap(tmp, cn[:-6].replace("/", ".")), re.M)
                if t:
                    path = f"assets/{modid}/{t.group(1)}" + ("" if t.group(1).endswith(".png") else ".png")
                    if path in names: e["texture"] = path; break
        anims = set()
        for k, v in ms.items():
            if "Predicate" in k or k == "registerControllers":
                anims |= {m.group(1) for l in v if (m := re.search(r"// String (animation\.[\w.]+)", l))}
        anims |= {m.group(1) for v in ms.values() for l in v if (m := re.search(r"// String (animation\.[\w.]+)", l))}  # RawAnimation fields
        e["controller_animations"] = sorted(anims)

    # renderer -> texture + model per entity (non-GeckoLib / fallback)
    for n in names:
        if not re.search(r"/client/renderer/\w+Renderer\.class$", n): continue
        dis = javap(tmp, n[:-6].replace("/", "."))
        ent = re.search(r"([A-Za-z0-9]+Entity)\b", dis); tex = re.findall(r"// String ([\w/.:-]+\.png)", dis)
        model = re.search(r"// class [\w/$]+/model/(Model\w+|\w+Model)\b", dis)
        for e in entities:
            if ent and e["class"] == ent.group(1) and not e.get("texture"):
                t = tex[0] if tex else None
                if t and ":" in t: t = f"assets/{t.split(':')[0]}/{t.split(':')[1]}"
                e["texture"] = t
                e["model_class"] = model.group(1) if model else None

    # single-entity mods: if paths are built at runtime (string concat), use the only geo/texture in the jar
    ent_tex = [n for n in names if n.startswith(f"assets/{modid}/textures/entit") and n.endswith(".png")]
    if len(entities) == 1:
        e = entities[0]
        if not e.get("geo") and len(geo) == 1: e["geo"] = next(iter(geo.values()))
        if not e.get("texture") and ent_tex: e["texture"] = max(ent_tex, key=lambda n: zipfile.ZipFile(a.jar).getinfo(n).file_size)
        if not e.get("id") and modid in (registry.values() or [modid]): e["id"] = modid
    out = {"mod_id": modid, "jar": a.jar.name, "entity_count": len(entities), "geo_files": geo, "animation_files": anim,
           "procedure_count": sum(1 for n in names if n.endswith("Procedure.class")), "entities": entities}
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "entities.json").write_text(json.dumps(out, indent=2))
    print(f"{modid}: {len(entities)} entities, {len(geo)} geo, {len(anim)} anim, {out['procedure_count']} procedures -> {a.out/'entities.json'}")

if __name__ == "__main__":
    sys.exit(main())
