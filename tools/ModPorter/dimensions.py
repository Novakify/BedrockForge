"""ModPorter: Java datapack dimensions -> Bedrock custom dimensions (void) + a Script API world generator.

Java side (read from data/<ns>/): dimension/*.json (biomes, height), dimension_type (height, effects),
worldgen/biome (features per step, sky/fog colours, ambient sound), forge/biome_modifier (add_features),
worldgen/placed_feature + configured_feature. Every structure feature (MCreator `structure_feature`,
simple_random_selector of them) becomes a placement rule; the generated script places the converted .mcstructure
tiles chunk by chunk around players with the same placement maths (count, in_square, rarity_filter, height_range,
heightmap). Bedrock custom dimensions only have the void generator, so noise terrain is not reproduced; mods that
build their dimensions from structure tiles (The Wonderland) come out whole.
"""
from __future__ import annotations
import json, re
from pathlib import Path


STEPS = ["raw_generation", "lakes", "local_modifications", "underground_structures", "surface_structures", "strongholds",
         "underground_ores", "underground_decoration", "fluid_springs", "vegetal_decoration", "top_layer_modification"]


def jload(b):
    t = b.decode("utf-8-sig", "replace") if isinstance(b, bytes) else b
    return json.loads(re.sub(r",(\s*[}\]])", r"\1", t))


def height_of(h):
    """Java height provider -> [min, max] absolute (only constant/uniform absolute are used by MCreator)."""
    if not isinstance(h, dict): return [64, 64]
    t = h.get("type", "minecraft:constant").split(":")[-1]
    if t == "constant":
        v = h.get("value", {}); a = v.get("absolute", v.get("above_bottom", 64)) if isinstance(v, dict) else v
        return [a, a]
    if t in ("uniform", "trapezoid", "biased_to_bottom"):
        lo = h.get("min_inclusive", {}); hi = h.get("max_inclusive", {})
        return [lo.get("absolute", lo.get("above_bottom", 0)), hi.get("absolute", hi.get("below_top", 128))]
    if "absolute" in h: return [h["absolute"], h["absolute"]]
    return [64, 64]


def intval(v, d=1):
    if isinstance(v, int): return v
    if isinstance(v, dict):
        if v.get("type", "").endswith("uniform"): return [v["value"]["min_inclusive"], v["value"]["max_inclusive"]] if "value" in v else [v.get("min_inclusive", d), v.get("max_inclusive", d)]
        return v.get("value", d)
    return d


