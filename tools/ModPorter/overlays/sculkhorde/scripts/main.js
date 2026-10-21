// Bedrock Forge - Sculk Horde (Apache-2.0, TeamPeril) core loop, original Script API re-implementation.
// Mirrors the Java mod's design:
//   * GRAVEMIND: one horde per world. Accumulated mass drives evolution: Undeveloped -> Immature (5000) -> Mature (20000).
//     Higher stages unlock stronger mobs (same table, costs and limits as the mod's EntityFactory).
//   * BURROWED: horde attacks burrow into victims. When a burrowed victim dies (or the effect runs out) a mite / leech /
//     stinger bursts out and a SCULK MASS block appears: victim max health x3 mass, 1/3 taxed to the gravemind,
//     the rest is spent by that mass block spawning horde mobs around it.
//   * INFESTATION: virtual cursors walk through terrain turning it to sculk (throttled: a fixed block budget per tick).
//   * NODES: once Immature the horde grows Sculk Nodes underground near its masses. Nodes keep infesting, darken
//     players close by and spend gravemind mass on defenders. Destroying a node costs the horde half its mass.
//   * ANCIENT NODE: the horde sleeps until a player activates a Sculk Ancient Node (or an operator runs /sh:start).
//   * Mob abilities: mites infect and die, creepers detonate into infestation, spitters spit acid, hatchers lay mites,
//     spore spewers spread spores, pods metamorphose, witches throw sickness, the enderman teleports onto targets.
// Debug: /sh:status  /sh:start  /sh:stop  /sh:mass <amount>  /sh:node  /sh:infect  /sh:budget <blocks_per_tick>  /sh:clear

import { world, system, Player, GameMode, EntityDamageCause, CommandPermissionLevel, CustomCommandParamType, CustomCommandStatus } from "@minecraft/server";

const NS = "sculkhorde";
const CFG = {
  massImmature: 5000, massMature: 20000, healthAbsorb: 3, tax: 1 / 3,
  burrowTicks: 200, blockBudget: 6, maxCursors: 40, cursorSteps: [24, 64],
  nodeCooldownTicks: 20 * 60 * 20, nodeMinSpacing: 160, maxNodes: 4, nodeRadius: 48, nodeDarkness: 24,
  massSpawnInterval: 100, maxMasses: 48, mobCapNear: 24, nodeDefenderCost: 40,
};
const STAGES = ["Undeveloped", "Immature", "Mature"];
// EntityFactory (id, min stage, cost, limit near the spawner, can the mass block buy it)
const FACTORY = [
  ["sculk_mite", 0, 5, 0, true], ["sculk_mite_aggressor", 0, 6, 0, true], ["sculk_zombie", 0, 20, 0, true],
  ["sculk_spitter", 0, 20, 0, true], ["sculk_hatcher", 0, 30, 0, false], ["sculk_salmon", 0, 5, 0, true, "water"],
  ["sculk_leech", 0, 5, 0, true, "water"], ["sculk_stinger", 0, 5, 0, true],
  ["sculk_creeper", 1, 20, 0, true], ["sculk_vindicator", 1, 10, 0, true], ["sculk_sheep", 1, 20, 0, true],
  ["sculk_ravager", 1, 50, 1, false], ["sculk_spore_spewer", 1, 40, 1, false], ["sculk_squid", 1, 8, 1, true, "water"],
  ["sculk_pufferfish", 1, 8, 0, true, "water"], ["sculk_phantom", 2, 15, 1, false], ["sculk_guardian", 2, 8, 1, true, "water"],
  ["sculk_witch", 2, 10, 2, true],
].map(([id, stage, cost, limit, mass, medium]) => ({ id: `${NS}:${id}`, stage: Number(stage), cost: Number(cost), limit: Number(limit), mass: !!mass, medium: String(medium ?? "land") }));
const HORDE = new Set(FACTORY.map((f) => f.id).concat([`${NS}:sculk_bee_infector`, `${NS}:sculk_bee_harvester`, `${NS}:sculk_phantom_corpse`,
  `${NS}:sculk_enderman`, `${NS}:sculk_metamorphosis_pod`, `${NS}:sculk_ghast`, `${NS}:sculk_brood_hatcher`, `${NS}:sculk_brood_spitter`,
  `${NS}:sculk_brood_striker`, `${NS}:sculk_vex`]));
