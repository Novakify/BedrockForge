"""GeckoBridge M2 runtime: Java-side conditions and triggered animations -> entity properties + a Script API driver.

GeckoLib decides animations from Java state Bedrock has no query for (Bear.isEating(), Cave Dweller FLEEING, a duck
named "Queso"...). M1 parked those as reserved Molang variables (always "false"). M2 turns each one into a client-synced
entity property `gb:<name>` that the client reads with q.property(), and a generated Script API file sets it through a
small set of generic DRIVERS (classify() picks one from the Java name + arguments; mods/<modid>.json can override):

  name_equals    nameTag equals a string the Java code compares against (case-insensitive)      [exact]
  random_spawn   chosen once at spawn (Lion.hasMane, enum variants)                                   [exact distribution unknown: 50%]
  night_sleep    sleeps at night when idle and nobody is near (Bear.isSleeping override); mob stops  [approx]
  idle_action    random short action while standing still with no player close (eat, sniff, dig...)  [approx]
  near_player    a player within N blocks (Snake.canRattle, Tortoise.canHide)                          [approx]
  after_attack   true for N ticks after this mob hit something (Rhino stun, charge cooldown)           [approx]
  block_solid    the block at an offset from the mob is solid (Cave Dweller crawl/crouch tunnels)       [exact]
  has_target     a player within the mob's follow range (Sculk isAggro / IS_ACTIVE ...)                [approx]
  off            never true (reported)

Triggered animations (GeckoLib triggerableAnim + triggerAnim(controller, name) in goals/entities) become an int
property `gb:trig` and one extra animation controller; the driver fires them on melee hits ("melee"), as an ability
cycle while a player is in range ("ability"), on spawn ("spawn") or periodically ("periodic").
"""
from __future__ import annotations
import json, os, re, tempfile, zipfile
from pathlib import Path

import controllers as C

SCRIPT_VERSION = "2.3.0"


# ---------------------------------------------------------------- triggers
GOAL_ROLES = (("ability", ("shoot", "spit", "throw", "summon", "zoltraak", "sonic", "rain", "rift", "bubble", "fangs", "circle",
                           "spear", "blast", "barrage", "buff", "acid", "magic", "elemental", "ranged", "spike", "dispense", "boost")),
              ("melee", ("melee", "attackgoal", "attack", "infect", "charge", "bite", "slam", "spin")),)


def triggers(jar, tmp=None):
    """[(owner_class, caller_class, method, controller, animation, role)] for every triggerAnim call in the jar."""
    tmp = tmp or tempfile.mkdtemp(prefix="gb_trig_")
    out = []
    with zipfile.ZipFile(jar) as z:
        callers = [n for n in z.namelist() if n.endswith(".class") and not n.startswith(C.GL_PREFIXES) and (b"triggerAnim" in z.read(n) or b"setAnimation" in z.read(n))]
        geo = [n for n in z.namelist() if n.endswith(".class") and not n.startswith(C.GL_PREFIXES) and b"registerControllers" in z.read(n)]
        for n in set(callers) | set(geo): z.extract(n, tmp)
        geo_cls = [g[:-6].replace("/", ".") for g in geo]
        geo_raw = {g: z.read(g.replace(".", "/") + ".class") for g in geo_cls}
    C.prefetch(tmp, [n[:-6].replace("/", ".") for n in set(callers) | set(geo)])
    for n in callers:
        cls = n[:-6].replace("/", ".")
        for mname, code in C.parse_methods(C.javap(tmp, cls)).items():
            strs = []
            for off, op, rest in code:
                v = C.const_of(op, rest) if op.startswith("ldc") else None
                if isinstance(v, str): strs.append(v)
                if op.startswith("invoke") and C.member(rest)[1] == "setAnimation" and strs and "(Ljava/lang/String;)V" in rest:
                    # MCreator: procedures call <Entity>.setAnimation("name"); the entity's procedure controller plays it
                    anim = strs[-1]
                    if anim != "empty":
                        owner = C.member(rest)[0].replace("/", ".")
                        low = cls.split(".")[-1].lower()
                        role = "spawn" if "spawn" in low and "tick" not in low else "interact" if "rightclick" in low else \
                               "melee" if any(k in low for k in ("attack", "hits", "hurt")) else "logic"
                        out.append({"owner": owner, "caller": cls.split(".")[-1], "method": mname, "controller": "procedure",
                                    "anim": anim, "role": role, "mcreator": True})
                    continue
                if op.startswith("invoke") and C.member(rest)[1] == "triggerAnim" and len(strs) >= 2:
                    ctrl, anim = strs[-2], strs[-1]
                    owner = cls.split("$")[0] if "$" in cls and cls.split("$")[0] in geo_cls else (cls if cls in geo_cls else None)
                    if owner is None:   # standalone goal class: whichever GeoEntity class references it
                        short = cls.replace(".", "/").encode()
                        owner = next((g for g, raw in geo_raw.items() if short in raw), None)
                    low = (cls.split(".")[-1] + mname).lower()
                    role = "spawn" if mname in ("<init>", "m_6075_", "onAddedToWorld") else \
                           "periodic" if mname in ("m_8119_", "tick", "m_6075_", "m_8024_", "aiStep", "m_8107_") and "goal" not in low else \
                           next((r for r, keys in GOAL_ROLES if any(k in low for k in keys)), "ability")
                    out.append({"owner": owner, "caller": cls.split(".")[-1], "method": mname, "controller": ctrl, "anim": anim, "role": role})
    return out


