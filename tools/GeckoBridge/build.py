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
import gbanim, molang, discover, runtime, sounds as S             # noqa: E402
import controllers as C                                            # noqa: E402
from ModelCore.rig import load_bedrock_geo, write_bedrock_geo       # noqa: E402

GB = "gb"
GLOW_MATERIAL = {"materials": {"version": "1.0.0", "gb_glow:entity_alphablend": {"+states": ["DisableCulling"]}}}
OWNER_VAR = "v.gb_hidden_parts"   # reserved for Figura / Bedrock Forge: non-zero hides the mob's parts (M3: bitmask)


def bone_id(n):
    """Bedrock rejects bone names with spaces/slashes in animations ("child 'Right Leg' not valid here")."""
    return re.sub(r"[^A-Za-z0-9_.\-]", "_", n)


def u5(*parts): return str(uuid.uuid5(uuid.NAMESPACE_URL, "geckobridge/" + "/".join(parts)))
def clean(s): return re.sub(r"[^a-z0-9_]+", "_", s.lower()).strip("_")


def jload(b):
    t = re.sub(r"(?m)^\s*//[^\n]*", "", b.decode("utf-8-sig", "replace")); t = re.sub(r",(\s*[}\]])", r"\1", t)
    return json.loads(t)


def cached_discover(jar):
    """discover() takes minutes on big mods (javap per class): cache by jar + converter source."""
    import hashlib
    h = hashlib.sha1(Path(jar).read_bytes())
    for f in ("discover.py", "controllers.py"): h.update((HERE / f).read_bytes())
    cache = Path("/tmp/gb_cache") / f"{Path(jar).stem}.{h.hexdigest()[:12]}.json"
    if cache.exists(): return tuple(json.loads(cache.read_text()))
    r = discover.discover(jar); cache.parent.mkdir(exist_ok=True); cache.write_text(json.dumps(r, default=list))
    return json.loads(json.dumps(r, default=list))


def cached_triggers(jar):
    import hashlib
    h = hashlib.sha1(Path(jar).read_bytes()); h.update((HERE / "runtime.py").read_bytes()); h.update((HERE / "controllers.py").read_bytes())
    cache = Path("/tmp/gb_cache") / f"{Path(jar).stem}.trig.{h.hexdigest()[:12]}.json"
    if cache.exists(): return json.loads(cache.read_text())
    r = runtime.triggers(jar); cache.parent.mkdir(exist_ok=True); cache.write_text(json.dumps(r)); return r


_CONV = {}
def _convert_structure(job):
    jar, n, modid, ns, custom = job
    try:
        sys.path.insert(0, str(HERE.parent / "ModPorter"))
        import structures as ST
        key = (jar, modid)
        if key not in _CONV: _CONV[key] = (zipfile.ZipFile(jar), ST.Converter(modid, ns, custom))
        z, cv = _CONV[key]
        return n, cv.convert(z.read(n)), None
    except Exception as ex:
        return n, None, f"{type(ex).__name__}: {ex}"


def cached_classinfo(jar):
    """Block/item properties + registry names from bytecode (ModPorter classinfo), cached."""
    import hashlib, tempfile
    sys.path.insert(0, str(HERE.parent / "ModPorter"))
    import classinfo as CI
    h = hashlib.sha1(Path(jar).read_bytes()); h.update((HERE.parent / "ModPorter" / "classinfo.py").read_bytes())
    cache = Path("/tmp/gb_cache") / f"{Path(jar).stem}.ci.{h.hexdigest()[:12]}.json"
    if cache.exists(): return json.loads(cache.read_text())
    tmp = tempfile.mkdtemp(prefix="gb_ci_")
    with zipfile.ZipFile(jar) as z:
        cls = [n for n in z.namelist() if n.endswith(".class") and not n.startswith(C.GL_PREFIXES)]
        for n in cls: z.extract(n, tmp)
    names = [n[:-6].replace("/", ".") for n in cls]
    reg = CI.registries(tmp, [n for n in names if n.lower().endswith(("blocks", "items", "mobeffects"))])
    out = {"registries": reg, "blocks": {}, "items": {}}
    for rn, c in reg["block"].items(): out["blocks"][rn] = CI.block_props(tmp, c)
    for rn, c in reg["item"].items(): out["items"][rn] = CI.item_info(tmp, c)
    cache.parent.mkdir(exist_ok=True); cache.write_text(json.dumps(out)); return out


def cached_sounds(jar, classes):
    import hashlib
    h = hashlib.sha1(Path(jar).read_bytes()); h.update((HERE / "sounds.py").read_bytes())
    cache = Path("/tmp/gb_cache") / f"{Path(jar).stem}.snd.{h.hexdigest()[:12]}.json"
    if cache.exists(): return json.loads(cache.read_text())
    r = S.mob_sounds(jar, classes); cache.parent.mkdir(exist_ok=True); cache.write_text(json.dumps(r)); return r


OVERLAYS = HERE.parent / "ModPorter" / "overlays"
# overlay rp/ files describing an entity's look: GeckoBridge's own conversion wins for entities it built
VISUAL_DIRS = ("entity", "render_controllers", "animation_controllers", "animations", "models")


