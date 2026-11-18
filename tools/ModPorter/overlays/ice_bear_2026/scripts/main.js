// Bedrock Forge - The Dweller (ice_bear_2026) behaviour, original Script API implementation.
// Mirrors the Java mod's design (MIT, by Cadentem):
//   * per-dimension SPAWN timer (640-870 s, 40% chance of a 1200 s cooldown) and cave NOISE timer (460-580 s);
//     spawns invisibly 16+ blocks from you, never more than one at a time, leaves after 300 s
//   * look straight at it (dot > 0.99 + line of sight) -> it targets you and ROLLS: chase / stare / stare / flee
//   * get within 12 blocks -> CHASE.  STARE: freezes while you watch, creeps closer when you look away;
//     after 4-13 looks it may vanish or switch to chase/flee.  CHASE: leaves 18 s in if you're not facing it.
//     FLEE: stares 10 ticks, sprints away, vanishes once you look away.
//   * crouches/crawls through 2- and 1-block gaps (hitbox + animation), immune to suffocation; teleports when stuck
// Debug: /ib:timer  /ib:fast_forward <spawn_seconds> <noise_seconds>  /ib:spawn

import { world, system, Player, GameMode, CommandPermissionLevel, CustomCommandParamType, CustomCommandStatus } from "@minecraft/server";

const NS = "ice_bear_2026", ID = `${NS}:${NS}`;
const CFG = {
  spawnMin: 640, spawnMax: 870, cooldownChance: 0.4, cooldown: 1200,   // seconds
  noiseMin: 460, noiseMax: 580,
  leave: 300, leaveChase: 18, spawnChancePerTick: 0.005, maxAmount: 1, spawnDistance: 16,
  spottingRange: 64, tooClose: 12, dimensions: ["minecraft:overworld"],
};
const T = (sec) => Math.round(sec * 20);
const rnd = (a, b) => a + Math.floor(Math.random() * (b - a + 1));

// ---------------------------------------------------------------- per-dimension timers (persist in the world)
const TIMERS = new Map();   // in-memory, saved to the world once a second
function timer(dimId) {
  if (TIMERS.has(dimId)) return TIMERS.get(dimId);
  let t;
  const raw = world.getDynamicProperty(`ib:timer:${dimId}`);
  if (typeof raw === "string") { try { t = JSON.parse(raw); } catch {} }
  if (!t) { t = { spawn: 0, noise: 0, targetSpawn: 0, targetNoise: 0, victim: undefined }; resetSpawn(t); resetNoise(t); }
  TIMERS.set(dimId, t); return t;
}
const saveTimer = (dimId, t) => world.setDynamicProperty(`ib:timer:${dimId}`, JSON.stringify(t));
function resetSpawn(t) { t.spawn = 0; t.targetSpawn = Math.random() <= CFG.cooldownChance ? T(CFG.cooldown) : T(rnd(CFG.spawnMin, CFG.spawnMax)); }
function resetNoise(t) { t.noise = 0; t.targetNoise = T(rnd(CFG.noiseMin, CFG.noiseMax + 1)); }

