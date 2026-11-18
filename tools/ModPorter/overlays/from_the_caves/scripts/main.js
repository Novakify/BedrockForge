// Bedrock Forge - From The Caves behaviour overlay (v0, core loop)
// Original Script API implementation of the mod's mechanics. Entity ids come from the ModPorter shell pack.
//
//   PHASE 1 (default)  : "Behind" spawns right behind you and vanishes once seen;
//                        "Front" appears 5-7 blocks ahead, chases, hits once, vanishes.
//   PHASE 2            : after 15 total minutes below y=55. Nearby animals/villagers get possessed
//                        for 30 s, chase you; a Wooden Cross has a 9% chance per use to exorcise.
//   PHASE 3            : only via /ftc:phase 3 (the original's sleep trigger is effectively disabled).
// Debug: /ftc:phase <1|2|3>   /ftc:rate <multiplier>  (speeds up all spawn chances for testing)

import { world, system, Player, CommandPermissionLevel, CustomCommandParamType, CustomCommandStatus, EntityComponentTypes, ItemComponentTypes } from "@minecraft/server";

const NS = "from_the_caves";
const BELOW_Y = 55;
const PHASE2_TICKS = 18000;          // 15 min accumulated below BELOW_Y
const P_BEHIND = 4e-5, P_FRONT = 4e-5, P_POSSESS = 9e-5;   // per player per tick (original rates)
const POSSESS_RADIUS = 10, POSSESS_TICKS = 600, EXORCISE_CHANCE = 0.09, CROSS_COST = 2;
const POSSESS = {
  "minecraft:villager_v2": "possessed_v", "minecraft:villager": "possessed_v", "minecraft:pig": "possessed_p",
  "minecraft:cow": "possessed_c", "minecraft:sheep": "possessed_s", "minecraft:wolf": "possessed_w",
  "minecraft:chicken": "possessed_ckn", "minecraft:iron_golem": "possessed_ig", "minecraft:wandering_trader": "possessed_survivor",
};
const UNPOSSESS = Object.fromEntries(Object.entries(POSSESS).filter(([k]) => k !== "minecraft:villager").map(([k, v]) => [`${NS}:${v}`, k]));

// ---------------------------------------------------------------- phase state (persists in the world)
const phaseKey = (dim) => `ftc:phase:${dim.id}`;
const getPhase = (dim) => Number(world.getDynamicProperty(phaseKey(dim)) ?? 1);
function setPhase(dim, n) { world.setDynamicProperty(phaseKey(dim), n); console.warn(`[FTC] ${dim.id} -> phase ${n}`); }
let rate = Number(world.getDynamicProperty("ftc:rate") ?? 1);
const roll = (p) => Math.random() < p * rate;

// ---------------------------------------------------------------- helpers
const air = (b) => !b || b.isAir;
function clearSpot(dim, loc) {
  // find an air column near loc so a 2-tall mob fits (like the original's 3x3x3 search)
  for (let r = 0; r <= 3; r++) for (let dy = -r; dy <= r; dy++) for (let dx = -r; dx <= r; dx++) for (let dz = -r; dz <= r; dz++) {
    const p = { x: Math.floor(loc.x) + dx, y: Math.floor(loc.y) + dy, z: Math.floor(loc.z) + dz };
    try { if (air(dim.getBlock(p)) && air(dim.getBlock({ ...p, y: p.y + 1 }))) return { x: p.x + 0.5, y: p.y, z: p.z + 0.5 }; } catch {}
  }
  return null;
}
function facePlayer(mob, player) {
  const d = { x: player.location.x - mob.location.x, z: player.location.z - mob.location.z };
  mob.setRotation({ x: 0, y: Math.atan2(-d.x, d.z) * 180 / Math.PI });
}
function canSee(player, mob) {
  const eye = player.getHeadLocation(), to = { x: mob.location.x - eye.x, y: mob.location.y + 1.4 - eye.y, z: mob.location.z - eye.z };
  const len = Math.hypot(to.x, to.y, to.z); if (len < 0.01 || len > 64) return false;
  const v = player.getViewDirection(), dot = (v.x * to.x + v.y * to.y + v.z * to.z) / len;
  if (dot < 0.55) return false;                                          // outside the view cone
  const hit = player.dimension.getBlockFromRay(eye, { x: to.x / len, y: to.y / len, z: to.z / len }, { maxDistance: len, includeLiquidBlocks: false, includePassableBlocks: false });
  return !hit;                                                           // nothing solid in between
}
function sound(dim, id, loc, vol = 0.7, pitch = 0.95) { try { dim.playSound(`${NS}.${id}`, loc, { volume: vol, pitch }); } catch {} }