const BURROWERS = new Set(["sculk_zombie", "sculk_mite", "sculk_vindicator", "sculk_hatcher", "sculk_sheep", "sculk_ravager",
  "sculk_salmon", "sculk_leech", "sculk_stinger", "sculk_brood_striker", "sculk_squid"].map((i) => `${NS}:${i}`));

// ---------------------------------------------------------------- persistent horde state
let S = { active: false, mass: 0, stage: 0, nodes: [], masses: [], lastNode: -1e9, budget: CFG.blockBudget };
let dirty = false;
try { const raw = world.getDynamicProperty("sh:state"); if (typeof raw === "string") S = { ...S, ...JSON.parse(raw) }; } catch {}
const save = () => { dirty = true; };
system.runInterval(() => { if (dirty) { dirty = false; try { world.setDynamicProperty("sh:state", JSON.stringify(S)); } catch {} } }, 100);

function addMass(n) {
  S.mass = Math.max(0, Math.round(S.mass + n));
  const st = S.mass >= CFG.massMature ? 2 : S.mass >= CFG.massImmature ? 1 : 0;
  if (st > S.stage) { S.stage = st; world.sendMessage(`§3The Sculk Horde has evolved: §b${STAGES[st]}§3.`); }
  save();
}

// ---------------------------------------------------------------- helpers
const key = (d, p) => `${d}|${Math.floor(p.x)},${Math.floor(p.y)},${Math.floor(p.z)}`;
const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
const rnd = (a, b) => a + Math.random() * (b - a);
const pick = (arr) => arr[Math.floor(Math.random() * arr.length)];
const alive = (e) => { try { return !!e && e.isValid; } catch { return false; } };
const survival = (p) => { try { const g = p.getGameMode(); return g !== GameMode.Creative && g !== GameMode.Spectator; } catch { return false; } };
const dim = (id) => world.getDimension(id);
function block(d, p) { try { return d.getBlock({ x: Math.floor(p.x), y: Math.floor(p.y), z: Math.floor(p.z) }); } catch { return undefined; } }
function particle(d, id, p) { try { d.spawnParticle(id, p); } catch {} }
function sound(d, id, p) { try { d.playSound(id, p); } catch {} }
function los(d, from, to) {
  const v = { x: to.x - from.x, y: to.y - from.y, z: to.z - from.z }, l = Math.hypot(v.x, v.y, v.z) || 1;
  try { return !d.getBlockFromRay(from, { x: v.x / l, y: v.y / l, z: v.z / l }, { maxDistance: l, includeLiquidBlocks: false, includePassableBlocks: false }); }
  catch { return false; }
}
function nearestPlayer(e, max, minDist = 0) {
  let best, bd = max;
  for (const p of e.dimension.getPlayers({ location: e.location, maxDistance: max })) {
    if (!survival(p)) continue; const dd = dist(p.location, e.location); if (dd < bd && dd >= minDist) { bd = dd; best = p; }
  }
  return best;
}
function spawn(d, id, p) { try { const e = d.spawnEntity(id, p); particle(d, "minecraft:sculk_soul_particle", p); return e; } catch { return undefined; } }
function groundAbove(d, p, up = 6) {   // first air-over-solid spot near p
  for (let dy = 0; dy <= up; dy++) {
    const b = block(d, { x: p.x, y: p.y + dy, z: p.z }), below = block(d, { x: p.x, y: p.y + dy - 1, z: p.z });
    if (b && below && b.isAir && !below.isAir && !below.isLiquid) return { x: Math.floor(p.x) + 0.5, y: Math.floor(p.y) + dy, z: Math.floor(p.z) + 0.5 };
  }
  return undefined;
}