// ---------------------------------------------------------------- helpers
const PASSABLE = /torch|flower|tulip|orchid|allium|bluet|daisy|poppy|dandelion|short_grass|tall_grass|fern|vine|button|lever|rail|carpet|sign|banner|sapling|mushroom|redstone_wire|cobweb|snow_layer|candle|pressure_plate|tripwire|ladder|dead_bush|sugar_cane|seagrass|kelp|lichen|moss_carpet|air|light_block|structure_void/;
function solid(dim, p) {
  try { const b = dim.getBlock({ x: Math.floor(p.x), y: Math.floor(p.y), z: Math.floor(p.z) }); return !!b && !b.isAir && !b.isLiquid && !PASSABLE.test(b.typeId); }
  catch { return false; }
}
const validTarget = (p) => p?.isValid && p.getGameMode() !== GameMode.Creative && p.getGameMode() !== GameMode.Spectator;
const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
function lookDot(player, mob) {
  const eye = player.getHeadLocation(), v = player.getViewDirection();
  const m = { x: mob.location.x - eye.x, y: mob.location.y + 2.3 - eye.y, z: mob.location.z - eye.z }, l = Math.hypot(m.x, m.y, m.z) || 1;
  return (v.x * m.x + v.y * m.y + v.z * m.z) / l;
}
function lineOfSight(player, mob) {
  const eye = player.getHeadLocation(), to = { x: mob.location.x - eye.x, y: mob.location.y + 2.3 - eye.y, z: mob.location.z - eye.z };
  const l = Math.hypot(to.x, to.y, to.z); if (l < 0.5) return true;
  const hit = player.dimension.getBlockFromRay(eye, { x: to.x / l, y: to.y / l, z: to.z / l }, { maxDistance: l, includeLiquidBlocks: false, includePassableBlocks: false });
  return !hit;
}
// Java isLookingAtMe(target, directly): range check, dot > 0.3 (facing) or > 0.99 + LOS (directly)
function isLookingAtMe(player, mob, directly) {
  if (!validTarget(player) || dist(player.getHeadLocation(), mob.location) > CFG.spottingRange) return false;
  const d = lookDot(player, mob);
  return directly ? d > 0.99 && lineOfSight(player, mob) : d > 0.3;
}
function sound(mob, id, vol = 3) { try { mob.dimension.playSound(`${NS}.${id}`, mob.location, { volume: vol }); } catch {} }

// ---------------------------------------------------------------- per-dweller AI state (in memory; re-derived after reload)
const AI = new Map();
function brain(m) {
  let b = AI.get(m.id);
  if (!b) {
    b = { roll: "stroll", target: undefined, leave: Number(m.getDynamicProperty("ib:leave") ?? T(CFG.leave)), chaseLeft: T(CFG.leaveChase),
          chaseSoundClock: 0, playedFlee: false, playedSpotted: false, fleeDelay: 10, fleeLeave: 20, lookedAt: 0, lookedAtMax: rnd(4, 13),
          wasNotLooking: false, stuck: 0, pose: "stand", state: "calm" };
    AI.set(m.id, b);
  }
  return b;
}
function setState(m, b, s) { if (b.state !== s) { b.state = s; m.triggerEvent(`${NS}:state_${s}`); } }
function setPose(m, b, p) { if (b.pose !== p) { b.pose = p; m.triggerEvent(`${NS}:pose_${p}`); } }
function pickRoll(b, rolls) { b.roll = rolls[Math.floor(Math.random() * rolls.length)]; }
function disappear(m) { sound(m, "disappear"); AI.delete(m.id); try { m.remove(); } catch {} }