# ---------------------------------------------------------------- conditions -> drivers
def classify(var, info, override=None):
    if override and var in override: return override[var]
    java = (info or {}).get("java", var).lower(); consts = (info or {}).get("consts", []); fields = (info or {}).get("fields", [])
    last = java.split(".")[-1]
    strs = [c for c in consts if isinstance(c, str)]
    if strs and any(k in java for k in ("m_7755_", "getname", "m_8077_", "m_6302_", "getstring", "equals")):
        return {"kind": "name_equals", "value": strs[0].lower()}
    if any(k in java for k in ("mane", "male", "gender")): return {"kind": "random_spawn", "p": 0.5}
    if "m_5803_" in java or "sleep" in java: return {"kind": "night_sleep"}
    if any(k in java for k in ("eat", "sniff", "dig", "egg", "laying", "graz", "sit", "groom", "dance")):
        return {"kind": "idle_action", "chance": 0.004, "min": 60, "max": 160}
    if "hide" in java: return {"kind": "near_player", "dist": 5}
    if "rattle" in java: return {"kind": "near_player", "dist": 7}
    if any(k in java for k in ("stun", "cooldown", "vibration")): return {"kind": "after_attack", "ticks": 60}
    if any(k in java for k in ("aggro", "active", "prepared", "angry", "hastarget", "attacking", "charging")):
        return {"kind": "has_target", "dist": 16}
    if fields and any("variant" in f.lower() or "type" in f.lower() for f in fields): return {"kind": "random_spawn", "p": 0.5}
    if "equals" in java and fields: return {"kind": "random_spawn", "p": 0.25}
    return {"kind": "off"}


def prop_name(var):
    return "gb:" + var.replace("v.gb_", "")[:26]