// ---------------------------------------------------------------- per player, every tick
function tickPlayer(p, now) {
  const dim = p.dimension; if (dim.id !== "minecraft:overworld") return;
  const phase = getPhase(dim), y = p.location.y;

  if (y <= BELOW_Y) {                                                    // phase 1 -> 2 timer
    const t = Number(p.getDynamicProperty("ftc:under55") ?? 0) + 1;
    p.setDynamicProperty("ftc:under55", t);
    if (t >= PHASE2_TICKS && phase === 1) { setPhase(dim, 2); p.setDynamicProperty("ftc:under55", 0); }
  }

  if (phase === 1 && y < BELOW_Y) {
    const v = p.getViewDirection(), flat = Math.hypot(v.x, v.z) || 1, fx = v.x / flat, fz = v.z / flat;
    if (roll(P_BEHIND)) {
      const at = clearSpot(dim, { x: p.location.x - fx * 1.0, y, z: p.location.z - fz * 1.0 });
      if (at) { const m = dim.spawnEntity(`${NS}:fromthecavesbehind`, at); m.setRotation(p.getRotation()); }
    }
    if (roll(P_FRONT)) {
      const d = 5 + Math.random() * 2, at = { x: p.location.x + fx * d, y: Math.floor(y), z: p.location.z + fz * d };
      for (let i = 0; i < 3; i++) { try { const b = dim.getBlock({ ...at, y: at.y + i }); if (b && !b.isAir && !b.isLiquid) b.setType("minecraft:air"); } catch {} }
      const m = dim.spawnEntity(`${NS}:fromthecavesfront`, { x: Math.floor(at.x) + 0.5, y: at.y, z: Math.floor(at.z) + 0.5 }); m.setRotation(p.getRotation());
    }
  }

  if (phase === 2 && roll(P_POSSESS)) {
    const near = dim.getEntities({ location: p.location, maxDistance: POSSESS_RADIUS }).filter((e) => POSSESS[e.typeId]);
    if (near.length) possess(near[Math.floor(Math.random() * near.length)], now);
  }
}

// ---------------------------------------------------------------- possession
function possess(target, now) {
  const dim = target.dimension, loc = target.location, rot = target.getRotation();
  const origType = target.typeId, name = target.nameTag;
  target.remove();
  const m = dim.spawnEntity(`${NS}:${POSSESS[origType]}`, loc);
  m.setRotation(rot);
  m.setDynamicProperty("ftc:revertAt", now + POSSESS_TICKS);
  m.setDynamicProperty("ftc:orig", origType);
  if (name) m.setDynamicProperty("ftc:name", name);
}
function unpossess(m, effects) {
  const orig = m.getDynamicProperty("ftc:orig") ?? UNPOSSESS[m.typeId]; if (!orig) return;
  const dim = m.dimension, loc = m.location, rot = m.getRotation(), name = m.getDynamicProperty("ftc:name");
  m.remove();
  const o = dim.spawnEntity(orig, { x: loc.x, y: loc.y + 0.1, z: loc.z });
  o.setRotation(rot); if (typeof name === "string") o.nameTag = name;
  if (effects) {
    try { dim.spawnParticle("minecraft:villager_happy", { x: loc.x, y: loc.y + 1, z: loc.z }); } catch {}
    try { dim.playSound("random.totem", loc); } catch {}
  }
}