function tickDweller(m, players) {
  const b = brain(m), dim = m.dimension;
  if (--b.leave <= 0) return disappear(m);
  if (b.leave % 100 === 0) m.setDynamicProperty("ib:leave", b.leave);

  // ---- targeting (TargetTooClose / TargetSeesMe goals) ----
  const near = players.filter((p) => p.dimension.id === dim.id && validTarget(p)).sort((a, c) => dist(a.location, m.location) - dist(c.location, m.location));
  if (b.target && !validTarget(b.target)) { if (b.roll === "chase") return disappear(m); b.target = undefined; b.roll = "stroll"; }
  if (near[0] && dist(near[0].location, m.location) <= CFG.tooClose && b.roll !== "chase") { b.target = near[0]; b.roll = "chase"; }
  if (!b.target) {
    const seer = near.find((p) => isLookingAtMe(p, m, true));
    if (seer) { b.target = seer; m.setProperty(`${NS}:spotted`, true); pickRoll(b, ["chase", "stare", "stare", "flee"]); }
  }
  const tgt = b.target, facing = !!tgt && isLookingAtMe(tgt, m, false);

  // spawned invisible -> becomes visible as soon as nobody is facing it (BreakInvis goal)
  if (m.getEffect("invisibility") && !facing && !near.some((p) => isLookingAtMe(p, m, false))) m.removeEffect("invisibility");
  if (m.getProperty(`${NS}:spotted`) && !b.playedSpotted) { sound(m, "spotted"); b.playedSpotted = true; }

  // ---- crouch / crawl through gaps (same block checks as the Java tick) ----
  const pos = m.location, yaw = m.getRotation().y * Math.PI / 180, f = { x: -Math.sin(yaw), z: Math.cos(yaw) };
  const fx = Math.round(f.x), fz = Math.round(f.z), up = (k) => solid(dim, { x: pos.x, y: pos.y + k, z: pos.z });
  const facingSolid = solid(dim, { x: pos.x + fx, y: pos.y, z: pos.z + fz }), oy = facingSolid ? 1 : 0;
  const off = (k) => solid(dim, { x: pos.x + fx, y: pos.y + oy + k, z: pos.z + fz });
  const crawl = up(1) || (!off(0) && off(1)) || (facingSolid && up(2));
  const crouch = !crawl && (up(2) || (!off(0) && !off(1) && (off(2) || (facingSolid && up(3)))));

  // ---- rolls ----
  let frozen = false;
  if (b.roll === "stare" && tgt) {
    const looking = facing && lineOfSight(tgt, m);
    if (b.wasNotLooking && looking) b.lookedAt++;
    if (b.lookedAt > b.lookedAtMax) {
      if (!looking && Math.random() < 0.1) return disappear(m);
      if (Math.random() < 0.3) pickRoll(b, ["chase", "flee"]);
    }
    frozen = looking; b.wasNotLooking = !looking;
    if (looking) m.clearVelocity();
    try { m.lookAt(tgt.getHeadLocation()); } catch {}
    setState(m, b, "stare");
  } else if (b.roll === "chase" && tgt) {
    if (--b.chaseLeft <= 0 && !facing) return disappear(m);
    if (--b.chaseSoundClock <= 0 && lineOfSight(tgt, m)) { sound(m, "chase"); b.chaseSoundClock = T(5); }
    m.setProperty(`${NS}:spotted`, false);
    setState(m, b, "chase");
  } else if (b.roll === "flee" && tgt) {
    if (--b.fleeLeave < 0 && !facing) return disappear(m);
    if (--b.fleeDelay > 0) { try { m.lookAt(tgt.getHeadLocation()); } catch {} frozen = true; setState(m, b, "stare"); }
    else { if (!b.playedFlee) { sound(m, "flee"); b.playedFlee = true; } m.setProperty(`${NS}:spotted`, false); setState(m, b, "flee"); }
  } else {
    setState(m, b, "calm");
  }
  setPose(m, b, crawl ? "crawl" : crouch ? "crouch" : frozen ? "frozen" : "stand");

  // ---- stuck in blocks: after ~6 checks teleport near the target, otherwise leave (Java hit counter) ----
  if (up(0) && up(1) && !crawl) {
    if (++b.stuck > 6) {
      b.stuck = 0;
      if (tgt) {
        const a = Math.random() * Math.PI * 2, r = 8 + Math.random() * 8, base = tgt.location;
        for (let dy = 6; dy >= -6; dy--) {
          const p = { x: base.x + Math.cos(a) * r, y: Math.floor(base.y) + dy, z: base.z + Math.sin(a) * r };
          if (solid(dim, { ...p, y: p.y - 1 }) && !solid(dim, p) && !solid(dim, { ...p, y: p.y + 1 }) && !solid(dim, { ...p, y: p.y + 2 })) { m.teleport(p); return; }
        }
      }
      return disappear(m);
    }
  } else b.stuck = 0;
}

// ---------------------------------------------------------------- spawner + cave noises (per dimension, each tick)
function standable(dim, p) {
  return solid(dim, { ...p, y: p.y - 1 }) && !solid(dim, p) && !solid(dim, { ...p, y: p.y + 1 }) && !solid(dim, { ...p, y: p.y + 2 });
}
function trySpawn(dim, victim, players) {
  for (let i = 0; i < 40; i++) {
    const p = { x: Math.floor(victim.location.x) + rnd(-35, 35) + 0.5, y: Math.floor(victim.location.y) + rnd(-6, 6), z: Math.floor(victim.location.z) + rnd(-35, 35) + 0.5 };
    if (!standable(dim, p)) continue;
    if (players.some((pl) => pl.dimension.id === dim.id && dist(pl.location, p) < CFG.spawnDistance)) continue;
    try {
      const m = dim.spawnEntity(ID, p);
      m.addEffect("invisibility", 20 * 600, { showParticles: false });
      m.setDynamicProperty("ib:leave", T(CFG.leave));
      return m;
    } catch {}
  }
  return undefined;
}
function caveNoise(dim, players, victim) {
  const id = `${NS}.cavenoise`;
  for (const p of players) if (p.dimension.id === dim.id) { try { p.playSound(id, { location: victim.location, volume: 2 }); } catch {} }
}