class Build:
    def __init__(self, jar, out, name=None, tol=None):
        self.jar = jar; self.z = zipfile.ZipFile(jar); self.names = set(self.z.namelist()); self.out = Path(out)
        if tol: gbanim.TOL.update({"rotation": tol, "position": tol / 10, "scale": tol / 50})
        self.ents, self.ctrl = cached_discover(jar)
        self.modid = self.ents["mod_id"]; self.ns = clean(self.modid)
        self.title = name or self.modid.replace("_", " ").title()
        self.rp = self.out / f"GB {self.title} RP"; self.bp = self.out / f"GB {self.title} BP"
        self.report = {"jar": Path(jar).name, "mod_id": self.modid, "entities": {}, "molang_errors": [], "warnings": [],
                       "baked_segments": {}, "bake_error_max": {"rotation": 0, "position": 0, "scale": 0}}
        self.geo_ids, self.anim_ids, self.anim_meta, self.rcs, self.acs, self.sound_defs = {}, {}, {}, {}, {}, {}
        self.files_written = 0
        # M2: Java-side state -> entity properties + Script API driver; triggered animations
        self.rinfo = {**C.RESERVED_INFO, **self.ents.get("reserved_info", {})}
        self.trigs = cached_triggers(jar)
        ov = HERE / "mods" / f"{self.modid}.json"
        self.override = json.loads(ov.read_text()) if ov.exists() else {}
        self.js = {"ns": self.ns, "entities": {}}; self.climbers = set()
        self.mob_sounds = cached_sounds(jar, sorted({e["java_class"] for e in self.ents["entities"]}))
        self.report["sounds_vanilla_default"] = self.mob_sounds.get("_unmapped", [])
        ovn = self.override.get("overlay") or (self.modid if (OVERLAYS / self.modid / "overlay.json").exists() else None)
        self.ov_dir = OVERLAYS / ovn if ovn else None
        self.rename = self.override.get("rename", {})

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
            for b in rig.bones:
                b.name = bone_id(b.name)
                if b.parent: b.parent = bone_id(b.parent)
            rig.identifier = f"geometry.{GB}.{self.ns}.{stem}" + (f"_{i}" if i else ""); ids.append(rig.identifier)
        self.write(self.rp, f"models/entity/{self.ns}/{stem}.geo.json", write_bedrock_geo(rigs))
        self.geo_ids[path] = ids[0]
        return ids[0]

    def dedupe_locators(self, gids, er):
        """Bedrock merges locators across an entity's geometries and errors when the same name differs
        ("model already has a locator ... that doesn't exactly match"): keep the first, rename later copies."""
        seen = {}
        for gid in gids:
            path = self.geo_file(gid)
            if not path: continue
            d = json.loads(path.read_text()); changed = False
            for g in d["minecraft:geometry"]:
                if g["description"]["identifier"] != gid: continue
                for b in g.get("bones", []):
                    for name in list((b.get("locators") or {})):
                        if name in seen and seen[name] != b["locators"][name]:
                            b["locators"][f"{name}_{gid.split('.')[-1]}"] = b["locators"].pop(name); changed = True
                        else: seen.setdefault(name, b["locators"][name])
            if changed: path.write_text(json.dumps(d)); er.setdefault("locators_renamed", []).append(gid)

    # ---------------------------------------------------------------- animations
    def animations(self, path):
        """Convert one GeckoLib animation file -> {source name: bedrock id}; written once per file."""
        if path in self.anim_ids: return self.anim_ids[path]
        if path not in self.names: self.report["warnings"].append(f"animation file missing: {path}"); self.anim_ids[path] = {}; return {}
        stem = clean(Path(path).name.split(".")[0])
        src = jload(self.z.read(path)).get("animations", {})
        out, ids, rep = {}, {}, {}
        for name, a in src.items():
            if isinstance(a.get("bones"), dict):
                a = {**a, "bones": {bone_id(k): v for k, v in a["bones"].items()}}
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
        if not cand:   # registry name "bear_hurt" for sounds.json event "entity.bear.hurt"
            cand = sorted((k for k in sj if k.replace(".", "_").endswith("_" + key.replace(".", "_"))), key=len)
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
                self.report["warnings"].append(f"{getattr(self, 'cur', '?')}: animation '{name}' not found in its animation file"); continue
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


    # ---------------------------------------------------------------- M2: Java state + triggers
    def resolve(self, var, e):
        """Reserved v.gb_* -> ("const", "0"/"1") | ("query", molang) | ("prop", driver)."""
        ov = (self.override.get("vars") or {}).get(var) or (self.override.get("entities", {}).get(e["id"], {}).get("vars") or {}).get(var)
        info = self.rinfo.get(var) or {"java": next((k for k, v in (e.get("reserved") or {}).items() if v == var), var), "consts": [], "fields": []}
        java = info.get("java", ""); consts = info.get("consts") or []; fields = info.get("fields") or []
        strs = [c for c in consts if isinstance(c, str)]
        if ov:
            if "molang" in ov: return ("query", ov["molang"])
            if "const" in ov: return ("const", str(ov["const"]))
            return ("prop", ov)
        if "animationprocedure" in java and "empty" in strs:      # MCreator: no procedure animation playing
            return ("query", "(q.property('gb:trig') == 0)") if getattr(self, "has_trig", False) else ("const", "1")
        if java.split(".")[-1] in ("m_46467_", "getDayTime"): return ("query", "(q.time_of_day * 24000)")
        if ("m_6095_" in java or "getType" in java) and fields:            # getType() == SOME_ENTITY_TYPE: fixed per entity
            ids = [self.ents.get("type_fields", {}).get(f) for f in fields]
            if any(ids): return ("const", "1" if e["id"] in ids else "0")
        if java.split(".")[-1] in ("m_29443_",): return ("query", "(!q.is_on_ground)")             # Parrot-style isFlying
        if java.split(".")[-1] in ("isClimbing", "m_6147_", "onClimbable"):
            self.climbers.add(e["id"]); return ("query", "q.is_wall_climbing")                        # + BP climb navigation
        if java.split(".")[-1] in ("isGlowing",): return ("prop", {"kind": "night"})
        if java.split(".")[-1] in ("m_21660_", "isAngry"): return ("prop", {"kind": "has_target", "dist": 16})
        if "m_150930_" in java: return ("prop", {"kind": "random_spawn", "p": 0.5})                    # held item (berries/honey)
        if "m_7755_" in java or "getName" in java and "getVariant" not in java or "m_8077_" in java:
            if strs:
                k = "name_contains" if "contains" in java else "name_equals"
                cs = info.get("case_sensitive") or (java.endswith(".equals") and "IgnoreCase" not in java)
                return ("prop", {"kind": k, "value": strs[0] if cs else strs[0].lower(), "case": bool(cs)})
        if "getVariant" in java and strs: return ("prop", {"kind": "one_of", "group": "variant_name", "value": strs[0]})
        if java.endswith("_present"): return ("prop", {"kind": "random_spawn", "p": 0.5})
        if "sheared" in var: return ("prop", {"kind": "bp_sheared"})          # JSON: shears interaction + regrow timer
        d = runtime.classify(var, info)
        if d["kind"] == "off": self.report.setdefault("m2_off", {})[var] = java
        return ("prop", d)

    def m2_entity(self, e, eid, full, cnames, rcs, desc, er, amap, ctrl_entity):
        """Replace reserved vars in this entity's controllers/render controllers; add trigger states; collect properties."""
        # MCreator: procedures call setAnimation("name") -> the entity's procedure controller plays it once
        mc = [{"name": t["anim"], "controller": e.get("mcreator") or "procedure", "chain": [["thenPlay", t["anim"]]], "role": t["role"]}
              for t in self.trigs if t.get("mcreator") and t.get("owner") == e["java_class"]]
        trig_src = list(ctrl_entity.get("triggerable") or []) + mc
        self.has_trig = bool(trig_src)
        txt = json.dumps([self.acs[c] for c in cnames] + [self.rcs[r] for r in rcs])
        # the entity overrides isSleeping (Bear sleeps at night): GeckoLib layers/variants calling it get the same driver
        for a, b in (self.override.get("replace") or {}).items(): txt = txt.replace(a, b)
        sleepvar = next((v for v, i in self.rinfo.items() if i.get("java") in ("m_5803_", "isSleeping") and v in txt), None)
        if sleepvar: txt = txt.replace("q.is_sleeping", sleepvar)
        vars_ = sorted(set(re.findall(r"\bv\.gb_[a-z0-9_]+", txt)) - {OWNER_VAR})
        props, script_props, onegroups = {}, {}, {}
        for v in vars_:
            kind, val = self.resolve(v, e)
            if kind == "const": rep = val
            elif kind == "query": rep = val
            else:
                pn = runtime.prop_name(v); props[pn] = val; rep = f"q.property('{pn}')"
                if val["kind"] == "one_of": onegroups.setdefault(val["group"], []).append(pn)
                elif val["kind"] not in ("random_spawn", "bp_sheared"): script_props[pn] = val
            txt = re.sub(re.escape(v) + r"\b", rep, txt)
            er.setdefault("m2", {})[v] = {"java": (self.rinfo.get(v) or {}).get("java"), kind: val}
        parts = json.loads(txt)
        for c, d in zip(cnames, parts[:len(cnames)]): self.acs[c] = d
        for r, d in zip(rcs, parts[len(cnames):]): self.rcs[r] = d
        # triggered animations -> int property gb:trig; states injected into the named GeckoLib controller
        trig = []
        roles = {t["anim"]: t["role"] for t in self.trigs if t.get("owner") == e["java_class"]}
        for t in trig_src:
            c = {"name": t.get("controller") or "triggerable"}
            if not any(x["anim"] == t["name"] for x in trig):
                steps = [(n, l) for n, l in self.chain_states(t["chain"], amap) if n]
                if not steps: continue
                role = t.get("role") or roles.get(t["name"]) or ("melee" if re.search(r"attack|bite|swing|slam|hit", t["name"], re.I) else
                                               "spawn" if re.search(r"spawn|emerge|birth|summon_in|rise", t["name"], re.I) else
                                               "death" if "death" in t["name"].lower() or "die" in t["name"].lower() else
                                               "hurt" if re.search(r"hurt|damage|flinch", t["name"], re.I) else "ability")
                trig.append({"index": len(trig) + 1, "anim": t["name"], "role": role, "controller": c["name"], "steps": steps,
                             "length": sum((self.anim_meta.get(n, {}).get("length") or 1) for n, _ in steps)})
        if trig:
            props["gb:trig"] = {"kind": "trigger", "max": len(trig)}
            by_ctrl = {}
            for t in trig: by_ctrl.setdefault(t["controller"], []).append(t)
            for cn_name, ts in by_ctrl.items():
                cn = next((x for x in cnames if x.endswith("." + (clean(cn_name) or "x"))), None)
                if cn is None:   # controller has no normal paths: its own Bedrock controller with an idle state
                    cn = f"controller.animation.{GB}.{self.ns}.{eid}.{clean(cn_name) or 'trig'}"
                    self.acs[cn] = {"initial_state": "default", "states": {"default": {}}}
                    key = f"ctrl_{len(cnames)}"; desc.setdefault("animations", {})[key] = cn; desc["scripts"].setdefault("animate", []).append(key)
                    cnames.append(cn)
                ac = self.acs[cn]; init = ac["initial_state"]
                for t in ts:
                    cond = f"q.property('gb:trig') == {t['index']}"
                    for sn, st in ac["states"].items():
                        if sn.startswith("trig_"): continue
                        st.setdefault("transitions", []).insert(0, {f"trig_{t['index']}": cond})
                    for j, (nid, loop) in enumerate(t["steps"]):
                        vid = self.variant_anim(nid, loop=loop); short = self.short_of(vid)
                        desc.setdefault("animations", {})[short] = vid
                        sn = f"trig_{t['index']}" + (f"__{j}" if j else "")
                        st = {"animations": [short], "transitions": [{init: f"q.property('gb:trig') != {t['index']}"}]}
                        if j + 1 < len(t["steps"]): st["transitions"].append({f"trig_{t['index']}__{j + 1}": "q.all_animations_finished"})
                        ac["states"][sn] = st
            er["triggers"] = [{k: t[k] for k in ("index", "anim", "role", "controller")} for t in trig]
        if "gb:trig" not in props:   # MCreator 'no procedure animation' test with nothing to trigger: always true
            for c in cnames + rcs:
                tbl = self.acs if c in self.acs else self.rcs
                t = json.dumps(tbl[c]).replace("(q.property('gb:trig') == 0)", "1").replace("q.property('gb:trig')", "0")
                tbl[c] = json.loads(t)
        if props:
            self.js["entities"][full] = {"props": script_props, "range": (e.get("attributes") or {}).get("follow_range", 16),
                                         "triggers": [{k: t[k] for k in ("index", "anim", "role", "length")} for t in trig]}
            hk = (self.override.get("hook_anims") or {}).get(eid)
            if hk: self.js["entities"][full]["hooks"] = hk
            if eid in (self.override.get("ability_hooked") or []): self.js["entities"][full]["hooked"] = True
        return props, onegroups

    # ---------------------------------------------------------------- per entity
    def entity(self, e, ctrl_entity):
        eid = clean(e["id"]); full = f"{self.ns}:{eid}"; self.cur = eid
        er = self.report["entities"].setdefault(eid, {"java": e["class"]})
        # geometry (+ baby/other models chosen in getModelResource)
        geos = [{"when": "1", "path": e["geo"]}] if not e.get("geo_variants") else e["geo_variants"]
        geo_map = {}
        for i, g in enumerate(geos):
            gid = self.geometry(g["path"]) if g.get("path") else None
            if gid: geo_map[f"g{i}"] = (gid, g["when"])
        if len(geo_map) > 1: self.dedupe_locators([v[0] for v in geo_map.values()], er)
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
        reserved = set(e.get("reserved", {}).values())       # Java-side conditions: resolved in m2_entity()
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
        layer_rows = []
        for L in e.get("layers", []):
            if L.get("kind") != "custom": continue
            for r in L.get("rows") or []:
                if r["texture"] not in self.names: continue
                emis = L.get("emissive") or any(w in r["texture"].lower() for w in ("eye", "glow", "emissive"))
                if emis and r["when"] == "1": glows.append(r["texture"])
                else: layer_rows.append({**r, "emissive": emis, "layer": L["class"].split(".")[-1]})
            if not L.get("rows"): self.report["warnings"].append(f"{full}: custom layer {L['class'].split('.')[-1]} draws nothing we could find")
        for i, g in enumerate(dict.fromkeys(glows)):
            key = f"textures/entity/{GB}/{self.ns}/{clean(Path(g).stem)}"
            self.write(self.rp, key + ".png", self.z.read(g))
            desc["textures"][f"glow{i}"] = key; desc["materials"]["glow"] = "gb_glow"
            grc = f"controller.render.{GB}.{self.ns}.{eid}.glow{i}"
            self.rcs[grc] = {"geometry": rcd["geometry"], "materials": [{"*": "Material.glow"}], "textures": [f"Texture.glow{i}"],
                             "ignore_lighting": True, "part_visibility": [{"*": f"{OWNER_VAR} == 0"}]}
            desc["render_controllers"].append(grc); er["glow"] = er.get("glow", 0) + 1
        # custom GeoRenderLayers (GeckoLib reRender with another texture): one overlay render controller each, shown by the
        # layer's own condition (sheared bear, sleeping snake eyes, TMNT tortoise skins by name)
        for i, r in enumerate(layer_rows):
            key = f"textures/entity/{GB}/{self.ns}/{clean(Path(r['texture']).stem)}"
            self.write(self.rp, key + ".png", self.z.read(r["texture"]))
            desc["textures"][f"layer{i}"] = key
            if r["emissive"]: desc["materials"]["glow"] = "gb_glow"
            lrc = f"controller.render.{GB}.{self.ns}.{eid}.layer{i}"
            self.rcs[lrc] = {"geometry": rcd["geometry"], "materials": [{"*": "Material.glow" if r["emissive"] else "Material.default"}],
                             "textures": [f"Texture.layer{i}"], "part_visibility": [{"*": f"({r['when']}) && {OWNER_VAR} == 0"}],
                             **({"ignore_lighting": True} if r["emissive"] else {})}
            desc["render_controllers"].append(lrc); er.setdefault("layers", []).append({"layer": r["layer"], "when": r["when"], "texture": key})
        # animations + controllers
        amap = self.animations(e["animation_file"]) if e.get("animation_file") else {}
        cnames = []
        for ci, c in enumerate(ctrl_entity.get("controllers", [])):
            if any("animationprocedure" in json.dumps(p.get("anims")) for p in c.get("paths", [])):
                e["mcreator"] = c["name"]; continue        # MCreator procedure controller: becomes trigger states (m2_entity)
            cn = self.controller(eid, ci, c, amap, {})
            if cn: cnames.append(cn)
        desc.setdefault("animations", {})
        if e["gb_kind"] == "projectile": self.projectile_face(next(iter(geo_map.values()))[0], desc)
        if e["gb_kind"] == "block": er["block"] = self.block_for(e, eid, next(iter(geo_map.values()))[0], e["variants"][0]["texture"])
        my_rcs = [r for r in desc["render_controllers"]]
        props, onegroups = self.m2_entity(e, eid, full, cnames, my_rcs, desc, er, amap, ctrl_entity)
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
        anims.update({k: v for k, v in desc.get("animations", {}).items() if k not in anims})
        desc["animations"] = anims
        used_vars = set()
        for nid in anims.values():
            stem = nid.split(".")[3] if nid.startswith("animation.") else None
            a = self.anim_files.get(stem, {}).get(nid) if stem else None
            if a: used_vars |= {("v." + m.split(".", 1)[1]).lower() for m in re.findall(r"\b(?:v|variable)\.[a-z_][a-z0-9_]*", json.dumps(a), re.I)}
        for v in sorted(used_vars - set(reserved) - {OWNER_VAR}):
            desc["scripts"]["pre_animation"].append(f"{v} = {v} ?? 0;")
        if used_vars: er["anim_vars_initialised"] = sorted(used_vars)
        if not anims: desc.pop("animations")      # Bedrock: "Node has too few children (0 < 1)"
        if getattr(self, "entity_sounds", None): desc["sound_effects"] = dict(self.entity_sounds)   # short name -> sound id (string)
        evs = {}
        for kind, ev in (self.mob_sounds.get(e["java_class"]) or {}).items():
            short = self.sound(ev)
            if short: evs[kind] = self.entity_sounds[short]
        if evs: self.ent_sounds[full] = {"volume": 1.0, "pitch": [0.9, 1.1], "events": evs}; er["sounds"] = evs
        if not desc["scripts"].get("animate"): desc["scripts"].pop("animate", None)
        if e["gb_kind"] != "mob": desc.pop("spawn_egg", None)
        er.update({"geometry": [v[0] for v in geo_map.values()], "textures": len(tex_map), "controllers": len(cnames),
                   "animations_used": len(anims) - len(cnames)})
        self.built.add(eid)
        self.write(self.rp, f"entity/{self.ns}/{eid}.entity.json", {"format_version": "1.10.0", "minecraft:client_entity": {"description": desc}})
        self.behaviour(e, eid, full, any("q.variant" in v[1] for v in tex_map.values()), len(tex_map), props, onegroups)
        nice = e.get("name") or " ".join(re.findall(r"[A-Z][a-z0-9]*", re.sub(r"(Attack)?Entity$", "", e["class"]))) or e["class"]
        self.lang += [f"entity.{full}.name={nice}", f"item.spawn_egg.entity.{full}.name=Spawn {nice} (GeckoBridge)"]

    def behaviour(self, e, eid, full, uses_variant, ntex, props=None, onegroups=None):
        at = e.get("attributes") or {}; w, h = (e.get("size") or [0.6, 1.8])
        hostile = e.get("kind") == "hostile"
        if e["gb_kind"] == "projectile":
            comps = {"minecraft:type_family": {"family": ["geckobridge", self.ns, "projectile"]},
                     "minecraft:collision_box": {"width": min(w, 1.0), "height": min(h, 1.0)},
                     "minecraft:projectile": {"on_hit": {"impact_damage": {"damage": at.get("attack_damage", 4), "knockback": True, "destroy_on_hit": True},
                                                         "remove_on_hit": {}}, "power": 1.2, "gravity": 0.0, "uncertainty_base": 1, "anchor": 1,
                                              "offset": [0, -0.1, 0], "inertia": 1, "liquid_inertia": 1, "hit_sound": "bow.hit"},
                     "minecraft:physics": {}, "minecraft:pushable": {"is_pushable": False, "is_pushable_by_piston": True},
                     "minecraft:conditional_bandwidth_optimization": {"default_values": {"max_optimized_distance": 80, "max_dropped_ticks": 7, "use_motion_prediction_hints": True}}}
            self.write(self.bp, f"entities/{self.ns}/{eid}.json", {"format_version": "1.21.0", "minecraft:entity": {
                "description": {"identifier": full, "is_spawnable": False, "is_summonable": True, "is_experimental": False,
                                **({"properties": self.prop_defs(props)} if props else {})}, "components": comps}})
            return
        if e["gb_kind"] == "block":
            comps = {"minecraft:type_family": {"family": ["geckobridge", self.ns, "gb_block_companion", "inanimate"]},
                     "minecraft:health": {"value": 1, "max": 1}, "minecraft:collision_box": {"width": 0.01, "height": 0.01},
                     "minecraft:physics": {"has_gravity": False, "has_collision": False},
                     "minecraft:pushable": {"is_pushable": False, "is_pushable_by_piston": False},
                     "minecraft:damage_sensor": {"triggers": {"cause": "all", "deals_damage": "no"}},
                     "minecraft:knockback_resistance": {"value": 1}, "minecraft:persistent": {}, "minecraft:fire_immune": {}}
            ent = {"description": {"identifier": full, "is_spawnable": False, "is_summonable": True, "is_experimental": False,
                                   **({"properties": self.prop_defs(props)} if props else {})}, "components": comps}
            self.write(self.bp, f"entities/{self.ns}/{eid}.json", {"format_version": "1.21.0", "minecraft:entity": ent})
            return
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
            "minecraft:persistent": {}, "minecraft:nameable": {},
        }
        if at.get("attack_damage"):
            comps["minecraft:attack"] = {"damage": at["attack_damage"]}
            comps["minecraft:behavior.melee_attack"] = {"priority": 2, "speed_multiplier": 1.25, "track_target": True}
        if hostile:
            comps["minecraft:behavior.nearest_attackable_target"] = {"priority": 2, "must_see": True, "reselect_targets": True,
                "entity_types": [{"filters": {"test": "is_family", "subject": "other", "value": "player"}, "max_dist": at.get("follow_range", 16)}]}
        goals = self.goals_of(e) if e["gb_kind"] == "mob" else None
        if goals:   # the mod's own AI goal list replaces the generic behaviours
            for k in [k for k in comps if k.startswith("minecraft:behavior.")]: comps.pop(k)
            comps.update(goals)
            if "minecraft:behavior.melee_attack" in goals and "minecraft:attack" not in comps: comps["minecraft:attack"] = {"damage": at.get("attack_damage", 3)}
            self.report["entities"][eid]["goals"] = sorted(goals)
        mv = e.get("movement", "walk")
        if e["id"] in self.climbers and mv == "walk":     # the mod's isClimbing (snails, snakes): wall climbing like spiders
            comps.pop("minecraft:navigation.walk", None); comps["minecraft:navigation.climb"] = {"can_path_over_water": True, "avoid_damage_blocks": True}
            comps["minecraft:can_climb"] = {}
        if mv == "fly":
            for k in ("minecraft:navigation.walk", "minecraft:movement.basic", "minecraft:jump.static", "minecraft:behavior.random_stroll"): comps.pop(k, None)
            comps.update({"minecraft:can_fly": {}, "minecraft:flying_speed": {"value": 0.1}, "minecraft:physics": {"has_gravity": False},
                "minecraft:movement.hover": {}, "minecraft:navigation.hover": {"can_path_over_water": True, "can_sink": False, "can_path_from_air": True},
                "minecraft:behavior.random_hover": {"priority": 6, "xz_dist": 8, "y_dist": 4, "y_offset": -1, "interval": 1, "hover_height": [1, 4]}})
        elif mv == "swim":
            for k in ("minecraft:navigation.walk", "minecraft:movement.basic", "minecraft:jump.static", "minecraft:behavior.random_stroll", "minecraft:behavior.float"): comps.pop(k, None)
            comps.update({"minecraft:breathable": {"breathes_water": True, "breathes_air": False, "suffocate_time": 0},
                "minecraft:underwater_movement": {"value": 0.08},
                "minecraft:navigation.generic": {"can_path_over_water": False, "can_swim": True, "can_walk": False, "can_breach": False},
                "minecraft:movement.sway": {"sway_amplitude": 0}, "minecraft:behavior.swim_wander": {"priority": 6, "speed_multiplier": 1.0, "look_ahead": 2, "wander_time": 5}})
        ent = {"description": {"identifier": full, "is_spawnable": True, "is_summonable": True, "is_experimental": False}, "components": comps}
        props = props or {}; seq = []
        if uses_variant:
            ent["component_groups"] = {f"gb_variant_{i}": {"minecraft:variant": {"value": i}} for i in range(ntex)}
            seq.append({"randomize": [{"weight": 1, "add": {"component_groups": [f"gb_variant_{i}"]}} for i in range(ntex)]})
        if props:   # Java-side state (M2): client-synced entity properties, read by the client with q.property()
            ent["description"]["properties"] = self.prop_defs(props)
        for p, d in props.items():     # chosen once at spawn, pure JSON (no script)
            if d.get("kind") == "random_spawn":
                w = max(1, min(99, round(d.get("p", 0.5) * 100)))
                seq.append({"randomize": [{"weight": w, "set_property": {p: True}}, {"weight": 100 - w, "set_property": {p: False}}]})
        for g, ps in (onegroups or {}).items():
            seq.append({"randomize": [{"weight": 1, "set_property": {p: True}} for p in ps]})
        # sheared state (shears -> layer shown, wool/fur regrows) for *sheared* properties: pure JSON
        sheared = [p for p, d in props.items() if "sheared" in p]
        if sheared:
            p = sheared[0]
            comps["minecraft:interact"] = {"interactions": [{"on_interact": {"filters": {"all_of": [
                {"test": "has_equipment", "subject": "other", "domain": "hand", "value": "shears"},
                {"test": "bool_property", "domain": p, "operator": "!=", "value": True}]}, "event": "gb:sheared", "target": "self"},
                "use_item": False, "hurt_item": 1, "play_sounds": "shear", "interact_text": "action.interact.shear"}]}
            ent.setdefault("component_groups", {})["gb_regrow"] = {"minecraft:timer": {"looping": False, "time": [240, 480],
                                                                                     "time_down_event": {"event": "gb:regrow"}}}
            ent.setdefault("events", {})["gb:sheared"] = {"set_property": {p: True}, "add": {"component_groups": ["gb_regrow"]}}
            ent["events"]["gb:regrow"] = {"set_property": {p: False}, "remove": {"component_groups": ["gb_regrow"]}}
        if seq: ent.setdefault("events", {})["minecraft:entity_spawned"] = seq[0] if len(seq) == 1 else {"sequence": seq}
        self.write(self.bp, f"entities/{self.ns}/{eid}.json", {"format_version": "1.21.0", "minecraft:entity": ent})

    # ---------------------------------------------------------------- M3: assets by name, projectiles, block entities
    def fill_assets(self, e):
        """GeckoLib's DefaultedEntityGeoModel / DefaultedBlockGeoModel build paths from the id at runtime: try those."""
        stems, s_ = [e["id"]], e["id"]
        for suf in ("_block", "_attack", "_entity", "_projectile", "_attack"):
            if s_.endswith(suf): s_ = s_[: -len(suf)]; stems.append(s_)
        dirs = ("block/", "entity/", "") if e["gb_kind"] == "block" else ("entity/", "")
        def find(kind, ext):
            for st in stems:
                for d in dirs:
                    n = f"assets/{self.modid}/{kind}/{d}{st}{ext}"
                    if n in self.names: return n
            return None
        if not e.get("geo"): e["geo"] = find("geo", ".geo.json"); e.setdefault("assets_by_name", []).append("geo")
        if not e.get("animation_file"): e["animation_file"] = find("animations", ".animation.json")
        if not e.get("variants"):
            t = find("textures", ".png")
            if t: e["variants"] = [{"when": "1", "texture": t}]

    def aliases(self):
        """Overlay entities (our own AI's projectiles) shown with a converted GeckoLib model: mods/<modid>.json visual_alias."""
        for alias, target in (self.override.get("visual_alias") or {}).items():
            src = self.rp / f"entity/{self.ns}/{target}.entity.json"
            if not src.exists(): self.report["warnings"].append(f"visual_alias {alias}: {target} not built"); continue
            d = json.loads(src.read_text()); d["minecraft:client_entity"]["description"]["identifier"] = f"{self.ns}:{alias}"
            d["minecraft:client_entity"]["description"].pop("spawn_egg", None)
            self.write(self.rp, f"entity/{self.ns}/{alias}.entity.json", d); self.built.add(alias)
            self.report.setdefault("visual_alias", {})[alias] = target

    def geo_file(self, gid):
        return next((p for p in (self.rp / "models").rglob("*.json") if f'"{gid}"' in p.read_text()), None)

    def projectile_face(self, gid, desc):
        """Projectiles: wrap the model in a gb_root bone turned to the flight direction (vanilla arrow-style)."""
        path = self.geo_file(gid); d = json.loads(path.read_text())
        for g in d["minecraft:geometry"]:
            if g["description"]["identifier"] != gid or any(b["name"] == "gb_root" for b in g.get("bones", [])): continue
            for b in g.get("bones", []):
                if not b.get("parent"): b["parent"] = "gb_root"
            g["bones"].insert(0, {"name": "gb_root", "pivot": [0, 0, 0]})
        path.write_text(json.dumps(d))
        nid = f"animation.{GB}.{self.ns}.gb_projectile.face"
        self.anim_files = getattr(self, "anim_files", {})
        self.anim_files.setdefault("gb_projectile", {})[nid] = {"loop": True, "bones": {"gb_root": {"rotation": ["q.target_x_rotation", "-q.target_y_rotation", 0]}}}
        desc["animations"]["gb_face"] = nid; desc["scripts"].setdefault("animate", []).append("gb_face")

    def block_for(self, e, eid, gid, tex_path):
        """GeckoLib block entity -> Bedrock block with the same (static) geometry + an animated companion entity spawned by
        the runtime when a player places the block (Bedrock blocks can't play animations); the block hides its bones
        while the companion is there (state gb:has_entity)."""
        bid = re.sub(r"_block$", "", eid); full_b = f"{self.ns}:{bid}"
        src = json.loads(self.geo_file(gid).read_text())
        g = next(x for x in src["minecraft:geometry"] if x["description"]["identifier"] == gid)
        bg = json.loads(json.dumps(g)); bgid = f"geometry.{GB}.{self.ns}.block.{bid}"; bg["description"]["identifier"] = bgid
        for k in ("visible_bounds_width", "visible_bounds_height", "visible_bounds_offset"): bg["description"].pop(k, None)
        self.write(self.rp, f"models/blocks/{self.ns}/{bid}.geo.json", {"format_version": "1.12.0", "minecraft:geometry": [bg]})
        tkey = f"gb_{self.ns}_{bid}"; tpath = f"textures/blocks/{GB}/{self.ns}/{bid}"
        self.write(self.rp, tpath + ".png", self.z.read(tex_path)); self.terrain[tkey] = {"textures": tpath}
        tops = [b["name"].lower() for b in bg.get("bones", [])]   # Bedrock matches these lower-cased
        box = {"origin": [-8, 0, -8], "size": [16, 16, 16]}
        self.write(self.bp, f"blocks/{self.ns}/{bid}.json", {"format_version": "1.21.40", "minecraft:block": {
            "description": {"identifier": full_b, "menu_category": {"category": "construction"}, "states": {"gb:has_entity": [False, True]}},
            "components": {"minecraft:geometry": bgid, "minecraft:material_instances": {"*": {"texture": tkey, "render_method": "alpha_test"}},
                           "minecraft:collision_box": box, "minecraft:selection_box": box, "minecraft:light_dampening": 0,
                           "minecraft:destructible_by_mining": {"seconds_to_destroy": 1.5}, "minecraft:destructible_by_explosion": {"explosion_resistance": 6}},
            "permutations": [{"condition": "q.block_state('gb:has_entity')",
                              "components": {"minecraft:geometry": {"identifier": bgid, "bone_visibility": {b: False for b in tops}}}}]}})
        self.js.setdefault("blocks", {})[full_b] = f"{self.ns}:{eid}"
        nice = (e.get("name") or bid.replace("_", " ").title())
        self.lang.append(f"tile.{full_b}.name={nice}")
        return full_b

    # ---------------------------------------------------------------- ModPorter content: blocks, items, recipes, loot
    def content(self):
        sys.path.insert(0, str(HERE.parent / "ModPorter"))
        import content as CT
        ci = cached_classinfo(self.jar)
        skip = set()
        if self.ov_dir and (self.ov_dir / "bp").exists():     # ids the behaviour overlay defines itself
            for f in (self.ov_dir / "bp").rglob("*.json"):
                skip.add(f.stem)
        ct = CT.Content(self.z, self.modid, self.ns, self.rp, self.bp, self.report, self.title)
        ct.skip = skip
        lang = ct.run(item_info=ci["items"], block_props=ci["blocks"])
        self.lang += [f"{k}={v}" for k, v in lang.items()]
        self.content_ids = {"blocks": ct.blocks, "items": ct.items}

    def worldgen(self):
        """Structure templates -> .mcstructure; dimensions -> custom dimensions + generator script; portals."""
        sys.path.insert(0, str(HERE.parent / "ModPorter"))
        nbts = sorted(n for n in self.names if n.startswith(f"data/{self.modid}/structures/") and n.endswith(".nbt"))
        if not nbts: return
        import dimensions as DM
        from concurrent.futures import ProcessPoolExecutor
        custom = {k: v for k, v in (getattr(self, "content_ids", {}).get("blocks") or {}).items()}
        jobs = [(self.jar, n, self.modid, self.ns, custom) for n in nbts]
        sids, fails = {}, []
        with ProcessPoolExecutor(max_workers=8) as ex:
            for n, data, err in ex.map(_convert_structure, jobs, chunksize=8):
                rel = n.split(f"data/{self.modid}/structures/", 1)[1][:-4]
                if err: fails.append(f"{rel}: {err}"); continue
                name = clean(rel.replace("/", "_"))
                self.write(self.bp, f"structures/{self.ns}/{name}.mcstructure", data)
                sids[f"{self.modid}:{rel}"] = f"{self.ns}:{name}"
        self.report["structures"] = {"converted": len(sids), "failed": fails[:50]}
        dm = DM.Dimensions(self.z, self.modid, self.ns, self.bp, self.report, sids)
        dims = dm.run()
        if not dims: return
        portals = {}
        for bid in (getattr(self, "content_ids", {}).get("blocks") or {}):
            if bid.endswith("_portal"):
                base = bid[:-7]
                tgt = next((d for d in dims if d.split(":")[1] in (base, base + "_dimension", base.replace("_dimension", ""))), None)
                if tgt: portals[f"{self.ns}:{bid}"] = tgt
        self.write(self.bp, "scripts/worldgen.js", DM.script({"ns": self.ns, "dims": dims, "portals": portals}))
        for rel, fog in DM.fogs(dims, self.ns).items(): self.write(self.rp, rel, fog)
        self.report["dimensions"]["portals"] = len(portals)
        self.worldgen_script = True

    def javabridge(self):
        """The mod's own Java code (MCreator procedures, entity/block/item/effect hooks, event subscribers) translated
        to Script API JavaScript by tools/JavaBridge."""
        sys.path.insert(0, str(HERE.parent / "JavaBridge"))
        import bridge as JB
        emap = {e["java_class"]: f"{self.ns}:{clean(e['id'])}" for e in self.ents["entities"]}
        sids = {}
        for p in (self.bp / "structures").rglob("*.mcstructure"):
            sids[f"{self.modid}:{p.stem}"] = f"{self.ns}:{p.stem}"
        rep = JB.build_bridge(self.jar, self.modid, self.ns, self.bp, emap, self.report, structures=sids)
        ci = cached_classinfo(self.jar)
        cfgp = self.bp / "scripts/javabridge/config.js"
        txt = cfgp.read_text()
        cfg = json.loads(txt[txt.index("=") + 1:].rstrip().rstrip(";"))
        cfg["blockClasses"] = {f"{self.ns}:{k}": v.replace(".", "/") for k, v in ci["registries"]["block"].items()}
        cfg["itemClasses"] = {f"{self.ns}:{k}": v.replace(".", "/") for k, v in ci["registries"]["item"].items()}
        cfg["effectClasses"] = {f"{self.ns}:{k}": v.replace(".", "/") for k, v in ci["registries"].get("effect", {}).items()}
        cfgp.write_text("globalThis.__JB_CFG = " + json.dumps(cfg, separators=(",", ":")) + ";\n")
        # procedures drive the triggered animations now: no automatic ability cycle
        for full, c in self.js["entities"].items(): c["hooked"] = True
        self.javabridge_on = True

    # ---------------------------------------------------------------- ModPorter behaviour overlay
    def rn(self, text):
        for a, b in self.rename.items(): text = text.replace(a, b)
        return text

    def apply_overlay(self):
        """Merge a ModPorter behaviour overlay (our own Script API AI, entity overrides, blocks, sounds) with the
        GeckoBridge visuals. Overlay BP entities replace our generic AI; our client-synced properties are kept."""
        import shutil
        od = self.ov_dir; ov = json.loads(self.rn((od / "overlay.json").read_text()))
        self.report["overlay"] = od.name
        if (od / "scripts").exists():
            for f in (od / "scripts").rglob("*"):
                if not f.is_file(): continue
                if f.suffix == ".js":
                    txt = self.rn(f.read_text())
                    for pt in self.override.get("patch") or []:   # hooks so the overlay's abilities fire GeckoLib triggered animations
                        if pt["find"] in txt:
                            txt = txt.replace(pt["find"], pt.get("prepend", "") + pt["find"] + pt.get("append", ""), 1)
                            self.report.setdefault("overlay_patches", []).append(pt["find"][:40])
                    self.write(self.bp, f"scripts/overlay/{f.relative_to(od / 'scripts')}", txt)
                else: self.write(self.bp, f"scripts/overlay/{f.relative_to(od / 'scripts')}", f.read_bytes())
        # every mod sound event as <ns>.<event> (the overlay scripts play those), plus random groups
        sj = self.java_sounds()
        for ev in sj:
            short = self.sound(ev)
            if short: self.sound_defs[f"{self.ns}.{ev}"] = self.sound_defs[self.entity_sounds[short]]
        for g, evs in (ov.get("sound_groups") or {}).items():
            files = [f for ev in evs for f in self.sound_defs.get(f"{self.ns}.{ev}", {}).get("sounds", [])]
            if files: self.sound_defs[f"{self.ns}.{g}"] = {"category": "hostile", "sounds": files}
        if ov.get("entity_sounds"):
            for e in self.ents["entities"]:
                full = f"{self.ns}:{clean(e['id'])}"
                self.ent_sounds[full] = {"volume": 1.0, "pitch": [0.9, 1.1], "events": {k: f"{self.ns}.{v}" for k, v in ov["entity_sounds"].items()}}
        bp_ents = {p.stem: p for p in (self.bp / "entities").rglob("*.json")}
        # overlay bp/ files: entities we built get the overlay's behaviour + our properties; the rest is copied
        for side, root in (("bp", self.bp), ("rp", self.rp)):
            if not (od / side).exists(): continue
            for f in (od / side).rglob("*"):
                if not f.is_file(): continue
                rel = f.relative_to(od / side); rel_s = self.rn(str(rel)); stem = Path(rel_s).name.split(".")[0]
                if side == "rp" and rel.parts[0] in VISUAL_DIRS and stem in self.built: continue
                if side == "bp" and rel.parts[0] == "entities" and stem in bp_ents:
                    mine = json.loads(bp_ents[stem].read_text()); theirs = json.loads(self.rn(f.read_text()))
                    me, te = mine["minecraft:entity"], theirs["minecraft:entity"]
                    te["description"]["properties"] = {**me["description"].get("properties", {}), **te["description"].get("properties", {})}
                    for k in ("component_groups", "events"):
                        merged = {**me.get(k, {}), **te.get(k, {})}
                        if merged: te[k] = merged
                    if "minecraft:entity_spawned" in me.get("events", {}) and "minecraft:entity_spawned" in te.get("events", {}) and \
                            me["events"]["minecraft:entity_spawned"] != te["events"]["minecraft:entity_spawned"]:
                        te["events"]["minecraft:entity_spawned"] = {"sequence": [me["events"]["minecraft:entity_spawned"], theirs["minecraft:entity"]["events"]["minecraft:entity_spawned"]]}
                    for c in ("minecraft:interact", "minecraft:navigation.climb", "minecraft:can_climb"):
                        if c in me["components"] and c not in te["components"]: te["components"][c] = me["components"][c]
                    te["description"]["identifier"] = me["description"]["identifier"]
                    if te["description"]["properties"]:   # our runtime finds its mobs by family
                        fam = te["components"].setdefault("minecraft:type_family", {"family": []}).setdefault("family", [])
                        for need in ("geckobridge", self.ns):
                            if need not in fam: fam.append(need)
                    else: te["description"].pop("properties")
                    bp_ents[stem].write_text(json.dumps(theirs, indent=1)); self.report.setdefault("overlay_merged", []).append(stem)
                    continue
                data = self.rn(f.read_text()) if f.suffix in (".json", ".js", ".lang", ".mcfunction") else f.read_bytes()
                tgt = root / rel_s
                if rel_s in ("textures/terrain_texture.json", "textures/item_texture.json") and tgt.exists():   # merge, don't replace
                    mine, theirs = json.loads(tgt.read_text()), json.loads(data)
                    mine.setdefault("texture_data", {}).update(theirs.get("texture_data", {})); data = mine
                elif rel_s == "textures/flipbook_textures.json" and tgt.exists():
                    data = json.loads(tgt.read_text()) + json.loads(data)
                self.write(root, rel_s, data)
        for key, change in (ov.get("entity_overrides") or {}).items():
            base = key.split("#")[0]
            targets = [p for st, p in bp_ents.items() if key == "*" or (st.startswith(base[1:]) or base[1:] in st if base.startswith("*") else st == base)]
            for p in targets:
                d = json.loads(p.read_text()); c = d["minecraft:entity"]["components"]
                if p.stem in change.get("except", []) or (change.get("if_has") and change["if_has"] not in c): continue
                for k in change.get("remove", []): c.pop(k, None)
                c.update(change.get("set", {}))
                fam = c.get("minecraft:type_family", {}).get("family", [])
                for need in ("geckobridge", self.ns):        # our runtime finds its mobs by family
                    if need not in fam: fam.append(need)
                p.write_text(json.dumps(d, indent=1))
            if not targets: self.report["warnings"].append(f"overlay override '{key}' matched no entity")
        for it in ov.get("items", []):
            iid = f"{self.ns}:{it['id']}"
            comps = {"minecraft:icon": it["id"], "minecraft:display_name": {"value": it.get("name", it["id"])},
                     "minecraft:max_stack_size": 1 if it.get("durability") else it.get("max_stack", 64)}
            if it.get("durability"): comps["minecraft:durability"] = {"max_durability": it["durability"]}
            self.write(self.bp, f"items/{it['id']}.json", {"format_version": "1.21.0", "minecraft:item": {
                "description": {"identifier": iid, "menu_category": {"category": "equipment"}}, "components": comps}})
            if it.get("texture") in self.names:
                self.write(self.rp, f"textures/items/{self.ns}/{it['id']}.png", self.z.read(it["texture"]))
                self.item_tex[it["id"]] = {"textures": f"textures/items/{self.ns}/{it['id']}"}
            if it.get("recipe"):
                r = it["recipe"]
                self.write(self.bp, f"recipes/{it['id']}.json", {"format_version": "1.20.10", "minecraft:recipe_shaped": {
                    "description": {"identifier": f"{iid}_recipe"}, "tags": ["crafting_table"], "pattern": r["pattern"],
                    "key": {k: {"item": v} for k, v in r["key"].items()}, "unlock": [{"item": next(iter(r["key"].values()))}],
                    "result": {"item": iid, "count": 1}}})
            self.lang.append(f"item.{iid}={it.get('name', it['id'])}")
        for k, v in (ov.get("lang") or {}).items(): self.lang.append(f"{k}={v}")
        if ov.get("license_file") and (od / ov["license_file"]).exists():
            for root in (self.rp, self.bp): self.write(root, "LICENSE_original_mod.txt", (od / ov["license_file"]).read_bytes())
        self.ov_deps = ov.get("script_deps", [])

    GOALS = {   # Java AI goal -> Bedrock behaviour component (args: the goal's constructor constants in order)
        "MeleeAttackGoal": lambda a: ("minecraft:behavior.melee_attack", {"speed_multiplier": a[0] if a else 1.0, "track_target": True}),
        "NearestAttackableTargetGoal": None, "HurtByTargetGoal": lambda a: ("minecraft:behavior.hurt_by_target", {}),
        "RandomStrollGoal": lambda a: ("minecraft:behavior.random_stroll", {"speed_multiplier": a[0] if a else 1.0}),
        "WaterAvoidingRandomStrollGoal": lambda a: ("minecraft:behavior.random_stroll", {"speed_multiplier": a[0] if a else 1.0}),
        "LookAtPlayerGoal": lambda a: ("minecraft:behavior.look_at_player", {"look_distance": a[0] if a else 8}),
        "RandomLookAroundGoal": lambda a: ("minecraft:behavior.random_look_around", {}), "FloatGoal": lambda a: ("minecraft:behavior.float", {}),
        "PanicGoal": lambda a: ("minecraft:behavior.panic", {"speed_multiplier": a[0] if a else 1.25}),
        "LeapAtTargetGoal": lambda a: ("minecraft:behavior.leap_at_target", {"yd": a[0] if a else 0.4, "must_be_on_ground": True}),
        "MoveTowardsTargetGoal": lambda a: ("minecraft:behavior.move_towards_target", {"speed_multiplier": a[0] if a else 1.0, "within_radius": a[1] if len(a) > 1 else 16}),
        "FleeSunGoal": lambda a: ("minecraft:behavior.flee_sun", {"speed_multiplier": a[0] if a else 1.0}),
        "RestrictSunGoal": lambda a: ("minecraft:behavior.restrict_sun", {}),
        "OpenDoorGoal": lambda a: ("minecraft:behavior.open_door", {"close_door_after": True}),
        "BreakDoorGoal": lambda a: ("minecraft:behavior.break_door", {}),
        "RandomSwimmingGoal": lambda a: ("minecraft:behavior.swim_wander", {"speed_multiplier": a[0] if a else 1.0, "look_ahead": 2, "wander_time": 5}),
        "AvoidEntityGoal": lambda a: ("minecraft:behavior.avoid_mob_type", {"entity_types": [{"filters": {"test": "is_family", "subject": "other", "value": "player"}, "max_dist": a[0] if a else 6, "walk_speed_multiplier": a[1] if len(a) > 1 else 1, "sprint_speed_multiplier": a[2] if len(a) > 2 else 1.2}]}),
        "TemptGoal": lambda a: ("minecraft:behavior.tempt", {"speed_multiplier": a[0] if a else 1.0}),
        "BreedGoal": None, "FollowParentGoal": lambda a: ("minecraft:behavior.follow_parent", {"speed_multiplier": a[0] if a else 1.1}),
        "MoveBackToVillageGoal": None, "RandomStandGoal": None,
    }

    def goals_of(self, e):
        """registerGoals (m_8099_) of the entity class -> Bedrock behaviours (None: keep the generic AI)."""
        import tempfile
        sys.path.insert(0, str(HERE.parent / "ModPorter"))
        import extract as MP
        if not hasattr(self, "_gtmp"):
            self._gtmp = tempfile.mkdtemp(prefix="gb_goals_")
        cls = e["java_class"]; pre = cls.replace(".", "/")
        for n in self.names:
            if n.startswith(pre) and n.endswith(".class"): self.z.extract(n, self._gtmp)
        dis = C.javap(self._gtmp, cls)
        if not dis: return None
        ms = MP.methods(dis)
        goals = MP.parse_goals(ms.get("m_8099_", []))
        if not goals: return None
        out = {}
        for g in goals:
            name = g["goal"]
            if "$" in name:   # anonymous subclass (MCreator: new MeleeAttackGoal(...) { ... }): its superclass decides
                sd = C.javap(self._gtmp, (pre.rsplit("/", 1)[0] + "/" + name).replace("/", "."))
                m = re.search(r"extends ([\w.$]+)", sd or ""); name = m.group(1).split(".")[-1] if m else name
            nums = [x for x in g["args"] if isinstance(x, (int, float))]
            if name == "NearestAttackableTargetGoal":
                fam = "player" if "Player" in g["args"] else "mob"
                comp = out.setdefault("minecraft:behavior.nearest_attackable_target", {"priority": g["priority"] or 2, "must_see": False, "reselect_targets": True, "entity_types": []})
                comp["entity_types"].append({"filters": {"test": "is_family", "subject": "other", "value": fam}, "max_dist": 32})
                continue
            f = self.GOALS.get(name)
            if not f:   # the mod's own goal classes: judge by what they are named after / extend
                low = name.lower()
                alias = ("MeleeAttackGoal" if "melee" in low or ("attack" in low and "ranged" not in low and "summon" not in low and "sequence" not in low) else
                         "RandomStrollGoal" if "stroll" in low or "wander" in low else "PanicGoal" if "panic" in low else
                         "HurtByTargetGoal" if "hurtbytarget" in low or "targetattacker" in low else
                         "FollowParentGoal" if "followparent" in low or "followadult" in low else "FloatGoal" if "float" in low else
                         "AvoidEntityGoal" if "avoid" in low or "hide" in low else None)
                if alias == "MeleeAttackGoal" and "target" in low: alias = None
                if "targetgoal" in low or low.endswith("target"):
                    comp = out.setdefault("minecraft:behavior.nearest_attackable_target", {"priority": g["priority"] or 2, "must_see": False, "reselect_targets": True, "entity_types": []})
                    if not comp["entity_types"]: comp["entity_types"].append({"filters": {"test": "is_family", "subject": "other", "value": "player"}, "max_dist": 32})
                    continue
                f = self.GOALS.get(alias) if alias else None
                if not f: self.report.setdefault("goals_unmapped", {})[name] = self.report.get("goals_unmapped", {}).get(name, 0) + 1; continue
            k, v = f(nums)
            out[k] = {"priority": g["priority"] if g["priority"] is not None else 5, **v}
        return out or None

    def prop_defs(self, props):
        return {p: ({"type": "int", "range": [0, d["max"]], "default": 0, "client_sync": True} if d.get("kind") == "trigger"
                    else {"type": "bool", "default": False, "client_sync": True}) for p, d in props.items()}

    # ---------------------------------------------------------------- run
    def run(self):
        import shutil
        for d in (self.rp, self.bp): shutil.rmtree(d, ignore_errors=True)
        self.lang = []; self.ent_sounds = {}; self.item_tex = {}; self.built = set(); self.terrain = {}
        by_class = {c["class"]: c for c in self.ctrl["entities"]}
        for e in self.ents["entities"]:
            e["gb_kind"] = "block" if e["class"].endswith("BlockEntity") else "mob" if e.get("is_mob", True) else "projectile"
            self.fill_assets(e)
            self.entity(e, by_class.get(e["java_class"], {}))
        self.aliases()
        if self.override.get("content", True): self.content()
        if self.override.get("worldgen", True): self.worldgen()
        if self.override.get("javabridge", any(n.startswith("net/mcreator/") for n in self.names)): self.javabridge()
        for stem, files in getattr(self, "anim_files", {}).items():
            for a in files.values():
                if not a.get("bones"): a.pop("bones", None)    # Bedrock: empty "bones" = "Required child not found"
            self.write(self.rp, f"animations/{self.ns}/{stem}.animation.json", {"format_version": "1.8.0", "animations": files})
        if self.acs: self.write(self.rp, f"animation_controllers/{self.ns}.ac.json", {"format_version": "1.10.0", "animation_controllers": self.acs})
        self.write(self.rp, f"render_controllers/{self.ns}.rc.json", {"format_version": "1.8.0", "render_controllers": self.rcs})
        self.write(self.rp, "materials/entity.material", GLOW_MATERIAL)
        if self.ov_dir: self.apply_overlay()
        if self.terrain:
            tt = self.rp / "textures/terrain_texture.json"
            d = json.loads(tt.read_text()) if tt.exists() else {"resource_pack_name": self.title, "texture_name": "atlas.terrain", "padding": 8, "num_mip_levels": 4, "texture_data": {}}
            d["texture_data"].update(self.terrain); self.write(self.rp, "textures/terrain_texture.json", d)
        if self.sound_defs: self.write(self.rp, "sounds/sound_definitions.json", {"format_version": "1.14.0", "sound_definitions": self.sound_defs})
        if self.ent_sounds: self.write(self.rp, "sounds.json", {"entity_sounds": {"entities": self.ent_sounds}})
        if self.item_tex: self.write(self.rp, "textures/item_texture.json", {"resource_pack_name": self.title, "texture_name": "atlas.items", "texture_data": self.item_tex})
        kv = {}
        for l in self.lang:
            if "=" in l: kv[l.split("=", 1)[0]] = l.split("=", 1)[1]
        self.write(self.rp, "texts/en_US.lang", "\n".join(f"{k}={v}" for k, v in kv.items()) + "\n"); self.write(self.rp, "texts/languages.json", ["en_US"])
        rph = self.override.get("pack_uuids", {}).get("rp") or u5(self.ns, "rp"); bph = self.override.get("pack_uuids", {}).get("bp") or u5(self.ns, "bp")
        credit = (f"Models, textures and animations from {self.report['jar']} ({self.modid}) by its original authors; "
                  f"GeckoLib (MIT). Converted by GeckoBridge (Bedrock Forge) for Fernando's private personal use only. Do not share.\n")
        # test functions (dev): /function gb/<ns>/all summons every converted entity in a grid around you
        ids = [k for k, v in self.report["entities"].items() if not v.get("skipped") and not v.get("block")]
        lines = [f"summon {self.ns}:{eid} ~{(i % 8) * 4 - 14} ~ ~{(i // 8) * 4 + 4}" for i, eid in enumerate(ids)]
        self.write(self.bp, f"functions/gb/{self.ns}/all.mcfunction", "\n".join(lines) + "\n")
        self.write(self.bp, f"functions/gb/{self.ns}/clear.mcfunction", f"kill @e[family={self.ns},type=!player]\n")
        for b in (self.js.get("blocks") or {}):
            self.write(self.bp, f"functions/gb/{self.ns}/give_blocks.mcfunction", "\n".join(f"give @s {x}" for x in self.js["blocks"]) + "\n")
        use_script = (bool(self.js["entities"]) and any(c["props"] or c["triggers"] for c in self.js["entities"].values())) or bool(self.js.get("blocks"))
        ov_main = (self.bp / "scripts" / "overlay" / "main.js").exists()
        wg = getattr(self, "worldgen_script", False)
        if use_script or ov_main or wg or getattr(self, "javabridge_on", False):
            body = runtime.script(self.js) if use_script else "// GeckoBridge: overlay only\n"
            if wg: body = 'import "./worldgen.js";   // ModPorter dimensions + structure world generator\n' + body
            if getattr(self, "javabridge_on", False): body = body + '\nimport "./javabridge/main.js";   // the mod\'s own code (JavaBridge)\n'
            if ov_main: body = 'import "./overlay/main.js";   // ModPorter behaviour overlay (our own code)\n' + body
            self.write(self.bp, "scripts/gb_runtime.js", body); use_script = True
            self.report["m2_script_entities"] = len(self.js["entities"])
        for root, kind, h, dep in ((self.rp, "resources", rph, bph), (self.bp, "data", bph, rph)):
            mods = [{"type": kind, "uuid": u5(self.ns, kind, "m"), "version": [0, 1, 0]}]
            deps = [{"uuid": dep, "version": [0, 1, 0]}]
            if kind == "data" and use_script:
                mods.append({"type": "script", "language": "javascript", "uuid": u5(self.ns, "script"), "entry": "scripts/gb_runtime.js",
                             "version": [0, 1, 0]})
                deps.append({"module_name": "@minecraft/server", "version": runtime.SCRIPT_VERSION})
            self.write(root, "manifest.json", {"format_version": 2, "header": {"name": f"GB {self.title} {'RP' if kind == 'resources' else 'BP'}",
                "description": "GeckoBridge private conversion", "uuid": h, "version": [0, 1, 0], "min_engine_version": [1, 21, 0]},
                "modules": mods, "dependencies": deps})
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
