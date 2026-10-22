#!/usr/bin/env python3
"""Bedrock Forge ModPorter - stage 2: entities.json + the mod jar -> Bedrock behavior + resource pack.

  python generate.py mod.jar out/entities.json -o build/ [--ns from_the_caves] [--private]

What it generates (stage A, "shells"):
  RP: GeckoLib geometry + animations (renamed per file so they don't collide), textures, client entity
      files with an idle/walk/attack animation controller, spawn eggs, sounds, names.
  BP: one entity per mod entity with its real stats (health, speed, damage, follow range, size) and a
      generic hostile AI so it can be tested. The mod's real behaviour lives in MCreator procedures and
      is ported separately (Script API); this is flagged in PORT_REPORT.md.
Assets are copied from the jar; respect the mod's licence (--private stamps the packs "private test").
"""
from __future__ import annotations
import argparse, json, re, shutil, uuid, zipfile
from pathlib import Path


_SMOOTH_EASINGS = {"easeInOutSine", "easeInSine", "easeOutSine", "easeInOutQuad", "easeInQuad", "easeOutQuad",
                   "easeInOutCubic", "catmullrom"}


def bedrock_anim(anim: dict) -> dict:
    """GeckoLib keyframes ({"vector": [...], "easing": ...}) -> Bedrock keyframes; drop GeckoLib timeline
    instructions (e.g. "DoDamageInstruction;") that Bedrock Molang rejects. Found in game content logs (E-022)."""
    for bone in (anim.get("bones") or {}).values():
        for ch in ("rotation", "position", "scale"):
            v = bone.get(ch)
            if not isinstance(v, dict):
                continue
            for t, val in list(v.items()):
                if isinstance(val, dict) and "vector" in val:
                    smooth = val.get("easing", "linear") in _SMOOTH_EASINGS
                    v[t] = {"post": val["vector"], "lerp_mode": "catmullrom"} if smooth else val["vector"]
    tl = anim.get("timeline")
    if isinstance(tl, dict):
        for t, val in list(tl.items()):
            vals = val if isinstance(val, list) else [val]
            keep = [x for x in vals if not re.fullmatch(r"\s*[A-Za-z]+Instruction;?\s*", str(x))]
            if keep:
                tl[t] = keep if isinstance(val, list) else keep[0]
            else:
                del tl[t]
        if not tl:
            del anim["timeline"]
    return anim

def jload(b: bytes):
    # MCreator/GeckoLib JSON sometimes has comments or trailing commas
    t = re.sub(r"//[^\n]*", "", b.decode("utf-8", "replace"))
    t = re.sub(r",(\s*[}\]])", r"\1", t)
    return json.loads(t)

def manifest(name, desc, kind, deps, version=(1, 0, 0)):
    return {"format_version": 2,
            "header": {"name": name, "description": desc, "uuid": str(uuid.uuid4()), "version": list(version), "min_engine_version": [1, 21, 0]},
            "modules": [{"type": kind, "uuid": str(uuid.uuid4()), "version": list(version)}],
            "dependencies": deps}

def pick_anims(anims):
    """Map a mod's animation names onto idle / walk / attack / living (always-on loop)."""
    def find(*keys, avoid=()):
        for k in keys:
            for a in anims:
                if k in a.split(".") or a.endswith(k):
                    if not any(x in a for x in avoid): return a
        for k in keys:
            for a in anims:
                if k in a and not any(x in a for x in avoid): return a
        return None
    return {r: v for r, v in {"idle": find("idle", avoid=("aggro", "twitch")), "walk": find("walk", "swim", "fly", "move", "run"),
            "attack": find("attack", "melee", "attack1", avoid=("cancel", "charge")), "living": find("living")}.items() if v}

