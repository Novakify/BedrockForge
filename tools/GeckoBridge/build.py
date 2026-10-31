#!/usr/bin/env python3
"""GeckoBridge M1 builder: a GeckoLib mod jar -> private Bedrock RP + BP for every GeckoLib mob in it.

    python -I tools/GeckoBridge/build.py mod.jar OUT [--name "Naturalist"] [--tol 0.05]

Pipeline (all offline):
  discover.py      entities, ids, sizes, model/texture/animation paths, texture variants, layers, stats
  controllers.py   GeckoLib controllers -> condition paths
  ModelCore.rig    geometry (renamed geometry.gb.<mod>.<file>)
  gbanim           animations, every GeckoLib easing baked exactly; Molang fixed + checked (molang.py)
  here             client entities, Bedrock animation controllers, render controllers (texture/geometry variants,
                   glow through the gb_glow material, owner hook v.gb_hidden_parts), sounds, BP entities, report.json
The packs are built from the mod's assets: private personal use only, never shared (see CREDITS.txt in the packs).
"""
from __future__ import annotations
import argparse, json, re, sys, uuid, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent))
import gbanim, molang, discover                                   # noqa: E402
from ModelCore.rig import load_bedrock_geo, write_bedrock_geo       # noqa: E402

GB = "gb"
GLOW_MATERIAL = {"materials": {"version": "1.0.0", "gb_glow:entity_alphablend": {"+states": ["DisableCulling"]}}}
OWNER_VAR = "v.gb_hidden_parts"   # reserved for Figura / Bedrock Forge: non-zero hides the mob's parts (M3: bitmask)


def u5(*parts): return str(uuid.uuid5(uuid.NAMESPACE_URL, "geckobridge/" + "/".join(parts)))
def clean(s): return re.sub(r"[^a-z0-9_]+", "_", s.lower()).strip("_")


def jload(b):
    t = re.sub(r"(?m)^\s*//[^\n]*", "", b.decode("utf-8-sig", "replace")); t = re.sub(r",(\s*[}\]])", r"\1", t)
    return json.loads(t)