// ---------------------------------------------------------------- infestation cursors (throttled)
const INFECTABLE = /^minecraft:(stone|cobblestone|mossy_cobblestone|deepslate|cobbled_deepslate|tuff|granite|diorite|andesite|calcite|dirt|coarse_dirt|rooted_dirt|grass_block|podzol|mycelium|mud|clay|gravel|sand|red_sand|sandstone|red_sandstone|moss_block|netherrack|soul_sand|soul_soil|blackstone|basalt|end_stone|terracotta|\w+_terracotta|dripstone_block|smooth_basalt|packed_mud|mud_bricks|\w+_log|\w+_wood|\w+_planks|stone_bricks|deepslate_bricks|deepslate_tiles)$/;
const CLEARABLE = /^minecraft:(short_grass|tall_grass|fern|large_fern|\w+_leaves|dandelion|poppy|\w+_tulip|azure_bluet|oxeye_daisy|cornflower|allium|blue_orchid|dead_bush|snow_layer)$/;
const cursors = [];
const NB = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
function addCursor(dimId, p, steps) {
  if (cursors.length >= CFG.maxCursors) cursors.shift();
  cursors.push({ d: dimId, x: Math.floor(p.x), y: Math.floor(p.y), z: Math.floor(p.z), left: steps ?? Math.round(rnd(...CFG.cursorSteps)) });
}
function exposed(d, p) { for (const [x, y, z] of NB) { const b = block(d, { x: p.x + x, y: p.y + y, z: p.z + z }); if (b && (b.isAir || CLEARABLE.test(b.typeId))) return true; } return false; }
function infect(d, b) {
  b.setType("minecraft:sculk");
  const up = block(d, { x: b.location.x, y: b.location.y + 1, z: b.location.z });
  if (up && CLEARABLE.test(up.typeId)) up.setType("minecraft:air");
  if (up && up.isAir) {
    const r = Math.random();
    if (r < 0.004) up.setType("minecraft:sculk_sensor"); else if (r < 0.006) up.setType("minecraft:sculk_shrieker");
  }
}
function stepCursor(c) {
  const d = dim(c.d);
  const opts = [];
  for (const [x, y, z] of NB) {
    const p = { x: c.x + x, y: c.y + y, z: c.z + z }, b = block(d, p);
    if (b === undefined) { c.left = 0; return; }          // unloaded chunk: the cursor dies
    if (INFECTABLE.test(b.typeId) && exposed(d, p)) opts.push(b);
  }
  if (!opts.length) {                                      // jump a little to find more surface
    c.x += Math.round(rnd(-3, 3)); c.y += Math.round(rnd(-2, 2)); c.z += Math.round(rnd(-3, 3)); c.left -= 2; return;
  }
  const b = pick(opts); infect(d, b); c.x = b.location.x; c.y = b.location.y; c.z = b.location.z; c.left--;
}
system.runInterval(() => {
  if (!S.active || !cursors.length) return;
  for (let i = 0; i < S.budget && cursors.length; i++) {
    const idx = system.currentTick * 7 + i; const c = cursors[idx % cursors.length];
    try { stepCursor(c); } catch { c.left = 0; }
    if (c.left <= 0) cursors.splice(cursors.indexOf(c), 1);
  }
}, 1);