class Dimensions:
    def __init__(self, z, modid, ns, bp, report, structure_ids):
        self.z, self.modid, self.ns, self.bp, self.report = z, modid, ns, Path(bp), report
        self.names = set(z.namelist()); self.structs = structure_ids     # java "ns:name" -> bedrock structure id
        self.rep = report.setdefault("dimensions", {"count": 0, "rules": 0, "skipped_features": {}})
        self.ignored, self.sopts = {}, {}

    def read(self, kind, ref):
        ns, path = ref.split(":", 1) if ":" in ref else ("minecraft", ref)
        p = f"data/{ns}/{kind}/{path}.json"
        return jload(self.z.read(p)) if p in self.names else None

    def feature_rules(self, cf, out):
        """configured feature -> list of {"s": [structure ids], ...} alternatives (one picked at random)."""
        if isinstance(cf, str): cf = self.read("worldgen/configured_feature", cf)
        if not cf: return None
        t = cf.get("type", "").split(":")[-1]
        if t == "structure_feature":
            c = cf["config"]; sid = self.structs.get(c["structure"])
            if not sid: return None
            # BlockIgnoreProcessor(ignored_blocks): those template blocks are not placed (mostly air, so a tile
            # never wipes what the tiles before it built). A structure used by several features keeps only the
            # blocks every one of them ignores.
            ign = c.get("ignored_blocks", [])
            ign = {b for b in ([ign] if isinstance(ign, str) else ign) if isinstance(b, str) and not b.startswith("#")}
            self.ignored[sid] = (self.ignored[sid] & ign) if sid in self.ignored else ign
            off = c.get("offset") or [0, 0, 0]
            o = {"rot": bool(c.get("random_rotation")), "mir": bool(c.get("random_mirror")),
                 "off": [int(v) for v in off] if isinstance(off, list) and len(off) == 3 else [0, 0, 0]}
            if o["rot"] or o["mir"] or any(o["off"]): self.sopts[sid] = o
            return [sid]
        if t in ("simple_random_selector",):
            alts = []
            for f in cf["config"].get("features", []):
                inner = f.get("feature") if isinstance(f, dict) else f
                if isinstance(inner, str):
                    inner_pf = self.read("worldgen/placed_feature", inner); inner = inner_pf["feature"] if inner_pf else None
                r = self.feature_rules(inner, out) if inner else None
                if r: alts += r
            return alts or None
        if t == "random_selector":
            alts = []
            for f in cf["config"].get("features", []):
                pf = f.get("feature"); pf = self.read("worldgen/placed_feature", pf)["feature"] if isinstance(pf, str) and self.read("worldgen/placed_feature", pf) else pf
                r = self.feature_rules(pf, out)
                if r: alts += r
            return alts or None
        self.rep["skipped_features"][t] = self.rep["skipped_features"].get(t, 0) + 1
        return None

    def placed(self, ref):
        pf = self.read("worldgen/placed_feature", ref)
        if not pf: return None
        alts = self.feature_rules(pf["feature"], None)
        if not alts: return None
        rule = {"id": ref, "s": alts, "count": 1, "chance": 1, "y": [64, 64], "square": False, "heightmap": None}
        for p in pf.get("placement", []):
            t = p["type"].split(":")[-1]
            if t == "count": rule["count"] = intval(p.get("count"), 1)
            elif t == "rarity_filter": rule["chance"] = int(p.get("chance", 1))
            elif t == "height_range": rule["y"] = height_of(p.get("height"))
            elif t == "in_square": rule["square"] = True
            elif t == "heightmap": rule["heightmap"] = p.get("heightmap")
        return rule

    def run(self):
        dims = {}
        mods = {}
        for n in sorted(self.names):      # sorted: same output every build
            if n.startswith(f"data/{self.modid}/forge/biome_modifier/") and n.endswith(".json"):
                d = jload(self.z.read(n))
                if d.get("type", "").endswith("add_features"):
                    bs = d["biomes"] if isinstance(d["biomes"], list) else [d["biomes"]]
                    fs = d["features"] if isinstance(d["features"], list) else [d["features"]]
                    st = STEPS.index(d.get("step")) if d.get("step") in STEPS else len(STEPS)
                    for b in bs: mods.setdefault(b, []).extend((st, f) for f in fs)
        for n in sorted(self.names):
            if not (n.startswith(f"data/{self.modid}/dimension/") and n.endswith(".json")): continue
            name = Path(n).stem; d = jload(self.z.read(n)); gen = d.get("generator", {})
            src = gen.get("biome_source", {})
            biomes = [b["biome"] for b in src.get("biomes", [])] if src.get("biomes") else [src.get("biome")] if src.get("biome") else []
            dt = self.read("dimension_type", d.get("type", "")) if isinstance(d.get("type"), str) else d.get("type")
            min_y = (dt or {}).get("min_y", 0); height = (dt or {}).get("height", 256)
            rules, look, spawns = [], {}, set()
            for b in biomes:
                bj = self.read("worldgen/biome", b) or {}
                # Java decorates a chunk step by step (raw_generation first: Wonderland's ground tiles, then the
                # houses / entrances standing on them): rules keep that order and the generator places in it
                feats = [(i, f) for i, step in enumerate(bj.get("features", [])) for f in (step if isinstance(step, list) else [step])] + mods.get(b, [])
                for st, f in sorted(feats, key=lambda t: t[0]):
                    r = self.placed(f)
                    if r: rules.append(r)
                for cat in (bj.get("spawners") or {}).values():   # what Java spawns naturally here (Wonderland: nothing)
                    for sp in cat or []:
                        t = sp.get("type", "")
                        if t: spawns.add(t if ":" in t else "minecraft:" + t)
                eff = bj.get("effects", {})
                look = {"fog": eff.get("fog_color"), "sky": eff.get("sky_color"), "ambient": (eff.get("ambient_sound") or "").split(":")[-1],
                        "mood": ((eff.get("mood_sound") or {}).get("sound") or "").split(":")[-1]}
            bid = f"{self.ns}:{name}"
            dims[bid] = {"min_y": min_y, "height": height, "rules": rules, "look": look, "biomes": biomes,
                         "spawns": sorted(x.replace(f"{self.modid}:", f"{self.ns}:") for x in spawns)}
            mh = max(16, ((height + 15) // 16) * 16); my = max(-512, min(512, min_y)); mh = min(mh, 512 - my)
            self.write(f"dimensions/{name}.json", {"format_version": "1.26.50", "minecraft:dimension": {
                "description": {"identifier": bid}, "components": {"minecraft:generation": {"generator_type": "void"},
                "minecraft:dimension_height": {"min_y": my, "height_range": mh}}}})
            self.rep["count"] += 1; self.rep["rules"] += len(rules)
        self.apply_ignored()
        return dims

    def apply_ignored(self):
        """Bake each structure's ignored blocks into its .mcstructure as structure_void (palette index 0 in
        structures.py output), the Bedrock equivalent of Java's BlockIgnoreProcessor at place time."""
        import nbtlib
        n = 0
        for sid, ign in sorted(self.ignored.items()):
            if not ign: continue
            names = {b if not b.startswith(self.modid + ":") else f"{self.ns}:{b.split(':', 1)[1]}" for b in ign}
            ns, name = sid.split(":", 1); f = self.bp / f"structures/{ns}/{name}.mcstructure"
            if not f.is_file(): continue
            nbt = nbtlib.File.load(str(f), gzipped=False, byteorder="little")
            pal = nbt["structure"]["palette"]["default"]["block_palette"]
            drop = {i for i, c in enumerate(pal) if str(c["name"]) in names}
            if not drop or str(pal[0]["name"]) != "minecraft:structure_void": continue
            layer = nbt["structure"]["block_indices"][0]
            for i, v in enumerate(layer):
                if int(v) in drop: layer[i] = nbtlib.Int(0)
            nbt.save(str(f), gzipped=False, byteorder="little"); n += 1
        self.rep["ignored_baked"] = n

    def write(self, rel, data):
        p = self.bp / rel; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(json.dumps(data, indent=1))


# ---------------------------------------------------------------------------------------------- runtime (JS)
JS = r'''// ModPorter world generator (generated, original Bedrock Forge code): places the mod's structure tiles in its
// custom dimensions chunk by chunk around players, with the Java placement rules (count / in_square / rarity /
// height_range / heightmap), deterministic per world + chunk. Portal blocks move players between dimensions.
import { world, system } from "@minecraft/server";
const WG = __WG__;
const NS = WG.ns;
globalThis.__WG_DIMS = WG.dims;
// ---- custom dimensions (script registration when the JSON definition isn't picked up by this game version)
try {
  system.beforeEvents.startup.subscribe((ev) => {
    const reg = ev.dimensionRegistry; if (!reg) return;
    for (const id of Object.keys(WG.dims)) { try { reg.registerCustomDimension(id); } catch {} }
  });
} catch {}
const dimCache = {};
function dim(id) { if (!(id in dimCache)) { try { dimCache[id] = world.getDimension(id); } catch { dimCache[id] = null; } } return dimCache[id]; }
// ---- per-chunk "generated" flags, 32x32 chunk regions packed in one dynamic property string each
const regions = new Map();
function regKey(d, cx, cz) { return `wg2:${d}:${cx >> 5}:${cz >> 5}`; }
function getRegion(k) {
  if (!regions.has(k)) { let s = ""; try { s = world.getDynamicProperty(k) ?? ""; } catch {} regions.set(k, { bits: s.padEnd(1024, "0").split(""), dirty: false }); }
  return regions.get(k);
}
function isDone(d, cx, cz) { const r = getRegion(regKey(d, cx, cz)); return r.bits[((cx & 31) << 5) | (cz & 31)] === "1"; }
function markDone(d, cx, cz) { const r = getRegion(regKey(d, cx, cz)); r.bits[((cx & 31) << 5) | (cz & 31)] = "1"; r.dirty = true; }
system.runInterval(() => { for (const [k, r] of regions) if (r.dirty) { try { world.setDynamicProperty(k, r.bits.join("").replace(/0+$/, "")); } catch {} r.dirty = false; } }, 100);
// ---- deterministic random per chunk + rule
function rng(seed) { let s = seed >>> 0; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }
function hash(str, cx, cz) { let h = 2166136261; for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); } h ^= Math.imul(cx, 73856093) ^ Math.imul(cz, 19349663); return h >>> 0; }
function ri(r, a) { return Array.isArray(a) ? a[0] + Math.floor(r() * (a[1] - a[0] + 1)) : a; }
// ---- generation jobs. One job per chunk: its placements in Java step order. A job waits until every block its
// structures cover is loaded (structureManager.place throws on unloaded chunks - the old one-retry queue lost most
// tiles that way, leaving the Wonderland as a few floating roofs), then places within a per-tick time budget so
// big tiles never stall a frame. A chunk counts as generated only once all of its placements went in.
const SOPTS = WG.sopts ?? {};
const ROT = ["None", "Rotate90", "Rotate180", "Rotate270"], MIR = ["None", "X", "Z"];
const sizes = new Map();
function sizeOf(sid) {
  if (!sizes.has(sid)) { let s = null; try { s = world.structureManager.get(sid)?.size ?? null; } catch {} sizes.set(sid, s ? { x: s.x, y: s.y, z: s.z } : null); }
  return sizes.get(sid);
}
function loadedAt(d, x, y, z) { try { return !!d.getBlock({ x, y, z }); } catch { return false; } }
const jobs = [], pending = new Set();
function generate(dId, cx, cz) {
  const D = WG.dims[dId]; const d = dim(dId); if (!D || !d) return;
  const key = `${dId}|${cx}|${cz}`; if (pending.has(key)) return;
  const places = [];
  for (const rule of D.rules) {
    const r = rng(hash(dId + rule.id, cx, cz));
    if (rule.chance > 1 && r() >= 1 / rule.chance) continue;
    const n = ri(r, rule.count);
    for (let i = 0; i < n; i++) {
      const x = cx * 16 + (rule.square ? Math.floor(r() * 16) : 0), z = cz * 16 + (rule.square ? Math.floor(r() * 16) : 0);
      const y = rule.y[0] + Math.floor(r() * (rule.y[1] - rule.y[0] + 1));
      const sid = rule.s[Math.floor(r() * rule.s.length)], o = SOPTS[sid];
      const q = { sid, x, y, z, hm: rule.heightmap };
      if (o) { q.x += o.off[0]; q.y += o.off[1]; q.z += o.off[2]; if (o.rot) q.rot = ROT[Math.floor(r() * 4)]; if (o.mir) q.mir = MIR[Math.floor(r() * 3)]; }
      places.push(q);
    }
  }
  if (!places.length) { markDone(dId, cx, cz); return; }
  pending.add(key); jobs.push({ key, dId, cx, cz, d, D, places, i: 0, next: 0 });
}
function playerNear(dId, cx, cz, r) {
  for (const p of world.getAllPlayers()) {
    let pd; try { pd = p.dimension.id; } catch { continue; } if (pd !== dId) continue;
    if (Math.abs(Math.floor(p.location.x / 16) - cx) <= r && Math.abs(Math.floor(p.location.z / 16) - cz) <= r) return true;
  }
  return false;
}
function ready(j, q) {
  const s = sizeOf(q.sid) ?? { x: 16, y: 1, z: 16 };
  const y = Math.max(j.D.min_y, Math.min(j.D.min_y + j.D.height - 1, q.y));
  return loadedAt(j.d, q.x, y, q.z) && loadedAt(j.d, q.x + s.x - 1, y, q.z + s.z - 1) && loadedAt(j.d, q.x + s.x - 1, y, q.z) && loadedAt(j.d, q.x, y, q.z + s.z - 1);
}
const MS = 4;   // generation's share of the 50 ms server tick
system.runInterval(() => {
  const t0 = Date.now(), now = system.currentTick;
  for (let k = 0; k < jobs.length; k++) {
    if (Date.now() - t0 >= MS) break;
    const j = jobs[k]; if (j.next > now) continue;
    if (!playerNear(j.dId, j.cx, j.cz, RADIUS + 4)) { jobs.splice(k--, 1); pending.delete(j.key); continue; }   // regenerates when someone comes back
    while (j.i < j.places.length && Date.now() - t0 < MS) {
      const q = j.places[j.i];
      if (!ready(j, q)) { j.next = now + 10; break; }
      try {
        let y = q.y;
        if (q.hm) { const top = j.d.getTopmostBlock({ x: q.x, z: q.z }); if (top) y = top.location.y + 1; }
        const opt = {}; if (q.rot) opt.rotation = q.rot; if (q.mir) opt.mirror = q.mir;
        world.structureManager.place(q.sid, j.d, { x: q.x, y, z: q.z }, opt);
        j.i++;
      } catch (e) {
        q.fail = (q.fail ?? 0) + 1;
        if (q.fail >= 5) { if (q.fail === 5) console.warn(`[WorldGen] ${q.sid} at ${q.x},${q.y},${q.z}: ${e}`); j.i++; }
        else { j.next = now + 20; break; }
      }
    }
    if (j.i >= j.places.length) { markDone(j.dId, j.cx, j.cz); pending.delete(j.key); jobs.splice(k--, 1); }
  }
  globalThis.__GB_PERF?.add("worldgen", Date.now() - t0);
}, 1);
const RADIUS = 4;
system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    const dId = p.dimension.id; if (!WG.dims[dId]) continue;
    const pcx = Math.floor(p.location.x / 16), pcz = Math.floor(p.location.z / 16);
    for (let r = 0; r <= RADIUS; r++) for (let dx = -r; dx <= r; dx++) for (let dz = -r; dz <= r; dz++) {   // nearest first
      if (Math.max(Math.abs(dx), Math.abs(dz)) !== r) continue;
      const cx = pcx + dx, cz = pcz + dz;
      if (isDone(dId, cx, cz)) continue;
      generate(dId, cx, cz);
    }
  }
}, 20);
// ---- natural spawns: a Bedrock custom dimension spawns the vanilla mobs of its default biome (creepers, zombies,
// skeletons...), a Java mod dimension only what its biomes list (Wonderland: nothing - its creatures come from the
// mod's own code). Vanilla mobs that appear in a mod dimension on their own are removed; the mod's mobs, anything the
// mod's code spawns (spawnEntity / summon this tick, tagged gb_own) and the biome's Java spawn list stay.
world.afterEvents.entitySpawn.subscribe(({ entity, cause }) => {
  let d, t; try { d = entity.dimension.id; t = entity.typeId; } catch { return; }
  const D = WG.dims[d]; if (!D || t.startsWith(NS + ":") || t === "minecraft:player") return;
  if ((D.spawns ?? []).includes(t)) return;
  try { if (entity.hasTag("gb_own")) return; } catch { return; }
  const own = globalThis.__GB_OWN_SPAWN;
  if (cause !== "Loaded" && own != null && system.currentTick - own <= 1) { try { entity.addTag("gb_own"); } catch {} return; }
  let mob = false; try { mob = entity.matches({ families: ["mob"] }) || entity.matches({ families: ["monster"] }); } catch {}
  if (mob) { try { entity.remove(); } catch {} }
});
// ---- dev travel: /scriptevent wg:tp <dimension name|overworld>
system.afterEvents.scriptEventReceive.subscribe((ev) => {
  if (ev.id !== "wg:tp" || !ev.sourceEntity) return;
  const p = ev.sourceEntity, name = ev.message.trim();
  const id = name.includes(":") ? name : (name === "overworld" ? "minecraft:overworld" : `${NS}:${name}`);
  const d = dim(id); if (!d) { try { p.sendMessage(`no dimension ${id}`); } catch {} return; }
  const D = WG.dims[id]; const x = Math.floor(p.location.x), z = Math.floor(p.location.z);
  if (D) generate(id, Math.floor(x / 16), Math.floor(z / 16));
  try { p.teleport({ x: x + 0.5, y: D ? D.min_y + Math.min(D.height - 2, 100) : 100, z: z + 0.5 }, { dimension: d }); p.addEffect("slow_falling", 300, { showParticles: false }); } catch (e) { try { p.sendMessage(String(e)); } catch {} }
});
// ---- look: fog per dimension (fog definitions in the resource pack), ambient sound loop
const inDim = new Map();
system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    const dId = p.dimension.id, prev = inDim.get(p.id);
    if (prev === dId) continue;
    inDim.set(p.id, dId);
    try { p.runCommand(`fog @s remove "${NS}_dim"`); } catch {}
    const D = WG.dims[dId];
    if (D) { try { p.runCommand(`fog @s push ${NS}:fog_${dId.split(":")[1]} "${NS}_dim"`); } catch {} }
  }
}, 20);
system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    const D = WG.dims[p.dimension.id]; if (!D || !D.look.ambient) continue;
    try { p.playSound(`${NS}.${D.look.ambient}`, { volume: 0.6 }); } catch {}
  }
}, 20 * 30);
// ---- portals: standing in <name>_portal moves you to <name> and back (Java NetherPortalBlock-style)
const PORTALS = WG.portals;
const cooldown = new Map();
function safeY(d, x, z, fallback) {
  try { const top = d.getTopmostBlock({ x, z }); if (top) return top.location.y + 1; } catch {}
  return fallback;
}
system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    if ((cooldown.get(p.id) ?? 0) > system.currentTick) continue;
    let b; try { b = p.dimension.getBlock({ x: Math.floor(p.location.x), y: Math.floor(p.location.y + 0.2), z: Math.floor(p.location.z) }); } catch { continue; }
    const target = b && PORTALS[b.typeId]; if (!target) continue;
    cooldown.set(p.id, system.currentTick + 100);
    const here = p.dimension.id;
    if (here === target) {          // leave: back where the player came in from
      let back; try { back = JSON.parse(p.getDynamicProperty(`wg:back:${target}`) ?? "null"); } catch {}
      const d = dim(back?.d ?? "minecraft:overworld");
      try { p.teleport(back ?? world.getDefaultSpawnLocation(), { dimension: d }); } catch {}
    } else {
      try { p.setDynamicProperty(`wg:back:${target}`, JSON.stringify({ d: here, x: p.location.x + 2, y: p.location.y, z: p.location.z + 2 })); } catch {}
      const d = dim(target); if (!d) continue;
      const D = WG.dims[target]; const x = Math.floor(p.location.x), z = Math.floor(p.location.z);
      generate(target, Math.floor(x / 16), Math.floor(z / 16));
      try { p.teleport({ x: x + 0.5, y: D.min_y + Math.min(D.height - 2, 80), z: z + 0.5 }, { dimension: d }); } catch {}
      system.runTimeout(() => { try { const y = safeY(d, x, z, null); if (y !== null) p.teleport({ x: x + 0.5, y, z: z + 0.5 }, { dimension: d }); } catch {} }, 60);
      try { p.addEffect("slow_falling", 200, { showParticles: false }); } catch {}
    }
  }
}, 4);
'''


def script(cfg):
    return JS.replace("__WG__", json.dumps(cfg, separators=(",", ":")))


def fogs(dims, ns):
    out = {}
    for did, D in dims.items():
        c = D["look"].get("fog")
        if c is None: continue
        c &= 0xFFFFFF; hexc = f"#{c:06X}"
        out[f"fogs/{did.split(':')[1]}.json"] = {"format_version": "1.16.100", "minecraft:fog_settings": {
            "description": {"identifier": f"{ns}:fog_{did.split(':')[1]}"},
            "distance": {"air": {"fog_start": 0, "fog_end": 48, "fog_color": hexc, "render_distance_type": "fixed"},
                         "weather": {"fog_start": 0, "fog_end": 32, "fog_color": hexc, "render_distance_type": "fixed"}}}}
    return out