// ---------------------------------------------------------------- per FTC entity, every tick
function tickMob(m, now, players) {
  const id = m.typeId.slice(NS.length + 1);
  if (id === "fromthecavesbehind") {
    // vanish the tick after any player has seen it
    const seen = Number(m.getDynamicProperty("ftc:seen") ?? 0);
    const nearest = players.filter((p) => p.dimension.id === m.dimension.id)
      .sort((a, b) => dist2(a.location, m.location) - dist2(b.location, m.location))[0];
    if (!seen) { if (players.some((p) => p.dimension.id === m.dimension.id && canSee(p, m))) m.setDynamicProperty("ftc:seen", now); else if (nearest) facePlayer(m, nearest); }
    else if (now - seen > 1) { sound(m.dimension, "appear_1", m.location, 0.7, 0.9); m.remove(); }
  } else if (UNPOSSESS[m.typeId]) {
    const at = Number(m.getDynamicProperty("ftc:revertAt") ?? 0);
    if (at && now >= at) unpossess(m, false);
  }
}
const dist2 = (a, b) => (a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2;

system.runInterval(() => {
  const now = system.currentTick, players = world.getAllPlayers();
  for (const p of players) { try { tickPlayer(p, now); } catch (e) { console.warn(`[FTC] player tick: ${e}`); } }
  const dims = new Set(players.map((p) => p.dimension.id));
  for (const d of dims) {
    for (const m of world.getDimension(d).getEntities({ families: [NS] })) { try { tickMob(m, now, players); } catch (e) { console.warn(`[FTC] mob tick: ${e}`); } }
  }
}, 1);

// Front mob: one hit, then gone
world.afterEvents.entityHurt.subscribe((ev) => {
  const src = ev.damageSource?.damagingEntity;
  if (src?.typeId === `${NS}:fromthecavesfront` && ev.hurtEntity instanceof Player) {
    sound(src.dimension, "appear_1", src.location, 0.7, 0.98);
    system.run(() => { try { src.remove(); } catch {} });
  }
});

// Wooden Cross exorcism
world.afterEvents.playerInteractWithEntity.subscribe((ev) => {
  const item = ev.itemStack; if (item?.typeId !== `${NS}:wooden_cross`) return;
  const target = ev.target; if (!UNPOSSESS[target.typeId]) return;
  if (Math.random() < EXORCISE_CHANCE) unpossess(target, true);
  // durability cost either way (like the original)
  const p = ev.player, inv = p.getComponent(EntityComponentTypes.Inventory)?.container; if (!inv) return;
  const slot = p.selectedSlotIndex, held = inv.getItem(slot); if (!held || held.typeId !== item.typeId) return;
  const dur = held.getComponent(ItemComponentTypes.Durability);
  if (dur) {
    if (dur.damage + CROSS_COST >= dur.maxDurability) { inv.setItem(slot, undefined); p.playSound("random.break"); }
    else { dur.damage += CROSS_COST; inv.setItem(slot, held); }
  }
});

// ---------------------------------------------------------------- debug commands
system.beforeEvents.startup.subscribe((ev) => {
  const reg = ev.customCommandRegistry;
  reg.registerCommand({ name: "ftc:phase", description: "From The Caves: show or set the phase", permissionLevel: CommandPermissionLevel.GameDirectors,
    optionalParameters: [{ name: "phase", type: CustomCommandParamType.Integer }] }, (origin, n) => {
      const dim = origin.sourceEntity?.dimension ?? world.getDimension("overworld");
      if (n === undefined) return { status: CustomCommandStatus.Success, message: `phase ${getPhase(dim)}` };
      if (![1, 2, 3].includes(n)) return { status: CustomCommandStatus.Failure, message: "phase must be 1, 2 or 3" };
      system.run(() => setPhase(dim, n)); return { status: CustomCommandStatus.Success, message: `phase -> ${n}` };
    });
  reg.registerCommand({ name: "ftc:rate", description: "From The Caves: multiply spawn chances (testing)", permissionLevel: CommandPermissionLevel.GameDirectors,
    mandatoryParameters: [{ name: "multiplier", type: CustomCommandParamType.Float }] }, (origin, x) => {
      rate = Math.max(0, x); system.run(() => world.setDynamicProperty("ftc:rate", rate));
      return { status: CustomCommandStatus.Success, message: `spawn rate x${rate}` };
    });
});

console.warn("[FTC] From The Caves behaviour overlay loaded (/ftc:phase, /ftc:rate)");