// ---------------------------------------------------------------- burrowed + sculk mass
const burrowed = new Map();   // entity id -> { until, d, loc, hp }
function burrow(victim) {
  if (!alive(victim) || HORDE.has(victim.typeId) || (victim instanceof Player && !survival(victim))) return;
  let hp = 20; try { hp = victim.getComponent("minecraft:health").effectiveMax; } catch {}
  const prev = burrowed.get(victim.id);
  burrowed.set(victim.id, { until: system.currentTick + CFG.burrowTicks, d: victim.dimension.id, loc: victim.location, hp, ent: victim });
  if (!prev) { try { victim.addEffect("slowness", CFG.burrowTicks, { amplifier: 0, showParticles: true }); } catch {} }
}
function burst(b) {
  const d = dim(b.d), p = b.loc;
  const water = block(d, p)?.isLiquid;
  spawn(d, `${NS}:${water ? "sculk_leech" : Math.random() < 0.25 ? "sculk_stinger" : "sculk_mite"}`, p);
  particle(d, "minecraft:sculk_charge_pop_particle", p); sound(d, "block.sculk_catalyst.bloom", p);
  placeMass(b.d, p, b.hp);
}
function placeMass(dimId, p, victimHealth) {
  const total = Math.round(victimHealth * CFG.healthAbsorb), tax = Math.round(total * CFG.tax);
  addMass(tax);
  if (!S.active) return;
  const d = dim(dimId), g = groundAbove(d, { x: p.x, y: p.y - 1, z: p.z }, 3);
  if (!g) return;
  const at = block(d, g); if (!at?.isAir) return;
  try { at.setType(`${NS}:sculk_mass`); } catch { return; }
  if (S.masses.length >= CFG.maxMasses) S.masses.shift();
  S.masses.push({ d: dimId, x: Math.floor(g.x), y: Math.floor(g.y), z: Math.floor(g.z), m: total - tax });
  addCursor(dimId, { x: g.x, y: g.y - 1, z: g.z }); save();
}
system.runInterval(() => {
  const now = system.currentTick;
  for (const [id, b] of burrowed) {
    if (alive(b.ent)) { b.loc = b.ent.location; b.d = b.ent.dimension.id; }
    if (now >= b.until) { burrowed.delete(id); if (alive(b.ent)) burst(b); }
  }
}, 10);
world.afterEvents.entityDie.subscribe(({ deadEntity }) => {
  const id = deadEntity.id, b = burrowed.get(id);
  if (b) { burrowed.delete(id); try { b.loc = deadEntity.location; } catch {} burst(b); }
  try { if (deadEntity.typeId === `${NS}:sculk_phantom`) spawn(deadEntity.dimension, `${NS}:sculk_phantom_corpse`, groundBelow(deadEntity)); } catch {}
});
function groundBelow(e) {
  const d = e.dimension, p = e.location;
  for (let y = Math.floor(p.y); y > p.y - 64; y--) { const b = block(d, { x: p.x, y: y - 1, z: p.z }); if (b === undefined) break; if (!b.isAir) return { x: p.x, y, z: p.z }; }
  return p;
}

// mass blocks spend their stored mass on reinforcements
const countNear = (d, id, p, r) => { try { return d.getEntities({ type: id, location: p, maxDistance: r }).length; } catch { return 99; } };
function hordeNear(d, p, r) { let n = 0; try { for (const e of d.getEntities({ location: p, maxDistance: r, families: ["sculkhorde"] })) n++; } catch { n = 99; } return n; }
function buy(d, p, budget, forMass) {
  const water = block(d, { x: p.x, y: p.y, z: p.z })?.isLiquid;
  const opts = FACTORY.filter((f) => f.stage <= S.stage && f.cost <= budget && (!forMass || f.mass) && (f.medium === "water") === !!water &&
    (!f.limit || countNear(d, f.id, p, 64) < f.limit));
  return opts.length ? pick(opts) : undefined;
}
system.runInterval(() => {
  if (!S.active || !S.masses.length) return;
  for (const m of [...S.masses]) {
    const d = dim(m.d), b = block(d, m);
    if (b === undefined) continue;                                           // not loaded: wait
    if (b.typeId !== `${NS}:sculk_mass`) { S.masses.splice(S.masses.indexOf(m), 1); save(); continue; }
    if (!d.getPlayers({ location: m, maxDistance: 96 }).length || hordeNear(d, m, 48) >= CFG.mobCapNear) continue;
    const f = buy(d, { x: m.x + 0.5, y: m.y, z: m.z + 0.5 }, m.m, true);
    if (!f) { b.setType("minecraft:sculk"); S.masses.splice(S.masses.indexOf(m), 1); save(); continue; }
    m.m -= f.cost; spawn(d, f.id, { x: m.x + 0.5, y: m.y + 0.2, z: m.z + 0.5 }); sound(d, "block.sculk.spread", m); save();
  }
}, CFG.massSpawnInterval);