# ---------------------------------------------------------------- Script API driver (generated, original code)
JS = r'''// GeckoBridge runtime (generated). Sets the entity properties that stand in for GeckoLib's Java-side conditions
// and fires triggered animations. Original Bedrock Forge code; no code from the converted mod.
import { world, system } from "@minecraft/server";
const CFG = __CONFIG__;
const NS = CFG.ns, TICK = 4;
const rnd = (a, b) => a + Math.floor(Math.random() * (b - a + 1));
const state = new Map();     // entity id -> { until: {prop: tick}, cool: {prop: tick}, trigUntil }
const st = (e) => { let s = state.get(e.id); if (!s) { s = { until: {}, cool: {}, trigUntil: 0, abil: 0 }; state.set(e.id, s); } return s; };
function setProp(e, p, v) { try { if (e.getProperty(p) !== v) e.setProperty(p, v); } catch {} }
function nearPlayer(e, dist) { try { return e.dimension.getPlayers({ location: e.location, maxDistance: dist }).length > 0; } catch { return false; } }
function moving(e) { try { const v = e.getVelocity(); return Math.hypot(v.x, v.z) > 0.02; } catch { return false; } }
function solidAt(e, o) {
  try {
    const l = e.location, r = (e.getRotation?.().y ?? 0) * Math.PI / 180;
    const fx = -Math.sin(r) * (o.fwd ?? 0), fz = Math.cos(r) * (o.fwd ?? 0);
    const b = e.dimension.getBlock({ x: Math.floor(l.x + fx), y: Math.floor(l.y + (o.dy ?? 0)), z: Math.floor(l.z + fz) });
    return !!b && !b.isAir && !b.isLiquid;
  } catch { return false; }
}
const night = () => { const t = world.getTimeOfDay(); return t > 13000 && t < 23000; };
const lastHit = new Map();   // entity id -> tick it last hit something
let now = 0;

function trigger(e, conf, name) {
  if (conf.hooks && conf.hooks[name]) name = conf.hooks[name];
  let t = conf.triggers.find((x) => x.anim === name);
  if (!t && (name?.startsWith?.("@") || name === "ability")) { const ab = conf.triggers.filter((x) => x.role === "ability"); t = ab[rnd(0, ab.length - 1)]; }
  if (!t) return;
  st(e).lastTrig = now;
  const s = st(e); if (now < s.trigUntil) return;
  setProp(e, "gb:trig", t.index); s.trigUntil = now + Math.max(4, Math.ceil(t.length * 20)) + 2;
}
globalThis.gbTrigger = (e, name) => { const c = CFG.entities[e.typeId]; if (c) trigger(e, c, name); };

function drive(e, conf) {
  const s = st(e);
  for (const [p, d] of Object.entries(conf.props)) {
    let v = false;
    switch (d.kind) {
      case "name_equals": { const n = e.nameTag ?? ""; v = (d.case ? n : n.toLowerCase()) === d.value; break; }
      case "name_contains": { const n = e.nameTag ?? ""; v = (d.case ? n : n.toLowerCase()).includes(d.value); break; }
      case "random_spawn": continue;   // chosen once in spawn()
      case "night_sleep": v = night() && !moving(e) && !nearPlayer(e, 6); break;
      case "idle_action":
        if (now < (s.until[p] ?? 0)) v = true;
        else if (now > (s.cool[p] ?? 0) && !moving(e) && !nearPlayer(e, 5) && Math.random() < d.chance * TICK) {
          s.until[p] = now + rnd(d.min, d.max); s.cool[p] = s.until[p] + rnd(200, 600); v = true;
        }
        break;
      case "near_player": v = nearPlayer(e, d.dist); break;
      case "night": v = night(); break;
      case "after_trigger": v = now - (s.lastTrig ?? -9999) < d.ticks; break;
      case "after_attack": v = now - (lastHit.get(e.id) ?? -9999) < d.ticks; break;
      case "has_target": { if (!("target" in e)) { v = nearPlayer(e, d.dist); break; } let t; try { t = e.target; } catch {} v = !!t && t.isValid !== false; break; }
      case "near_target": v = nearPlayer(e, d.dist); break;
      case "block_solid": v = solidAt(e, d); break;
      default: v = false;
    }
    setProp(e, p, v);
    if (d.kind === "night_sleep") {
      try { if (v) e.addEffect("slowness", 20, { amplifier: 255, showParticles: false }); } catch {}
    }
  }
  if (conf.triggers.length) {
    if (now >= s.trigUntil && e.getProperty("gb:trig") !== 0) setProp(e, "gb:trig", 0);
    const abil = conf.triggers.filter((t) => t.role === "ability" || t.role === "periodic" || t.role === "logic");
    // hurt reactions
    if (now - (s.hurt ?? -99) <= TICK) { const h = conf.triggers.find((t) => t.role === "hurt"); if (h) trigger(e, conf, h.anim); }
    if (abil.length && !conf.hooked && now >= s.trigUntil && now >= s.abil) {
      const ok = abil.some((t) => t.role === "periodic") || nearPlayer(e, conf.range);
      if (ok) { s.abil = now + rnd(100, 220); const t = abil[rnd(0, abil.length - 1)]; trigger(e, conf, t.anim); }
    }
  }
}

function spawn(e) {
  const conf = CFG.entities[e.typeId]; if (!conf) return;
  for (const [p, d] of Object.entries(conf.props)) if (d.kind === "random_spawn") setProp(e, p, Math.random() < d.p);
  for (const t of conf.triggers) if (t.role === "spawn") { system.runTimeout(() => { try { trigger(e, conf, t.anim); } catch {} }, 2); break; }
}
world.afterEvents.entitySpawn.subscribe(({ entity }) => { if (entity?.typeId?.startsWith(NS + ":")) spawn(entity); });
world.afterEvents.entityHurt.subscribe(({ hurtEntity: e }) => { if (e && CFG.entities[e.typeId]) st(e).hurt = now; });
world.afterEvents.entityHitEntity.subscribe(({ damagingEntity: e }) => {
  const conf = e && CFG.entities[e.typeId]; if (!conf) return;
  lastHit.set(e.id, now);
  const melee = conf.triggers.filter((t) => t.role === "melee");
  if (melee.length) trigger(e, conf, melee[rnd(0, melee.length - 1)].anim);
});
world.afterEvents.playerInteractWithEntity?.subscribe(({ target: e }) => {
  const conf = e && CFG.entities[e.typeId]; if (!conf) return;
  const t = conf.triggers.filter((x) => x.role === "interact"); if (t.length) trigger(e, conf, t[rnd(0, t.length - 1)].anim);
});
world.afterEvents.entityDie.subscribe(({ deadEntity: e }) => {
  const conf = e && CFG.entities[e.typeId]; if (!conf) return;
  const d = conf.triggers.find((t) => t.role === "death"); if (d) { try { setProp(e, "gb:trig", d.index); } catch {} }
});
world.afterEvents.entityRemove.subscribe(({ removedEntityId }) => { state.delete(removedEntityId); lastHit.delete(removedEntityId); });

// GeckoLib block entities: Bedrock blocks can't animate, so a player-placed block gets an animated companion entity
// (the converted GeckoLib model) and hides its own bones while it is there.
function liveDims() {             // dimensions with a player in them (only those have ticking chunks to look at)
  const out = [], seen = new Set();
  for (const p of world.getAllPlayers()) { let d; try { d = p.dimension; } catch { continue; } if (!seen.has(d.id)) { seen.add(d.id); out.push(d); } }
  return out;
}
const BLOCKS = CFG.blocks ?? {};
function setHas(block, v) { try { block.setPermutation(block.permutation.withState("gb:has_entity", v)); } catch {} }
function companionsAt(dim, loc, id) {
  try { return dim.getEntities({ type: id, location: { x: loc.x + 0.5, y: loc.y, z: loc.z + 0.5 }, maxDistance: 0.75 }); } catch { return []; }
}
if (Object.keys(BLOCKS).length) {
  world.afterEvents.playerPlaceBlock.subscribe(({ block }) => {
    const id = BLOCKS[block.typeId]; if (!id) return;
    try { block.dimension.spawnEntity(id, { x: block.location.x + 0.5, y: block.location.y, z: block.location.z + 0.5 }); setHas(block, true); } catch {}
  });
  world.afterEvents.playerBreakBlock.subscribe(({ block, brokenBlockPermutation }) => {
    const id = BLOCKS[brokenBlockPermutation.type.id]; if (!id) return;
    for (const c of companionsAt(block.dimension, block.location, id)) { try { c.remove(); } catch {} }
  });
  system.runInterval(() => {      // block gone some other way (explosion, horde): drop its companion
    for (const dm of liveDims()) {
      let list = []; try { list = dm.getEntities({ families: ["gb_block_companion"] }); } catch { continue; }
      for (const c of list) {
        try {
          const b = c.dimension.getBlock({ x: Math.floor(c.location.x), y: Math.floor(c.location.y + 0.01), z: Math.floor(c.location.z) });
          const want = Object.keys(BLOCKS).find((k) => BLOCKS[k] === c.typeId);
          if (!b || b.typeId !== want) c.remove();
        } catch {}
      }
    }
  }, 40);
}

system.runInterval(() => {
  now += TICK; const t0 = Date.now();
  for (const dm of liveDims()) {      // every dimension a player is in (mod dimensions too: their mobs animate)
    let list = [];
    try { list = dm.getEntities({ families: [NS] }); } catch { continue; }
    for (const e of list) { const conf = CFG.entities[e.typeId]; if (conf) { try { drive(e, conf); } catch {} } }
  }
  globalThis.__GB_PERF?.add("anim", Date.now() - t0);
}, TICK);
'''


def script(config):
    return JS.replace("__CONFIG__", json.dumps(config, separators=(",", ":")))