class Build:
    def __init__(self, jar, out, name=None, tol=None):
        self.z = zipfile.ZipFile(jar); self.names = set(self.z.namelist()); self.out = Path(out)
        if tol: gbanim.TOL.update({"rotation": tol, "position": tol / 10, "scale": tol / 50})
        self.ents, self.ctrl = discover.discover(jar)
        self.modid = self.ents["mod_id"]; self.ns = clean(self.modid)
        self.title = name or self.modid.replace("_", " ").title()
        self.rp = self.out / f"GB {self.title} RP"; self.bp = self.out / f"GB {self.title} BP"
        self.report = {"jar": Path(jar).name, "mod_id": self.modid, "entities": {}, "molang_errors": [], "warnings": [],
                       "baked_segments": {}, "bake_error_max": {"rotation": 0, "position": 0, "scale": 0}}
        self.geo_ids, self.anim_ids, self.anim_meta, self.rcs, self.acs, self.sound_defs = {}, {}, {}, {}, {}, {}
        self.files_written = 0

    # ---------------------------------------------------------------- helpers
    def write(self, rel_root, rel, data):
        p = rel_root / rel; p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, (dict, list)): p.write_text(json.dumps(data, indent=1))
        elif isinstance(data, bytes): p.write_bytes(data)
        else: p.write_text(data)
        self.files_written += 1

    def check(self, where, expr):
        r = molang.check(expr)
        if r: self.report["molang_errors"].append({"where": where, "expr": expr, "error": r})
        return r is None

    # ---------------------------------------------------------------- geometry
    def geometry(self, path):
        if path in self.geo_ids: return self.geo_ids[path]
        if path not in self.names: self.report["warnings"].append(f"geometry missing: {path}"); return None
        stem = clean(Path(path).name.replace(".geo.json", "").replace(".json", ""))
        rigs = load_bedrock_geo(jload(self.z.read(path)))
        ids = []
        for i, rig in enumerate(rigs):
            rig.identifier = f"geometry.{GB}.{self.ns}.{stem}" + (f"_{i}" if i else ""); ids.append(rig.identifier)
        self.write(self.rp, f"models/entity/{self.ns}/{stem}.geo.json", write_bedrock_geo(rigs))
        self.geo_ids[path] = ids[0]
        return ids[0]

    # ---------------------------------------------------------------- animations
    def animations(self, path):
        """Convert one GeckoLib animation file -> {source name: bedrock id}; written once per file."""
        if path in self.anim_ids: return self.anim_ids[path]
        if path not in self.names: self.report["warnings"].append(f"animation file missing: {path}"); self.anim_ids[path] = {}; return {}
        stem = clean(Path(path).name.split(".")[0])
        src = jload(self.z.read(path)).get("animations", {})
        out, ids, rep = {}, {}, {}
        for name, a in src.items():
            short = clean(re.sub(r"^animation\.", "", name))
            nid = f"animation.{GB}.{self.ns}.{stem}.{short}"
            conv = gbanim.convert_animation(name, a, nid, report=rep)
            err, _ = gbanim.measure_error(a, conv[nid])
            for k, v in err.items():
                if v > self.report["bake_error_max"][k]: self.report["bake_error_max"][k] = v
            out.update(conv); ids[name] = nid; self.anim_meta[nid] = {"loop": a.get("loop", False), "length": a.get("animation_length"),
                                                                      "src": a}
        for k, v in rep.get("baked_segments", {}).items(): self.report["baked_segments"][k] = self.report["baked_segments"].get(k, 0) + v
        if rep.get("molang_whitespace_joined"): self.report.setdefault("molang_whitespace_joined", []).extend(rep["molang_whitespace_joined"])
        if rep.get("dropped_markers"): self.report.setdefault("dropped_markers", {}).update(rep["dropped_markers"])
        # sound keyframes: GeckoLib hands the effect name to the mod's Java handler; we map it to the mod's sounds.json
        for nid, a in out.items():
            se_out = {}
            for t, se in (self.anim_meta[nid]["src"].get("sound_effects") or {}).items():
                for one in (se if isinstance(se, list) else [se]):
                    short = self.sound(one.get("effect", "") if isinstance(one, dict) else str(one))
                    if short: se_out.setdefault(t, []).append({"effect": short})
            if se_out: a["sound_effects"] = {t: (v[0] if len(v) == 1 else v) for t, v in se_out.items()}
            self._walk_molang(nid, a)
        self.anim_files = getattr(self, "anim_files", {}); self.anim_files[stem] = out
        self.anim_ids[path] = ids
        return ids

    def _walk_molang(self, where, a):
        for bone, chans in (a.get("bones") or {}).items():
            for ch, val in chans.items():
                vals = [val] if isinstance(val, list) else list(val.values()) if isinstance(val, dict) else []
                for v in vals:
                    for x in (v if isinstance(v, list) else (v.get("pre", []) + v.get("post", [])) if isinstance(v, dict) else []):
                        if isinstance(x, str): self.check(f"{where}/{bone}/{ch}", x)
        if a.get("anim_time_update"): self.check(f"{where}/anim_time_update", a["anim_time_update"])

    def java_sounds(self):
        if not hasattr(self, "_sounds"):
            p = f"assets/{self.modid}/sounds.json"
            self._sounds = jload(self.z.read(p)) if p in self.names else {}
        return self._sounds

    def sound(self, effect):
        """GeckoLib sound keyframe effect -> Bedrock short name (client entity sound_effects), or None."""
        if not effect: return None
        sj = self.java_sounds(); key = effect.split(":")[-1]
        cand = [k for k in sj if k == key or k.endswith("." + key) or k.replace(".", "_") == key.replace(".", "_")]
        if not cand:
            self.report.setdefault("sounds_unmapped", {}); self.report["sounds_unmapped"][effect] = self.report["sounds_unmapped"].get(effect, 0) + 1
            return None
        ev = cand[0]; bid = f"{GB}.{self.ns}.{clean(ev)}"
        if bid not in self.sound_defs:
            files = []
            for snd in sj[ev].get("sounds", []):
                name = snd["name"] if isinstance(snd, dict) else snd
                ns_, rel = (name.split(":", 1) if ":" in name else (self.modid, name))
                src = f"assets/{ns_}/sounds/{rel}.ogg"
                if src in self.names:
                    dst = f"sounds/{GB}/{self.ns}/{clean(rel)}"
                    self.write(self.rp, dst + ".ogg", self.z.read(src))
                    files.append({"name": dst, **({k: snd[k] for k in ("volume", "pitch") if isinstance(snd, dict) and k in snd})})
            if not files:
                self.report.setdefault("sounds_unmapped", {})[effect] = "vanilla/missing files"; return None
            self.sound_defs[bid] = {"category": "neutral", "sounds": files}
        short = clean(ev)
        self.entity_sounds = getattr(self, "entity_sounds", {}); self.entity_sounds[short] = bid
        return short

    def variant_anim(self, nid, loop=None, speed=None):
        """A copy of an animation with GeckoLib's controller-side loop override or speed."""
        stem = nid.split(".")[3]; base = self.anim_files[stem][nid]
        if loop is not None and base.get("loop", False) == loop: loop = None      # already loops that way
        if loop is None and (speed in (None, 1, 1.0)): return nid
        suffix = (f".loop_{loop}" if loop is not None else "") + (f".x{str(round(speed, 3)).replace('.', '_')}" if speed not in (None, 1, 1.0) else "")
        vid = nid + suffix
        if vid not in self.anim_files[stem]:
            a = json.loads(json.dumps(base))
            if loop is not None: a["loop"] = loop
            if speed not in (None, 1, 1.0):
                a["anim_time_update"] = f"q.anim_time + q.delta_time * {round(float(speed), 4)}"
            self.anim_files[stem][vid] = a
        return vid

    # ---------------------------------------------------------------- controllers
    def chain_states(self, chain, amap):
        """RawAnimation chain -> list of (bedrock anim id or None for a wait, wait seconds)."""
        steps = []
        for step in chain:
            kind, args = step[0], list(step[1:])
            if kind in ("unresolved",): continue
            if kind == "thenWait": steps.append((None, (args[0] if args else 0) / 20)); continue
            name = args[0] if args else None
            nid = amap.get(name) or next((v for k, v in amap.items() if k.split(".")[-1] == str(name).split(".")[-1]), None)
            if not nid:
                self.report["warnings"].append(f"animation '{name}' not found in the entity's animation file"); continue
            loop = {"thenLoop": True, "thenPlayAndHold": "hold_on_last_frame", "thenPlay": None, "then": None}.get(kind)
            reps = int(args[1]) if kind == "thenPlayXTimes" and len(args) > 1 else 1
            for _ in range(reps): steps.append((nid, loop))
        return steps

    def controller(self, eid, ci, c, amap, speedmap):
        """One GeckoLib controller -> one Bedrock animation controller (states = distinct outcomes)."""
        cname = f"controller.animation.{GB}.{self.ns}.{eid}.{clean(c['name']) or ci}"
        outcomes = {}   # key -> {"when": [..], "steps": [...]}
        for p in c.get("paths", []):
            steps = []
            for chain in p["anims"][-1:]:      # the last setAnimation on a path wins in GeckoLib
                for nid, loop in self.chain_states(chain, amap):
                    if nid is None: steps.append(("wait", loop)); continue
                    sp = p.get("speed") if isinstance(p.get("speed"), (int, float)) else None
                    steps.append(("anim", self.variant_anim(nid, loop=loop if loop is not None else None, speed=sp)))
            key = json.dumps(steps)
            outcomes.setdefault(key, {"when": [], "steps": steps})["when"].append(p["when"])
        if not outcomes: return None
        states, order = {}, []
        def cond(ws): return "1" if "1" in ws else " || ".join(f"({w})" for w in ws) if len(ws) > 1 else ws[0]
        for i, (key, o) in enumerate(outcomes.items()):
            base = clean("_".join(self.short_of(s[1]).split("_", 1)[-1] if s[0] == "anim" else "wait" for s in o["steps"]))[:40] or "none"
            sname = base if base not in states else f"{base}_{i}"
            o["state"] = sname; o["cond"] = cond(o["when"]); order.append(o)
            states[sname] = None
        states = {}
        for o in order:
            sub = []
            for j, (kind, val) in enumerate(o["steps"]):
                sub.append((f"{o['state']}" if j == 0 else f"{o['state']}__{j}", kind, val))
            if not sub: sub = [(o["state"], "none", None)]       # GeckoLib PlayState.STOP: nothing plays
            for j, (sname, kind, val) in enumerate(sub):
                st = {"transitions": []}
                if kind == "anim":
                    short = self.short_of(val); st["animations"] = [short]
                # leave for the state another outcome wants (exclusive conditions); own condition false -> others
                for other in order:
                    if other is o: continue
                    if other["cond"] != "1": st["transitions"].append({other["state"]: other["cond"]})
                if o["cond"] != "1":
                    fallback = next((x for x in order if x["cond"] == "1"), None)
                    if fallback and fallback is not o: st["transitions"].append({fallback["state"]: f"!({o['cond']})"})
                if j + 1 < len(sub):   # chained RawAnimation: next step when this one is done
                    nxt = sub[j + 1][0]
                    st["transitions"].append({nxt: "q.all_animations_finished" if kind == "anim" else f"q.state_time >= {val}"})
                if c.get("transition_ticks"): st["blend_transition"] = round(c["transition_ticks"] / 20, 3)
                if not st["transitions"]: st.pop("transitions")
                states[sname] = st
        for st in states.values():
            for tr in st.get("transitions", []):
                for k, v in tr.items(): self.check(f"{cname}/{k}", v)
        initial = next((o["state"] for o in order if o["cond"] == "1"), order[-1]["state"])   # Java's final else-branch
        self.acs[cname] = {"initial_state": initial, "states": states}
        return cname

    def short_of(self, nid):
        return nid.split(".", 4)[-1].replace(".", "_")

    # ---------------------------------------------------------------- per entity
    def entity(self, e, ctrl_entity):
        eid = clean(e["id"]); full = f"{self.ns}:{eid}"
        er = self.report["entities"].setdefault(eid, {"java": e["class"]})
        # geometry (+ baby/other models chosen in getModelResource)
        geos = [{"when": "1", "path": e["geo"]}] if not e.get("geo_variants") else e["geo_variants"]
        geo_map = {}
        for i, g in enumerate(geos):
            gid = self.geometry(g["path"]) if g.get("path") else None
            if gid: geo_map[f"g{i}"] = (gid, g["when"])
        if not geo_map: er["skipped"] = "no geometry"; return
        # textures (+ variants)
        tex_map = {}
        for i, v in enumerate(e.get("variants") or []):
            if v["texture"] not in self.names: continue
            key = f"textures/entity/{GB}/{self.ns}/{clean(Path(v['texture']).stem)}"
            if "/" in v["texture"].split("textures/entity/")[-1]:
                key = f"textures/entity/{GB}/{self.ns}/" + clean(v["texture"].split("textures/entity/")[-1][:-4].replace("/", "_"))
            self.write(self.rp, key + ".png", self.z.read(v["texture"])); tex_map[f"t{i}"] = (key, v["when"])
        if not tex_map: er["skipped"] = "no texture"; return
        desc = {"identifier": full, "materials": {"default": "entity_alphatest"},
                "textures": {k: v[0] for k, v in tex_map.items()}, "geometry": {k: v[0] for k, v in geo_map.items()},
                "render_controllers": [], "spawn_egg": {"base_color": "#2b2b2b", "overlay_color": "#7a3cff"},
                "scripts": {"pre_animation": [f"{OWNER_VAR} = {OWNER_VAR} ?? 0;"], "animate": []}}
        # reserved condition variables (0 until the M2 runtime sets them)
        reserved = set(e.get("reserved", {}).values())
        for c in ctrl_entity.get("controllers", []): reserved |= set((c.get("unknown_conditions") or {}).values())
        for r in sorted(reserved): desc["scripts"]["pre_animation"].append(f"{r} = {r} ?? 0;")
        if reserved: er["reserved_vars"] = sorted(reserved)
        # main render controller: geometry + texture picked by GeckoLib's own conditions
        rc = f"controller.render.{GB}.{self.ns}.{eid}"
        def pick(kind, m):
            items = list(m.items())
            if len(items) == 1: return f"{kind}.{items[0][0]}"
            expr = f"{kind}.{next((k for k, v in items if v[1] == '1'), items[-1][0])}"
            for k, (_, when) in reversed([x for x in items if x[1][1] != "1"]):
                expr = f"({when}) ? {kind}.{k} : ({expr})"
            return expr
        rcd = {"geometry": pick("Geometry", geo_map), "materials": [{"*": "Material.default"}],
               "textures": [pick("Texture", tex_map)], "part_visibility": [{"*": f"{OWNER_VAR} == 0"}]}
        for k in ("geometry",): self.check(f"{rc}/{k}", rcd[k])
        self.check(f"{rc}/texture", rcd["textures"][0])
        self.rcs[rc] = rcd; desc["render_controllers"].append(rc)
        # glow (GeckoLib AutoGlowingGeoLayer / emissive custom layers) through the gb_glow material
        glows = []
        if e.get("glowmask") and e["glowmask"] in self.names: glows.append(e["glowmask"])
        for L in e.get("layers", []):
            if L.get("kind") == "custom":
                for t in L.get("textures", []):
                    if any(w in t.lower() for w in ("eye", "glow", "emissive")): glows.append(t)
                    else: self.report["warnings"].append(f"{full}: custom layer {L['class'].split('.')[-1]} texture {t} not drawn yet (M2)")
        for i, g in enumerate(dict.fromkeys(glows)):
            key = f"textures/entity/{GB}/{self.ns}/{clean(Path(g).stem)}"
            self.write(self.rp, key + ".png", self.z.read(g))
            desc["textures"][f"glow{i}"] = key; desc["materials"]["glow"] = "gb_glow"
            grc = f"controller.render.{GB}.{self.ns}.{eid}.glow{i}"
            self.rcs[grc] = {"geometry": rcd["geometry"], "materials": [{"*": "Material.glow"}], "textures": [f"Texture.glow{i}"],
                             "ignore_lighting": True, "part_visibility": [{"*": f"{OWNER_VAR} == 0"}]}
            desc["render_controllers"].append(grc); er["glow"] = er.get("glow", 0) + 1
        # animations + controllers
        amap = self.animations(e["animation_file"]) if e.get("animation_file") else {}
        cnames = []
        for ci, c in enumerate(ctrl_entity.get("controllers", [])):
            cn = self.controller(eid, ci, c, amap, {})
            if cn: cnames.append(cn)
        used = set()
        for cn in cnames:
            for st in self.acs[cn]["states"].values(): used |= set(st.get("animations", []))
        anims = {}
        for stem, files in getattr(self, "anim_files", {}).items():
            for nid in files:
                if self.short_of(nid) in used: anims[self.short_of(nid)] = nid
        for i, cn in enumerate(cnames):
            key = f"ctrl_{i}"; anims[key] = cn; desc["scripts"]["animate"].append(key)
        if not cnames and amap:   # no controller found: play the idle-like animation so the mob isn't frozen
            idle = next((v for k, v in amap.items() if "idle" in k.lower()), next(iter(amap.values())))
            anims[self.short_of(idle)] = idle; desc["scripts"]["animate"].append(self.short_of(idle)); er["fallback_idle"] = idle
        desc["animations"] = anims
        if getattr(self, "entity_sounds", None): desc["sound_effects"] = {k: {"effect": v} for k, v in self.entity_sounds.items()}
        if not desc["scripts"]["animate"]: desc["scripts"].pop("animate")
        er.update({"geometry": [v[0] for v in geo_map.values()], "textures": len(tex_map), "controllers": len(cnames),
                   "animations_used": len(anims) - len(cnames)})
        self.write(self.rp, f"entity/{self.ns}/{eid}.entity.json", {"format_version": "1.10.0", "minecraft:client_entity": {"description": desc}})
        self.behaviour(e, eid, full, any("q.variant" in v[1] for v in tex_map.values()), len(tex_map))
        nice = e.get("name") or e["class"]
        self.lang += [f"entity.{full}.name={nice}", f"item.spawn_egg.entity.{full}.name=Spawn {nice} (GeckoBridge)"]

    def behaviour(self, e, eid, full, uses_variant, ntex):
        at = e.get("attributes") or {}; w, h = (e.get("size") or [0.6, 1.8])
        hostile = e.get("kind") == "hostile"
        comps = {
            "minecraft:type_family": {"family": ["geckobridge", self.ns, "mob"] + (["monster"] if hostile else [])},
            "minecraft:health": {"value": at.get("max_health", 20), "max": at.get("max_health", 20)},
            "minecraft:movement": {"value": round(at.get("movement_speed", 0.25), 3)},
            "minecraft:collision_box": {"width": w, "height": h},
            "minecraft:physics": {}, "minecraft:pushable": {"is_pushable": True, "is_pushable_by_piston": True},
            "minecraft:jump.static": {}, "minecraft:movement.basic": {},
            "minecraft:navigation.walk": {"can_path_over_water": True, "avoid_damage_blocks": True},
            "minecraft:behavior.float": {"priority": 0},
            "minecraft:behavior.random_stroll": {"priority": 6, "speed_multiplier": 1.0},
            "minecraft:behavior.look_at_player": {"priority": 7, "look_distance": 8},
            "minecraft:behavior.random_look_around": {"priority": 8},
            "minecraft:behavior.hurt_by_target": {"priority": 1},
            "minecraft:persistent": {},
        }
        if at.get("attack_damage"):
            comps["minecraft:attack"] = {"damage": at["attack_damage"]}
            comps["minecraft:behavior.melee_attack"] = {"priority": 2, "speed_multiplier": 1.25, "track_target": True}
        if hostile:
            comps["minecraft:behavior.nearest_attackable_target"] = {"priority": 2, "must_see": True, "reselect_targets": True,
                "entity_types": [{"filters": {"test": "is_family", "subject": "other", "value": "player"}, "max_dist": at.get("follow_range", 16)}]}
        mv = e.get("movement", "walk")
        if mv == "fly":
            for k in ("minecraft:navigation.walk", "minecraft:movement.basic", "minecraft:jump.static", "minecraft:behavior.random_stroll"): comps.pop(k)
            comps.update({"minecraft:can_fly": {}, "minecraft:flying_speed": {"value": 0.1}, "minecraft:physics": {"has_gravity": False},
                "minecraft:movement.hover": {}, "minecraft:navigation.hover": {"can_path_over_water": True, "can_sink": False, "can_path_from_air": True},
                "minecraft:behavior.random_hover": {"priority": 6, "xz_dist": 8, "y_dist": 4, "y_offset": -1, "interval": 1, "hover_height": [1, 4]}})
        elif mv == "swim":
            for k in ("minecraft:navigation.walk", "minecraft:movement.basic", "minecraft:jump.static", "minecraft:behavior.random_stroll", "minecraft:behavior.float"): comps.pop(k)
            comps.update({"minecraft:breathable": {"breathes_water": True, "breathes_air": False, "suffocate_time": 0},
                "minecraft:underwater_movement": {"value": 0.08},
                "minecraft:navigation.generic": {"can_path_over_water": False, "can_swim": True, "can_walk": False, "can_breach": False},
                "minecraft:movement.sway": {"sway_amplitude": 0}, "minecraft:behavior.swim_wander": {"priority": 6, "speed_multiplier": 1.0, "look_ahead": 2, "wander_time": 5}})
        ent = {"description": {"identifier": full, "is_spawnable": True, "is_summonable": True, "is_experimental": False}, "components": comps}
        if uses_variant:
            ent["component_groups"] = {f"gb_variant_{i}": {"minecraft:variant": {"value": i}} for i in range(ntex)}
            ent["events"] = {"minecraft:entity_spawned": {"randomize": [{"weight": 1, "add": {"component_groups": [f"gb_variant_{i}"]}} for i in range(ntex)]}}
        self.write(self.bp, f"entities/{self.ns}/{eid}.json", {"format_version": "1.21.0", "minecraft:entity": ent})

    # ---------------------------------------------------------------- run
    def run(self):
        import shutil
        for d in (self.rp, self.bp): shutil.rmtree(d, ignore_errors=True)
        self.lang = []
        by_class = {c["class"]: c for c in self.ctrl["entities"]}
        for e in self.ents["entities"]:
            if not e.get("is_mob", True): self.report["entities"][clean(e["id"])] = {"skipped": "not a mob (projectile etc.) - M3"}; continue
            self.entity(e, by_class.get(e["java_class"], {}))
        for stem, files in getattr(self, "anim_files", {}).items():
            self.write(self.rp, f"animations/{self.ns}/{stem}.animation.json", {"format_version": "1.8.0", "animations": files})
        if self.acs: self.write(self.rp, f"animation_controllers/{self.ns}.ac.json", {"format_version": "1.10.0", "animation_controllers": self.acs})
        self.write(self.rp, f"render_controllers/{self.ns}.rc.json", {"format_version": "1.8.0", "render_controllers": self.rcs})
        self.write(self.rp, "materials/entity.material", GLOW_MATERIAL)
        if self.sound_defs: self.write(self.rp, "sounds/sound_definitions.json", {"format_version": "1.14.0", "sound_definitions": self.sound_defs})
        self.write(self.rp, "texts/en_US.lang", "\n".join(self.lang) + "\n"); self.write(self.rp, "texts/languages.json", ["en_US"])
        rph = u5(self.ns, "rp"); bph = u5(self.ns, "bp")
        credit = (f"Models, textures and animations from {self.report['jar']} ({self.modid}) by its original authors; "
                  f"GeckoLib (MIT). Converted by GeckoBridge (Bedrock Forge) for Fernando's private personal use only. Do not share.\n")
        for root, kind, h, dep in ((self.rp, "resources", rph, bph), (self.bp, "data", bph, rph)):
            self.write(root, "manifest.json", {"format_version": 2, "header": {"name": f"GB {self.title} {'RP' if kind == 'resources' else 'BP'}",
                "description": "GeckoBridge private conversion", "uuid": h, "version": [0, 1, 0], "min_engine_version": [1, 21, 0]},
                "modules": [{"type": kind, "uuid": u5(self.ns, kind, "m"), "version": [0, 1, 0]}],
                "dependencies": [{"uuid": dep, "version": [0, 1, 0]}]})
            self.write(root, "CREDITS.txt", credit)
        # budget
        kf = sum(len(v) if isinstance(v, dict) else 1 for files in getattr(self, "anim_files", {}).values() for a in files.values()
                 for b in (a.get("bones") or {}).values() for k, v in b.items() if k in ("rotation", "position", "scale"))
        size = sum(p.stat().st_size for p in (self.rp / "animations").rglob("*.json")) if (self.rp / "animations").exists() else 0
        self.report["budget"] = {"entities": len(self.report["entities"]), "keyframes": kf, "animation_bytes": size,
                                 "max_bones": max((len(g.get("bones", [])) for p in (self.rp / "models").rglob("*.json")
                                                   for g in json.loads(p.read_text())["minecraft:geometry"]), default=0)}
        self.report["rp_uuid"], self.report["bp_uuid"] = rph, bph
        self.write(self.out, "report.json", self.report)
        return self.report


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("jar"); ap.add_argument("out"); ap.add_argument("--name"); ap.add_argument("--tol", type=float)
    a = ap.parse_args()
    r = Build(a.jar, a.out, a.name, a.tol).run()
    if a.tol is None and r["budget"]["animation_bytes"] > 1.5 * 1024 * 1024:   # frame/load budget (M1 plan item 7)
        print(f"animations {r['budget']['animation_bytes'] // 1024} KB > 1.5 MB: rebuilding with 0.25 deg tolerance")
        r = Build(a.jar, a.out, a.name, 0.25).run(); r["budget"]["tolerance_deg"] = 0.25
        Path(a.out, "report.json").write_text(json.dumps(r, indent=1))
    ok = [k for k, v in r["entities"].items() if not v.get("skipped")]
    print(f"{r['mod_id']}: {len(ok)} mobs built, {len(r['entities']) - len(ok)} skipped, molang errors {len(r['molang_errors'])}, "
          f"warnings {len(r['warnings'])}, bake error max {r['bake_error_max']}, keyframes {r['budget']['keyframes']}, "
          f"animations {r['budget']['animation_bytes'] // 1024} KB")