// ---------------------------------------------------------------- nodes
function placeNode(dimId, p) {
  const d = dim(dimId), b = block(d, p); if (!b) return false;
  try { b.setType(`${NS}:sculk_node`); } catch { return false; }
  S.nodes.push({ d: dimId, x: Math.floor(p.x), y: Math.floor(p.y), z: Math.floor(p.z) }); S.lastNode = system.currentTick; save();
  for (let i = 0; i < 4; i++) addCursor(dimId, p, 64);
  world.sendMessage("§3A Sculk Node has taken root somewhere nearby...");
  return true;
}
function tryGrowNode() {
  if (S.stage < 1 || S.nodes.length >= CFG.maxNodes || system.currentTick - S.lastNode < CFG.nodeCooldownTicks || !S.masses.length) return;
  const m = pick(S.masses);
  if (S.nodes.some((n) => n.d === m.d && dist(n, m) < CFG.nodeMinSpacing)) return;
  const d = dim(m.d), y = Math.max(m.y - Math.round(rnd(8, 20)), m.d === "minecraft:overworld" ? -58 : 2);
  const b = block(d, { x: m.x, y, z: m.z }); if (!b || b.isAir || b.isLiquid || !INFECTABLE.test(b.typeId)) return;
  placeNode(m.d, { x: m.x, y, z: m.z });
}
system.runInterval(() => {
  if (!S.active) return;
  tryGrowNode();
  for (const n of [...S.nodes]) {
    const d = dim(n.d), b = block(d, n); if (b === undefined) continue;
    if (b.typeId !== `${NS}:sculk_node`) { S.nodes.splice(S.nodes.indexOf(n), 1); save(); continue; }
    if (cursors.length < CFG.maxCursors) addCursor(n.d, { x: n.x + rnd(-CFG.nodeRadius, CFG.nodeRadius) | 0, y: n.y + rnd(-4, 12) | 0, z: n.z + rnd(-CFG.nodeRadius, CFG.nodeRadius) | 0 }, 32);
    for (const p of d.getPlayers({ location: n, maxDistance: CFG.nodeDarkness })) if (survival(p)) try { p.addEffect("darkness", 140, { showParticles: false }); } catch {}
    if (S.mass >= CFG.nodeDefenderCost && d.getPlayers({ location: n, maxDistance: 64 }).length && hordeNear(d, n, 48) < CFG.mobCapNear) {
      const g = groundAbove(d, { x: n.x + rnd(-6, 6), y: n.y + 1, z: n.z + rnd(-6, 6) }, 12);
      const f = g && buy(d, g, CFG.nodeDefenderCost * 2, false);
      if (f) { addMass(-f.cost); spawn(d, f.id, g); }
    }
  }
}, 100);
world.afterEvents.playerBreakBlock.subscribe(({ brokenBlockPermutation: perm, block: b, player }) => {
  const t = perm.type.id;
  if (t === `${NS}:sculk_node`) {
    S.nodes = S.nodes.filter((n) => !(n.x === b.location.x && n.y === b.location.y && n.z === b.location.z)); addMass(-S.mass / 2);
    world.sendMessage(`§b${player.name} destroyed a Sculk Node! §3The Horde loses half its mass.`);
    sound(b.dimension, "mob.warden.death", b.location);
  } else if (t === `${NS}:sculk_mass`) {
    S.masses = S.masses.filter((m) => !(m.x === b.location.x && m.y === b.location.y && m.z === b.location.z)); save();
  }
});
world.afterEvents.playerInteractWithBlock.subscribe(({ block: b, player }) => {
  if (b.typeId !== `${NS}:sculk_ancient_node` || S.active) return;
  S.active = true; save();
  world.sendMessage(`§3${player.name} has awoken the Sculk Horde. §bThe Gravemind stirs...`);
  sound(b.dimension, "mob.warden.emerge", b.location);
  for (let i = 0; i < 6; i++) addCursor(b.dimension.id, b.location, 64);
  for (let i = 0; i < 3; i++) spawn(b.dimension, `${NS}:sculk_phantom`, { x: b.location.x + rnd(-8, 8), y: b.location.y + 20, z: b.location.z + rnd(-8, 8) });
});