system.runInterval(() => {
  const players = world.getAllPlayers();
  for (const dimId of CFG.dimensions) {
    const dim = world.getDimension(dimId), here = players.filter((p) => p.dimension.id === dimId && validTarget(p) && p.location.y <= 300);
    const dwellers = dim.getEntities({ type: ID });
    for (const m of dwellers) { try { tickDweller(m, players); } catch (e) { console.warn(`[Dweller] ${e}`); } }
    if (!here.length) continue;
    const t = timer(dimId);
    let victim = here.find((p) => p.id === t.victim);
    if (!victim) { victim = here[Math.floor(Math.random() * here.length)]; t.victim = victim.id; }
    t.spawn++; t.noise++;
    if (t.noise >= t.targetNoise && (dwellers.length > 0 || t.spawn >= T(CFG.spawnMax) / 2)) { caveNoise(dim, here, victim); resetNoise(t); }
    if (t.spawn >= t.targetSpawn && dwellers.length < CFG.maxAmount && Math.random() <= CFG.spawnChancePerTick) {
      if (trySpawn(dim, victim, players)) { caveNoise(dim, here, victim); resetSpawn(t); }
      else t.victim = undefined;
    }
    if (system.currentTick % 20 === 0) saveTimer(dimId, t);
  }
}, 1);

world.afterEvents.entityRemove.subscribe((ev) => AI.delete(ev.removedEntityId));

// ---------------------------------------------------------------- debug commands (operators)
system.beforeEvents.startup.subscribe((ev) => {
  const reg = ev.customCommandRegistry, OP = CommandPermissionLevel.GameDirectors;
  reg.registerCommand({ name: "ib:timer", description: "The Dweller: show spawn/noise timers", permissionLevel: OP }, (o) => {
    const d = o.sourceEntity?.dimension?.id ?? "minecraft:overworld", t = timer(d);
    return { status: CustomCommandStatus.Success, message: `spawn ${(t.spawn / 20).toFixed(0)}/${(t.targetSpawn / 20).toFixed(0)} s, noise ${(t.noise / 20).toFixed(0)}/${(t.targetNoise / 20).toFixed(0)} s` };
  });
  reg.registerCommand({ name: "ib:fast_forward", description: "The Dweller: add seconds to the spawn and noise timers", permissionLevel: OP,
    mandatoryParameters: [{ name: "spawn_seconds", type: CustomCommandParamType.Integer }, { name: "noise_seconds", type: CustomCommandParamType.Integer }] },
    (o, s, n) => {
      const d = o.sourceEntity?.dimension?.id ?? "minecraft:overworld";
      system.run(() => { const t = timer(d); t.spawn += T(s); t.noise += T(n); saveTimer(d, t); });
      return { status: CustomCommandStatus.Success, message: `+${s}s spawn, +${n}s noise` };
    });
  reg.registerCommand({ name: "ib:spawn", description: "The Dweller: try to spawn one near you now", permissionLevel: OP }, (o) => {
    const p = o.sourceEntity; if (!(p instanceof Player)) return { status: CustomCommandStatus.Failure, message: "run as a player" };
    system.run(() => { const m = trySpawn(p.dimension, p, world.getAllPlayers()); p.sendMessage(m ? "§7Something is here." : "§cNo valid spawn spot nearby."); });
    return { status: CustomCommandStatus.Success };
  });
});

console.warn("[Dweller] Bedrock Forge port loaded (/ib:timer, /ib:fast_forward, /ib:spawn)");