def anim_controller(ns, key, anims):
    has = {a.split(".")[-1] for a in anims}
    states = {"default": {"animations": ["idle"] if "idle" in has else [], "transitions": []}}
    if "walk" in has:
        states["walking"] = {"animations": ["walk"], "transitions": [{"default": "q.modified_move_speed < 0.05"}], "blend_transition": 0.2}
        states["default"]["transitions"].append({"walking": "q.modified_move_speed > 0.05"})
        states["default"]["blend_transition"] = 0.2
    if "attack" in has:
        states["attacking"] = {"animations": ["attack"], "transitions": [{"default": "q.all_animations_finished"}]}
        for st in states.values():
            if st is not states.get("attacking"): st["transitions"].insert(0, {"attacking": "v.attack_time > 0"})
    return {f"controller.animation.{ns}.{key}.move": {"initial_state": "default", "states": states}}

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("jar", type=Path); ap.add_argument("entities", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=Path("build")); ap.add_argument("--ns"); ap.add_argument("--private", action="store_true")
    ap.add_argument("--overlay", type=Path, help="behaviour overlay dir (overlay.json + scripts/), see overlays/")
    a = ap.parse_args(argv)
    data = json.loads(a.entities.read_text()); z = zipfile.ZipFile(a.jar); names = set(z.namelist())
    modid = data["mod_id"]; ns = a.ns or modid
    title = modid.replace("_", " ").title()
    _ov = (a.overlay or (Path(__file__).parent / "overlays" / modid)) / "overlay.json"
    if _ov.exists(): title = json.loads(_ov.read_text()).get("pack_name", title)
    rp, bp = a.out / f"{title} RP", a.out / f"{title} BP"
    shutil.rmtree(a.out, ignore_errors=True)
    for d in (rp, bp): d.mkdir(parents=True)
    tag = " (PRIVATE TEST PORT - original mod is not ours)" if a.private else ""
    rpm = manifest(f"{title} RP", f"Bedrock Forge ModPorter port of {data['jar']}{tag}", "resources", [])
    bpm = manifest(f"{title} BP", f"Bedrock Forge ModPorter port of {data['jar']}{tag}", "data",
                   [{"uuid": rpm["header"]["uuid"], "version": rpm["header"]["version"]}])
    rpm["dependencies"] = [{"uuid": bpm["header"]["uuid"], "version": bpm["header"]["version"]}]

    report = {"generated": [], "fallbacks": [], "warnings": []}
    jlang = {}
    lp = f"assets/{modid}/lang/en_us.json"
    if lp in names:
        try: jlang = jload(z.read(lp))
        except Exception: pass
    geo_ids, anim_maps, lang = {}, {}, []

    # ---- geometry + animations, renamed per source file ----
    for src in sorted(n for n in names if n.endswith(".geo.json")):
        stem = Path(src).name[:-9]; g = jload(z.read(src))
        geos = g.get("minecraft:geometry") or []
        for i, geo in enumerate(geos):
            gid = f"geometry.{ns}.{stem}" + (f"_{i}" if i else "")
            geo["description"]["identifier"] = gid
        g["format_version"] = g.get("format_version", "1.12.0")
        (rp / "models/entity").mkdir(parents=True, exist_ok=True)
        (rp / "models/entity" / f"{stem}.geo.json").write_text(json.dumps(g, indent=1))
        geo_ids[src] = f"geometry.{ns}.{stem}"
    for src in sorted(n for n in names if n.endswith(".animation.json")):
        stem = Path(src).name[:-15]; an = jload(z.read(src)); mp = {}
        new = {}
        keys = list((an.get("animations") or {}))
        lasts = [k.split(".")[-1].lower() for k in keys]
        for k, v in (an.get("animations") or {}).items():
            short = k.split(".")[-1]
            if lasts.count(short.lower()) > 1:   # collision -> keep more of the name
                short = re.sub(r"^animation\.(model\.|%s\.)?" % re.escape(stem), "", k).replace(".", "_").strip("_") or short
            nk = f"animation.{ns}.{stem}.{short.lower()}"; new[nk] = v; mp[k] = (short, nk)
        an["animations"] = {k: bedrock_anim(v) for k, v in new.items()}
        (rp / "animations").mkdir(parents=True, exist_ok=True)
        (rp / "animations" / f"{stem}.animation.json").write_text(json.dumps(an, indent=1))
        anim_maps[src] = mp

    # ---- per entity ----
    sound_defs = {}; glow_used = False
    fallback_geo = next((v for k, v in geo_ids.items() if "phase1" in k), next(iter(geo_ids.values()), None))
    skipped = [e["id"] for e in data["entities"] if e.get("is_mob") is False]
    if skipped: report["warnings"].append(f"{len(skipped)} non-mob entity types skipped (projectiles/effects/cursors): {', '.join(skipped)}")
    for e in [e for e in data["entities"] if e.get("is_mob") is not False]:
        eid = e["id"] or (modid if e["class"].lower().startswith(modid.replace("_", "")) else e["class"][:-6].lower())
        full = f"{ns}:{eid}"
        tex = e.get("texture")
        tex_key = None
        if tex and tex in names:
            tex_key = f"textures/entity/{ns}/{Path(tex).stem}"
            (rp / Path(tex_key).parent).mkdir(parents=True, exist_ok=True)
            (rp / f"{tex_key}.png").write_bytes(z.read(tex))
        else:
            report["warnings"].append(f"{full}: texture {tex} not found")
        gid = geo_ids.get(e.get("geo")) if e.get("geo") else None
        if not gid and e.get("model_class") in ("HumanoidModel", "PlayerModel"):
            gid = "geometry.humanoid.custom"   # vanilla player-shaped body: Bedrock has it built in (64x64 skin layout)
            report["fallbacks"].append(f"{full}: {e.get('model_class')} -> built-in geometry.humanoid.custom")
        if not gid:
            gid = fallback_geo; report["fallbacks"].append(f"{full}: Java-code model ({e.get('model_class')}) not converted yet, using {gid}")
        amap = anim_maps.get(e.get("animation_file") or "", {})
        anims = {short.lower(): nk for _, (short, nk) in amap.items()}  # mods mix "Idle"/"WALK"/"walk"
        ckey = Path(e.get("animation_file") or eid).name.split(".")[0]
        client = {"format_version": "1.10.0", "minecraft:client_entity": {"description": {
            "identifier": full,
            "materials": {"default": "entity_alphatest"},
            "textures": {"default": tex_key or "textures/misc/missing_texture"},
            "geometry": {"default": gid},
            "render_controllers": ["controller.render.default"],
            "spawn_egg": {"base_color": "#1b1b1b", "overlay_color": "#8b0000"}}}}
        desc = client["minecraft:client_entity"]["description"]
        if e.get("glowmask") and e["glowmask"] in names:
            gk = f"textures/entity/{ns}/{Path(e['glowmask']).stem}"
            (rp / f"{gk}.png").write_bytes(z.read(e["glowmask"]))
            desc["materials"]["glow"] = "entity_emissive_alpha"; desc["textures"]["glow"] = gk
            desc["render_controllers"].append(f"controller.render.{ns}.glow")
            glow_used = True
        roles = pick_anims(list(anims))
        for r, v in roles.items():   # fill missing idle/walk/attack roles from the mod's own names (move.run, head.attack, ...)
            if r != "living" and r not in anims: anims[r] = anims[v]
        if anims:
            desc["animations"] = dict(anims)
            ctrl = anim_controller(ns, ckey, [r for r in ("idle", "walk", "attack") if r in anims])
            cname = next(iter(ctrl))
            (rp / "animation_controllers").mkdir(parents=True, exist_ok=True)
            (rp / "animation_controllers" / f"{ckey}.ac.json").write_text(json.dumps({"format_version": "1.10.0", "animation_controllers": ctrl}, indent=1))
            desc["animations"]["move"] = cname
            desc["scripts"] = {"animate": ["move"] + (["living"] if "living" in roles else [])}
        (rp / "entity").mkdir(parents=True, exist_ok=True)
        (rp / "entity" / f"{eid}.entity.json").write_text(json.dumps(client, indent=1))

        # behavior: real stats + generic hostile AI (placeholder for the procedure-driven behaviour)
        at = e.get("attributes", {})
        w, h = (e.get("size") or [0.6, 1.8])
        comps = {
            "minecraft:type_family": {"family": ["monster", "mob", ns]},
            "minecraft:health": {"value": at.get("max_health", 20), "max": at.get("max_health", 20)},
            "minecraft:movement": {"value": round(at.get("movement_speed", 0.25), 3)},
            "minecraft:attack": {"damage": at.get("attack_damage", 3)},
            "minecraft:follow_range": {"value": at.get("follow_range", 16), "max": at.get("follow_range", 16)},
            "minecraft:knockback_resistance": {"value": min(1.0, at.get("knockback_resistance", 0))},
            "minecraft:collision_box": {"width": w, "height": h},
            "minecraft:physics": {}, "minecraft:pushable": {"is_pushable": True, "is_pushable_by_piston": True},
            "minecraft:jump.static": {}, "minecraft:movement.basic": {},
            "minecraft:navigation.walk": {"can_path_over_water": True, "avoid_damage_blocks": True, "can_open_doors": True},
            "minecraft:behavior.float": {"priority": 0},
            "minecraft:behavior.melee_attack": {"priority": 2, "speed_multiplier": 1.2, "track_target": True},
            "minecraft:behavior.nearest_attackable_target": {"priority": 2, "must_see": False, "reselect_targets": True,
                "entity_types": [{"filters": {"test": "is_family", "subject": "other", "value": "player"}, "max_dist": at.get("follow_range", 16)}]},
            "minecraft:behavior.hurt_by_target": {"priority": 1},
            "minecraft:behavior.random_stroll": {"priority": 6, "speed_multiplier": 0.8},
            "minecraft:behavior.look_at_player": {"priority": 7, "look_distance": 8},
            "minecraft:behavior.random_look_around": {"priority": 8},
            "minecraft:despawn": {"despawn_from_distance": {}},
        }
        if e.get("mob_type") == "undead": comps["minecraft:type_family"]["family"].append("undead")
        mv = e.get("movement", "walk")
        if mv == "fly":
            for k in ("minecraft:navigation.walk", "minecraft:movement.basic", "minecraft:jump.static", "minecraft:behavior.random_stroll"): comps.pop(k)
            comps.update({"minecraft:can_fly": {}, "minecraft:flying_speed": {"value": round(min(at.get("flying_speed", 0.6), 1.0) * 0.25, 3)},
                "minecraft:physics": {"has_gravity": False}, "minecraft:movement.hover": {},
                "minecraft:navigation.hover": {"can_path_over_water": True, "can_sink": False, "can_pass_doors": True, "can_path_from_air": True, "avoid_damage_blocks": True},
                "minecraft:behavior.random_hover": {"priority": 6, "xz_dist": 8, "y_dist": 4, "y_offset": -1, "interval": 1, "hover_height": [1, 4]}})
        elif mv == "swim":
            for k in ("minecraft:navigation.walk", "minecraft:movement.basic", "minecraft:jump.static", "minecraft:behavior.random_stroll", "minecraft:behavior.float"): comps.pop(k)
            comps.update({"minecraft:breathable": {"breathes_water": True, "breathes_air": False, "suffocate_time": 0},
                "minecraft:underwater_movement": {"value": round(min(at.get("movement_speed", 0.2), 0.5) * 0.3, 3) or 0.06},
                "minecraft:navigation.generic": {"is_amphibious": False, "can_path_over_water": False, "can_swim": True, "can_walk": False, "can_breach": False},
                "minecraft:movement.sway": {"sway_amplitude": 0}, "minecraft:behavior.swim_wander": {"priority": 6, "speed_multiplier": 1.0, "look_ahead": 2, "wander_time": 5}})
        if "movement_speed" in at and at["movement_speed"] == 0:   # stationary (pods, spewers, corpses, nodes)
            for k in [k for k in comps if k.startswith(("minecraft:navigation", "minecraft:movement", "minecraft:behavior.random", "minecraft:behavior.melee", "minecraft:jump"))]: comps.pop(k)
            comps["minecraft:knockback_resistance"] = {"value": 1.0}
            comps["minecraft:pushable"] = {"is_pushable": False, "is_pushable_by_piston": False}
        if "attack_damage" in at and not at["attack_damage"]:   # explicitly harmless (nodes, pods), not just unknown
            for k in ("minecraft:behavior.melee_attack", "minecraft:behavior.nearest_attackable_target", "minecraft:attack"): comps.pop(k, None)
        beh = {"format_version": "1.21.0", "minecraft:entity": {
            "description": {"identifier": full, "is_spawnable": True, "is_summonable": True, "is_experimental": False},
            "components": comps}}
        (bp / "entities").mkdir(parents=True, exist_ok=True)
        (bp / "entities" / f"{eid}.json").write_text(json.dumps(beh, indent=1))

        # sounds: the mod's own ambient/hurt/death/step events -> RP entity_sounds (an overlay can override)
        for kind, sid in (e.get("sounds") or {}).items():
            if ":" in sid and sid.split(":")[0] == modid: sound_defs.setdefault(full, {})[kind] = f"{ns}.{sid.split(':')[1]}"
        nice = jlang.get(f"entity.{modid}.{eid}") or e["class"][:-6]
        lang += [f"entity.{full}.name={nice}", f"item.spawn_egg.entity.{full}.name=Spawn {nice} ({eid})"]
        report["generated"].append({"id": full, "geo": gid, "texture": tex_key, "anims": sorted(anims),
                                    "health": at.get("max_health"), "speed": at.get("movement_speed"), "damage": at.get("attack_damage"),
                                    "events": sorted((e.get("events") or {}).keys())})

    if glow_used:
        (rp / "render_controllers").mkdir(parents=True, exist_ok=True)
        (rp / "render_controllers" / f"{ns}_glow.render_controllers.json").write_text(json.dumps({"format_version": "1.8.0", "render_controllers": {
            f"controller.render.{ns}.glow": {"geometry": "Geometry.default", "materials": [{"*": "Material.glow"}], "textures": ["Texture.glow"], "ignore_lighting": True}}}, indent=1))

    # ---- every entity texture (layers like glowing eyes are referenced by overlays) ----
    for n in names:
        if n.startswith(f"assets/{modid}/textures/entit") and n.endswith(".png"):
            dst = rp / "textures/entity" / ns / Path(n).name
            if not dst.exists(): dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(z.read(n))

    # ---- sounds: copy every .ogg, register the mod's sounds.json events ----
    sj = f"assets/{modid}/sounds.json"
    defs = {}
    if sj in names:
        for ev, spec in jload(z.read(sj)).items():
            files = []
            for s_ in spec.get("sounds", []):
                nm = s_ if isinstance(s_, str) else s_.get("name")
                if not nm: continue
                src = f"assets/{modid}/sounds/{nm.split(':')[-1]}.ogg"
                if src in names:
                    dst = rp / "sounds" / ns / f"{nm.split(':')[-1]}.ogg"; dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(z.read(src))
                    files.append({"name": f"sounds/{ns}/{nm.split(':')[-1]}"})
            if files: defs[f"{ns}.{ev}"] = {"category": spec.get("category", "hostile"), "sounds": files}
    ent_sounds = {eid_: {"volume": 1.0, "pitch": [0.9, 1.1], "events": {k: v for k, v in evs.items() if v in defs}}
                  for eid_, evs in sound_defs.items()}
    ent_sounds = {k: v for k, v in ent_sounds.items() if v["events"]}
    if ent_sounds:
        (rp / "sounds.json").write_text(json.dumps({"entity_sounds": {"entities": ent_sounds}}, indent=1))
        report["generated_sounds"] = len(ent_sounds)
    if defs:
        (rp / "sounds").mkdir(exist_ok=True)
        (rp / "sounds" / "sound_definitions.json").write_text(json.dumps({"format_version": "1.20.20", "sound_definitions": defs}, indent=1))

    (rp / "texts").mkdir(exist_ok=True)
    (rp / "texts" / "en_US.lang").write_text("\n".join(sorted(set(lang))) + "\n")
    (rp / "texts" / "languages.json").write_text('["en_US"]')
    (rp / "manifest.json").write_text(json.dumps(rpm, indent=2)); (bp / "manifest.json").write_text(json.dumps(bpm, indent=2))
    if f"logo.png" in names:
        for d in (rp, bp): (d / "pack_icon.png").write_bytes(z.read("logo.png"))

    # ---- behaviour overlay (our own code: scripts, entity overrides, items, recipes) ----
    ov_dir = a.overlay or (Path(__file__).parent / "overlays" / modid)
    if ov_dir.exists() and (ov_dir / "overlay.json").exists():
        ov = json.loads((ov_dir / "overlay.json").read_text())
        if (ov_dir / "scripts").exists():
            shutil.copytree(ov_dir / "scripts", bp / "scripts", dirs_exist_ok=True)
            bpm["modules"].append({"type": "script", "language": "javascript", "uuid": str(uuid.uuid4()), "entry": "scripts/main.js", "version": [1, 0, 0]})
            bpm["dependencies"] += ov.get("script_deps", [])
        for key, change in (ov.get("entity_overrides") or {}).items():
            targets = [f for f in (bp / "entities").glob("*.json") if (key == "*" or (f.stem.startswith(key[1:].split("#")[0]) if key.startswith("*") else f.stem == key.split("#")[0]))]
            for f in targets:
                d = json.loads(f.read_text()); c = d["minecraft:entity"]["components"]
                if f.stem in change.get("except", []) or (change.get("if_has") and change["if_has"] not in c): continue
                for k in change.get("remove", []): c.pop(k, None)
                c.update(change.get("set", {})); f.write_text(json.dumps(d, indent=1))
            if not targets: report["warnings"].append(f"overlay override '{key}' matched no entity")
        item_tex = {}
        for it in ov.get("items", []):
            iid = f"{ns}:{it['id']}"
            comps = {"minecraft:icon": it["id"], "minecraft:display_name": {"value": it.get("name", it["id"])},
                     "minecraft:max_stack_size": 1 if it.get("durability") else it.get("max_stack", 64)}
            if it.get("durability"): comps["minecraft:durability"] = {"max_durability": it["durability"]}
            (bp / "items").mkdir(exist_ok=True)
            (bp / "items" / f"{it['id']}.json").write_text(json.dumps({"format_version": "1.21.0", "minecraft:item": {
                "description": {"identifier": iid, "menu_category": {"category": "equipment"}}, "components": comps}}, indent=1))
            if it.get("texture") in names:
                dst = rp / "textures/items" / ns / f"{it['id']}.png"; dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(z.read(it["texture"]))
                item_tex[it["id"]] = {"textures": f"textures/items/{ns}/{it['id']}"}
            if it.get("recipe"):
                r = it["recipe"]; (bp / "recipes").mkdir(exist_ok=True)
                (bp / "recipes" / f"{it['id']}.json").write_text(json.dumps({"format_version": "1.20.10", "minecraft:recipe_shaped": {
                    "description": {"identifier": f"{iid}_recipe"}, "tags": ["crafting_table"], "pattern": r["pattern"],
                    "key": {k: {"item": v} for k, v in r["key"].items()}, "unlock": [{"item": next(iter(r["key"].values()))}],
                    "result": {"item": iid, "count": 1}}}, indent=1))
            lang.append(f"item.{iid}={it.get('name', it['id'])}")
        if item_tex:
            (rp / "textures/item_texture.json").write_text(json.dumps({"resource_pack_name": title, "texture_name": "atlas.items", "texture_data": item_tex}, indent=1))
            (rp / "texts" / "en_US.lang").write_text("\n".join(sorted(set(lang))) + "\n")
        # random sound groups ("hurt" -> pick one of hurt_1..4), entity sound events, name overrides
        sd_path = rp / "sounds" / "sound_definitions.json"
        sdefs = json.loads(sd_path.read_text())["sound_definitions"] if sd_path.exists() else {}
        for g, evs in (ov.get("sound_groups") or {}).items():
            files = [f for ev in evs for f in sdefs.get(f"{ns}.{ev}", {}).get("sounds", [])]
            if files: sdefs[f"{ns}.{g}"] = {"category": "hostile", "sounds": files}
            else: report["warnings"].append(f"sound group {g}: none of {evs} found")
        if sdefs:
            sd_path.parent.mkdir(exist_ok=True)
            sd_path.write_text(json.dumps({"format_version": "1.20.20", "sound_definitions": sdefs}, indent=1))
        if ov.get("entity_sounds"):
            es = {"entity_sounds": {"entities": {f"{ns}:{e['id']}": {"volume": 1.0, "pitch": [0.9, 1.1],
                  "events": {k: f"{ns}.{v}" for k, v in ov["entity_sounds"].items()}} for e in data["entities"] if e.get("id")}}}
            (rp / "sounds.json").write_text(json.dumps(es, indent=1))
        if ov.get("lang"):
            kv = dict(l.split("=", 1) for l in lang if "=" in l); kv.update(ov["lang"])
            lang = [f"{k}={v}" for k, v in kv.items()]
            (rp / "texts" / "en_US.lang").write_text("\n".join(sorted(lang)) + "\n")

        # hand-authored files win over generated ones (custom AI entities, render controllers, ...)
        for side, root in (("rp", rp), ("bp", bp)):
            if (ov_dir / side).exists():
                shutil.copytree(ov_dir / side, root, dirs_exist_ok=True)
                report["warnings"] += [f"overlay replaced {side}/{f.relative_to(ov_dir / side)}" for f in (ov_dir / side).rglob("*") if f.is_file()]
        if ov.get("credits"):
            for m in (rpm, bpm): m["header"]["description"] += " | " + ov["credits"]
            (rp / "manifest.json").write_text(json.dumps(rpm, indent=2))
            if ov.get("license_file") and (ov_dir / ov["license_file"]).exists():
                for root in (rp, bp): shutil.copy(ov_dir / ov["license_file"], root / "LICENSE_original_mod.txt")
        (bp / "manifest.json").write_text(json.dumps(bpm, indent=2))
        report["overlay"] = str(ov_dir.name)

    # ---- report ----
    L = [f"# ModPorter report: {data['jar']}", "", f"- entities: {len(report['generated'])}, geometry files: {len(geo_ids)}, animation files: {len(anim_maps)}, sounds: {len(defs)}",
         f"- procedures (behaviour not auto-ported): {data.get('procedure_count')}",
         f"- behaviour overlay: {report.get('overlay', 'none')}", "",
         "## Fallbacks", *([f"- {x}" for x in report["fallbacks"]] or ["- none"]), "", "## Warnings", *([f"- {x}" for x in report["warnings"]] or ["- none"]), "",
         "## Entities", "", "| id | hp | speed | dmg | anims | procedure events |", "|---|---|---|---|---|---|"]
    for g in report["generated"]:
        L.append(f"| `{g['id']}` | {g['health']} | {g['speed']} | {g['damage']} | {', '.join(g['anims']) or '-'} | {', '.join(g['events']) or '-'} |")
    (a.out / "PORT_REPORT.md").write_text("\n".join(L) + "\n")
    print(f"generated {len(report['generated'])} entities, {len(geo_ids)} geo, {len(anim_maps)} anim files, {len(defs)} sounds -> {a.out}")

if __name__ == "__main__":
    main()