// ---------------------------------------------------------------- combat: burrowing + mob abilities
world.afterEvents.entityHurt.subscribe(({ hurtEntity, damageSource }) => {
  const src = damageSource.damagingEntity; if (!src) return;
  let t; try { t = src.typeId; } catch { return; }
  if (BURROWERS.has(t)) burrow(hurtEntity);
  if (t === `${NS}:sculk_mite`) { burrow(hurtEntity); try { src.remove(); } catch {} }   // mites infect and die
});
const ABILITIES = {
  sculk_creeper(e, st) {
    const p = nearestPlayer(e, 3);
    st.fuse = p ? (st.fuse ?? 0) + 5 : 0;
    if (st.fuse >= 30) {
      const d = e.dimension, at = e.location; e.remove();
      try { d.createExplosion(at, 2.5, { breaksBlocks: false, causesFire: false }); } catch {}
      for (let i = 0; i < 4; i++) addCursor(d.id, { x: at.x, y: at.y - 1, z: at.z }, 24);
    } else if (st.fuse === 5) sound(e.dimension, "random.fuse", e.location);
  },
  sculk_spitter(e, st) {
    if ((st.cd = (st.cd ?? 0) - 5) > 0) return;
    const p = nearestPlayer(e, 16, 3); if (!p) return;
    const eye = { x: e.location.x, y: e.location.y + 1.6, z: e.location.z }, tgt = p.getHeadLocation();
    if (!los(e.dimension, eye, tgt)) return;
    st.cd = 40;
    for (let i = 1; i < 10; i++) particle(e.dimension, "minecraft:sculk_soul_particle",
      { x: eye.x + (tgt.x - eye.x) * i / 10, y: eye.y + (tgt.y - eye.y) * i / 10, z: eye.z + (tgt.z - eye.z) * i / 10 });
    try { p.applyDamage(4, { cause: EntityDamageCause.projectile, damagingEntity: e }); p.addEffect("poison", 60); } catch {}
    sound(e.dimension, "mob.llama.spit", e.location);
  },
  sculk_hatcher(e, st) {
    if ((st.cd = (st.cd ?? 200) - 5) > 0) return;
    if (!nearestPlayer(e, 16) || hordeNear(e.dimension, e.location, 24) > 12) return;
    st.cd = 600; for (let i = 0; i < 2; i++) spawn(e.dimension, `${NS}:sculk_mite`, e.location);
  },
  sculk_spore_spewer(e, st) {
    if ((st.cd = (st.cd ?? 0) - 5) > 0) return;
    st.cd = 100;
    for (const p of e.dimension.getPlayers({ location: e.location, maxDistance: 12 })) if (survival(p)) try { p.addEffect("darkness", 120, { showParticles: false }); p.addEffect("nausea", 100); } catch {}
    for (let i = 0; i < 6; i++) particle(e.dimension, "minecraft:sculk_charge_pop_particle", { x: e.location.x + rnd(-2, 2), y: e.location.y + 2.2, z: e.location.z + rnd(-2, 2) });
    if (S.active) addCursor(e.dimension.id, { x: e.location.x, y: e.location.y - 1, z: e.location.z }, 16);
  },
  sculk_phantom_corpse(e, st) {
    if (S.active && (st.cd = (st.cd ?? 0) - 5) <= 0) { st.cd = 200; addCursor(e.dimension.id, { x: e.location.x, y: e.location.y - 1, z: e.location.z }, 24); }
  },
  sculk_metamorphosis_pod(e, st) {
    st.age = (st.age ?? 0) + 5;
    if (st.age < 1200) return;
    const d = e.dimension, at = e.location; e.remove();
    spawn(d, `${NS}:${pick(S.stage >= 2 ? ["sculk_ravager", "sculk_witch", "sculk_vindicator"] : ["sculk_vindicator", "sculk_ravager"])}`, at);
    sound(d, "block.sculk_catalyst.bloom", at);
  },
  sculk_witch(e, st) {
    if ((st.cd = (st.cd ?? 0) - 5) > 0) return;
    const p = nearestPlayer(e, 10); if (!p || !los(e.dimension, { x: e.location.x, y: e.location.y + 2, z: e.location.z }, p.getHeadLocation())) return;
    st.cd = 80; try { p.addEffect(pick(["poison", "slowness", "weakness"]), 100); } catch {}
    particle(e.dimension, "minecraft:splash_spell_emitter", p.location); sound(e.dimension, "mob.witch.throw", e.location);
  },
  sculk_enderman(e, st) {
    if ((st.cd = (st.cd ?? 0) - 5) > 0) return;
    const p = nearestPlayer(e, 32, 7); if (!p) return;
    const g = groundAbove(e.dimension, { x: p.location.x + rnd(-3, 3), y: p.location.y - 1, z: p.location.z + rnd(-3, 3) }, 4); if (!g) return;
    st.cd = 160; particle(e.dimension, "minecraft:portal_reverse_particle", e.location);
    try { e.teleport(g, { facingLocation: p.location }); } catch {} sound(e.dimension, "mob.endermen.portal", g);
  },
  sculk_ghast(e, st) {
    if ((st.cd = (st.cd ?? 300) - 5) > 0) return;
    const p = nearestPlayer(e, 40); if (!p || hordeNear(e.dimension, p.location, 24) > 10) return;
    st.cd = 600; spawn(e.dimension, `${NS}:${pick(["sculk_zombie", "sculk_spitter", "sculk_mite_aggressor"])}`, e.location);
  },
};
const MEM = new Map();
let abilityTick = 0;
system.runInterval(() => {
  abilityTick++;
  const live = new Set();
  for (const d of ["minecraft:overworld", "minecraft:nether", "minecraft:the_end"].map(dim)) {
    for (const [name, fn] of Object.entries(ABILITIES)) {
      let list; try { list = d.getEntities({ type: `${NS}:${name}` }); } catch { continue; }
      for (const e of list) {
        live.add(e.id); const st = MEM.get(e.id) ?? {}; MEM.set(e.id, st);
        try { fn(e, st); } catch {}
      }
    }
  }
  if (abilityTick % 60 === 0) for (const id of MEM.keys()) if (!live.has(id)) MEM.delete(id);
}, 5);

// ---------------------------------------------------------------- commands
system.beforeEvents.startup.subscribe((ev) => {
  const reg = ev.customCommandRegistry, OP = CommandPermissionLevel.GameDirectors;
  const ok = (message) => ({ status: CustomCommandStatus.Success, message });
  const run = (fn) => (o) => { system.run(() => fn(o)); return ok(); };
  reg.registerCommand({ name: "sh:status", description: "Sculk Horde: show the horde's state", permissionLevel: OP }, () =>
    ok(`Horde ${S.active ? "§aACTIVE" : "§7dormant"}§r | stage ${STAGES[S.stage]} | mass ${S.mass} | nodes ${S.nodes.length} | masses ${S.masses.length} | cursors ${cursors.length} | budget ${S.budget}/tick`));
  reg.registerCommand({ name: "sh:start", description: "Sculk Horde: awaken the horde", permissionLevel: OP }, run(() => { S.active = true; save(); world.sendMessage("§3The Sculk Horde has awoken."); }));
  reg.registerCommand({ name: "sh:stop", description: "Sculk Horde: put the horde to sleep (spreading and spawning stop)", permissionLevel: OP }, run(() => { S.active = false; cursors.length = 0; save(); world.sendMessage("§7The Sculk Horde sleeps."); }));
  reg.registerCommand({ name: "sh:mass", description: "Sculk Horde: add (or remove) gravemind mass", permissionLevel: OP,
    mandatoryParameters: [{ name: "amount", type: CustomCommandParamType.Integer }] }, (o, n) => { system.run(() => addMass(n)); return ok(`mass ${S.mass + n}`); });
  reg.registerCommand({ name: "sh:budget", description: "Sculk Horde: blocks the infestation may convert per tick (0-64)", permissionLevel: OP,
    mandatoryParameters: [{ name: "blocks_per_tick", type: CustomCommandParamType.Integer }] }, (o, n) => { S.budget = Math.max(0, Math.min(64, n)); save(); return ok(`budget ${S.budget}/tick`); });
  reg.registerCommand({ name: "sh:node", description: "Sculk Horde: grow a Sculk Node under your feet", permissionLevel: OP }, run((o) => {
    const p = o.sourceEntity; if (p instanceof Player) placeNode(p.dimension.id, { x: p.location.x, y: p.location.y - 1, z: p.location.z });
  }));
  reg.registerCommand({ name: "sh:infect", description: "Sculk Horde: start 5 infestation cursors at your feet", permissionLevel: OP }, run((o) => {
    const p = o.sourceEntity; if (p instanceof Player) for (let i = 0; i < 5; i++) addCursor(p.dimension.id, { x: p.location.x, y: p.location.y - 1, z: p.location.z }, 48);
  }));
  reg.registerCommand({ name: "sh:clear", description: "Sculk Horde: remove every loaded horde mob and stop all cursors", permissionLevel: OP }, run(() => {
    cursors.length = 0;
    for (const d of ["minecraft:overworld", "minecraft:nether", "minecraft:the_end"].map(dim)) try { for (const e of d.getEntities({ families: ["sculkhorde"] })) e.remove(); } catch {}
  }));
});
