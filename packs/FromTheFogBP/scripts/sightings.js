// From The Fog (Bedrock) - sightings: scheduler, presets, positioning, per-tick logic, spotting, vanish, scares (spec §2.1-2.13).
import { world, system, EntityDamageCause, BlockPermutation } from "@minecraft/server";
import {
  add, sub, scale, norm, dist, floorV, forwardOf, leftOf, yawTo, pitchTo, angleBetween, fovToAngle,
  randInt, pick, chance, DEFAULT_TABLE, COMMON_TABLE, isNight, ticks, players, playersNear, nearestPlayer,
  getBlock, isPassable, isLeaves, isSeeThrough, isReplaceable, GLASS_RX, debug,
} from "./util.js";
import { opt, on } from "./config.js";
import { state, activity, setActivity, grant } from "./state.js";
import { SKINS } from "./skins.js";

export const HEROBRINE = "ftf:herobrine";

// ---------------------------------------------------------------- presets (spec §2.2)
/** @typedef {{type:string, mode:string, max_distance:number, spot_fov:number, jumpscare:boolean, light_reactant:boolean, despawn:number, sneaky_strike:boolean, custom?:boolean, vanishing_delay?:number}} Preset */
/** @type {Record<string, Preset>} */
export const PRESETS = {
  creeping: { type: "creeping", mode: "creeping", max_distance: 16, spot_fov: 85, jumpscare: true, light_reactant: false, despawn: 30, sneaky_strike: true },
  stalking: { type: "stalking", mode: "stalking", max_distance: 50, spot_fov: 77, jumpscare: true, light_reactant: false, despawn: 30, sneaky_strike: false },
  lurking: { type: "lurking", mode: "lurking", max_distance: 128, spot_fov: 66, jumpscare: true, light_reactant: false, despawn: 30, sneaky_strike: false },
  dwelling: { type: "dwelling", mode: "dwelling", max_distance: 64, spot_fov: 75, jumpscare: true, light_reactant: false, despawn: 30, sneaky_strike: true },
  nightmare: { type: "nightmare", mode: "nightmare", max_distance: 16, spot_fov: 60, jumpscare: false, light_reactant: false, despawn: 60, sneaky_strike: false },
  ghost_miner: { type: "ghost_miner", mode: "still", max_distance: 16, spot_fov: 60, jumpscare: false, light_reactant: false, despawn: 60, sneaky_strike: false },
  shrine_surprise: { type: "shrine_surprise", mode: "still", max_distance: 16, spot_fov: 0, jumpscare: false, light_reactant: false, despawn: 2, sneaky_strike: false },
};

const DAY_RANGES = { insane: [15, 30], common: [30, 60], default: [60, 120], rare: [120, 240], scarce: [240, 360] };
const NIGHT_RANGES = { insane: [8, 15], common: [15, 30], default: [30, 60], rare: [60, 120], scarce: [120, 180] };
const VANISH_DELAY_KEYS = { creeping: "creeping_vanishing_delay", dwelling: "dwelling_vanishing_delay", stalking: "stalking_vanishing_delay", lurking: "lurking_vanishing_delay", nightmare: "nightmare_vanishing_delay", ghost_miner: "ghost_miner_vanishing_delay" };

// ---------------------------------------------------------------- per-sighting state
/**
 * @typedef {Object} Sighting
 * @property {Preset} preset
 * @property {import("@minecraft/server").Entity} entity
 * @property {string} dim
 * @property {string} targetId player the sighting was rolled for
 * @property {boolean} spotted
 * @property {boolean} facePlayer
 * @property {boolean} lightReactant
 * @property {number} slip 0 none, 1 left, -1 right
 * @property {{x:number,y:number,z:number}} slipVec per-tick side step
 * @property {boolean} sense sighting-sense active
 * @property {boolean} signRolled
 * @property {boolean} spottedSign
 * @property {boolean} suspensefulSpotted
 * @property {boolean} teleportJumpscared
 * @property {number|undefined} glassRemove pending window-watcher removal handle
 * @property {number[]} runs scheduled runs to cancel on cleanup
 * @property {string} skin
 * @property {boolean} removing
 * @property {string|undefined} nightmarePlayer
 */
/** @type {Sighting|undefined} */
export let cur;
let timerHandle;
let retryCount = 0;
let lastPreset = "creeping";

export function rollChance(table, optionId) {
  const v = String(opt(optionId) ?? "default");
  const n = Math.max(1, Math.floor((table[v] ?? table.default) / activity()));
  return randInt(1, n) === 1;
}
export const rollDefault = (optionId) => rollChance(DEFAULT_TABLE, optionId);
export const rollCommon = (optionId) => rollChance(COMMON_TABLE, optionId);

function later(fn, t) {
  const h = system.runTimeout(fn, t);
  cur?.runs.push(h);
  return h;
}

export function canSeeSky(dim, loc) {
  try {
    const top = dim.getTopmostBlock({ x: Math.floor(loc.x), z: Math.floor(loc.z) });
    return !top || top.y < Math.floor(loc.y) + 1;
  } catch { return true; }
}
export function isUnderground(player) {
  const y = player.location.y;
  return y >= -64 && y <= 55 && !canSeeSky(player.dimension, player.getHeadLocation());
}

// ---------------------------------------------------------------- scheduler (spec §2.1)
let timerRolled = false;
export function sightingTimerTick() {
  if (cur || !state.started_events || timerRolled) return;
  const r = randInt(1, 7);
  const type = r === 1 ? "creeping" : r <= 3 ? "stalking" : r <= 5 ? "lurking" : "dwelling";
  if (opt(`${type}_sighting`) !== true) return; // retry next second
  const freq = String(opt("sighting_frequency"));
  const [a, b] = (isNight() ? NIGHT_RANGES : DAY_RANGES)[freq] ?? DAY_RANGES.default;
  const secs = Math.max(1, Math.floor(randInt(a, b) / activity()));
  timerRolled = true;
  debug(`next sighting: ${type} in ${secs}s`);
  timerHandle = system.runTimeout(() => { timerHandle = undefined; summonSighting(type); }, secs * 20);
}

let raining = false;
export function setRaining(v) { raining = v; }

function summonSighting(type) {
  const w = String(opt("sighting_window"));
  const night = isNight();
  if ((w === "day" && night) || (w === "night" && !night) || (w === "rain" && !raining) || !state.started_events) { cleanup(); return; }
  if (world.getDimension("overworld").getEntities({ type: "ftf:door_ghost" }).length > 0 ||
      world.getDimension("nether").getEntities({ type: "ftf:door_ghost" }).length > 0) { cleanup(); return; }
  const p = pick(players());
  if (!p) { cleanup(); return; }
  runPreset(type, p);
}

/** Preset entry with the dimension / underground routing table of spec §2.2. */
export function runPreset(type, player) {
  const d = player.dimension.id;
  const nether = d === "minecraft:nether", end = d === "minecraft:the_end";
  const under = isUnderground(player);
  let t = type;
  if (type === "stalking" || type === "lurking") t = end ? type : (nether || under) ? "dwelling" : type;
  else if (type === "dwelling") t = (nether || under) ? "dwelling" : "stalking";
  lastPreset = type;
  return sighting(PRESETS[t] ?? PRESETS.creeping, player);
}

// ---------------------------------------------------------------- positioning (spec §2.3)
function randomSpread(player, dMin, dMax, sMin, sMax, front) {
  const yaw = player.getRotation().y;
  const sign = front ? (randInt(0, 1) === 1 ? 1 : -1) : -1;
  const lat = randInt(sMin, sMax) * (randInt(0, 1) ? 1 : -1);
  const fwd = forwardOf(yaw), left = leftOf(yaw);
  return add(player.location, add(scale(fwd, sign * randInt(dMin, dMax)), scale(left, lat)));
}

/** Descend up to 32 blocks to the first passable cell above a solid block (spec §2.3 ground correction). */
export function groundCorrect(dim, loc) {
  let p = floorV(loc);
  for (let i = 0; i < 40; i++) {
    const b = getBlock(dim, p);
    if (!b) return undefined;
    if (b.isLiquid || /kelp|seagrass/.test(b.typeId)) return undefined;
    const below = getBlock(dim, { x: p.x, y: p.y - 1, z: p.z });
    if (!below) return undefined;
    const pause = /farmland|grass_path|dirt_path/.test(below.typeId);
    if (!isPassable(b) || (isPassable(below) && !pause && !below.isLiquid)) { p = { x: p.x, y: p.y - 1, z: p.z }; continue; }
    if (below.isLiquid) return undefined;
    return { x: p.x + 0.5, y: p.y, z: p.z + 0.5 };
  }
  return undefined;
}

function surfaceAt(dim, loc) {
  try {
    let top = dim.getTopmostBlock({ x: Math.floor(loc.x), z: Math.floor(loc.z) });
    let guard = 0;
    while (top && (isLeaves(top) || isPassable(top)) && guard++ < 48) top = top.below();
    if (!top || top.isLiquid) return undefined;
    return { x: top.x + 0.5, y: top.y + 1, z: top.z + 0.5 };
  } catch { return undefined; }
}

function findBed(player) {
  for (const off of [0, -1]) {
    const b = getBlock(player.dimension, add(player.location, { x: 0, y: off, z: 0 }));
    if (b && b.typeId === "minecraft:bed") return b;
  }
  return undefined;
}

/** @returns {{loc:any, lightReactant:boolean}|undefined} */
function position(preset, player, at) {
  const dim = player.dimension;
  switch (preset.mode) {
    case "creeping": {
      const p = randomSpread(player, 3, 5, 2, 3, false);
      const g = groundCorrect(dim, add(p, { x: 0, y: 7, z: 0 }));
      return g && { loc: g, lightReactant: false };
    }
    case "dwelling": {
      const p = randomSpread(player, 6, 12, 0, 18, true);
      const g = groundCorrect(dim, add(p, { x: 0, y: 7, z: 0 }));
      if (!g) return undefined;
      let lr = false;
      if (opt("dwelling_shadow") === true && dim.id !== "minecraft:nether") {
        try { lr = dim.getLightLevel(floorV(add(g, { x: 0, y: 1, z: 0 }))) < 8; } catch {}
      }
      return { loc: g, lightReactant: lr };
    }
    case "stalking": case "lurking": {
      const p = preset.mode === "stalking" ? randomSpread(player, 25, 46, 0, 35, true) : randomSpread(player, 50, 100, 0, 50, true);
      const s = surfaceAt(dim, p);
      return s && { loc: s, lightReactant: false };
    }
    case "nightmare": {
      const bed = findBed(player);
      if (!bed) return undefined;
      // Bedrock bed "direction": 0 south, 1 west, 2 north, 3 east (foot -> head)
      const dir = Number(bed.permutation.getState("direction") ?? 0);
      const off = [{ x: 0, z: -2 }, { x: 2, z: 0 }, { x: 0, z: 2 }, { x: -2, z: 0 }][dir] ?? { x: 0, z: 2 };
      const loc = { x: bed.x + 0.5 + off.x, y: bed.y, z: bed.z + 0.5 + off.z };
      return { loc, lightReactant: false };
    }
    case "still":
    default:
      return { loc: at ?? player.location, lightReactant: false };
  }
}

// ---------------------------------------------------------------- spawning (spec §2.2 flow)
function chooseSkin() {
  if (opt("hoebrine_returns") === true) return 0; // hoebrine model has no Bedrock port; falls back to default
  const list = (opt("herobrine_skin") || []).filter((/** @type {string} */ s) => SKINS.some((k) => k.id === s));
  const id = list.length ? pick(list) : "default";
  return Math.max(0, SKINS.findIndex((k) => k.id === id));
}

/**
 * @param {Preset} preset
 * @param {import("@minecraft/server").Player} player
 * @param {any} [at] explicit position for "still" sightings
 */
export function sighting(preset, player, at) {
  removeHerobrine(false);
  if (retryCount >= 5) { retryCount = 0; cleanup(); debug("sighting gave up after 5 retries", false); return undefined; }
  const pos = position(preset, player, at);
  const dim = player.dimension;
  const tooFar = pos && !preset.custom && preset.mode !== "still" && preset.mode !== "nightmare" &&
    !players().some((p) => p.dimension.id === dim.id && dist(p.location, pos.loc) >= 0.4 && dist(p.location, pos.loc) <= preset.max_distance);
  if (!pos || tooFar) {
    retryCount++;
    debug(`${preset.type} position failed, retry ${retryCount}`, false);
    system.runTimeout(() => { if (!cur) sighting(preset, player, at); }, 2);
    return undefined;
  }
  retryCount = 0;
  let entity;
  try { entity = dim.spawnEntity(HEROBRINE, pos.loc); } catch (e) { debug(`spawn failed ${e}`, false); cleanup(); return undefined; }
  const skinIdx = chooseSkin();
  const skin = SKINS[skinIdx];
  entity.setProperty("ftf:skin", skinIdx);
  entity.setProperty("ftf:emissive", opt("emissive_layers") !== false);
  const pants = opt("from_the_pants") === true;
  entity.setProperty("ftf:hide_arms", pants || !skin.arms);
  entity.setProperty("ftf:hide_legs", !skin.legs);
  entity.setProperty("ftf:hide_body", pants || !skin.body);
  entity.triggerEvent(opt("no_sleep") === true ? "ftf:no_sleep_on" : "ftf:no_sleep_off");
  const nt = String(opt("nametag"));
  if (nt === "dynamic") entity.nameTag = skin.name;
  else if (nt === "cryptic") entity.nameTag = "§k" + skin.name;
  const near = nearestPlayer(dim, pos.loc) ?? player;
  entity.teleport(pos.loc, { facingLocation: near.location });

  cur = {
    preset, entity, dim: dim.id, targetId: player.id, spotted: false, facePlayer: true, lightReactant: preset.light_reactant || pos.lightReactant,
    slip: 0, slipVec: { x: 0, y: 0, z: 0 }, sense: false, signRolled: false, spottedSign: false, suspensefulSpotted: false, teleportJumpscared: false,
    glassRemove: undefined, runs: [], skin: skin.id, removing: false, nightmarePlayer: preset.type === "nightmare" ? player.id : undefined,
  };
  const despawnT = ticks(preset.despawn);
  later(() => despawn(), despawnT);
  if (preset.sneaky_strike && opt("sneaky_strike") === true && preset.despawn > 3) later(() => { if (rollDefault("sneaky_strike_chance")) punch(); }, despawnT - 60);
  if (opt("sighting_sense") === true && rollCommon("sighting_sense_chance")) cur.sense = true;
  debug(`sighting ${preset.type} (${skin.id}) at ${Math.round(pos.loc.x)} ${Math.round(pos.loc.y)} ${Math.round(pos.loc.z)}${cur.lightReactant ? " [shadow]" : ""}${cur.sense ? " [sense]" : ""}`);
  return entity;
}

// ---------------------------------------------------------------- cleanup / removal (spec §2.6)
export function cleanup() {
  if (cur) for (const h of cur.runs) system.clearRun(h);
  if (cur?.glassRemove !== undefined) system.clearRun(cur.glassRemove);
  cur = undefined;
  timerRolled = false;
  if (timerHandle !== undefined) { system.clearRun(timerHandle); timerHandle = undefined; }
}

/** Kill every Herobrine entity (no drops, no death animation). */
export function removeHerobrine(doCleanup = true) {
  for (const id of ["overworld", "nether", "the_end"]) {
    for (const e of world.getDimension(id).getEntities({ type: HEROBRINE })) { try { e.remove(); } catch {} }
  }
  if (doCleanup) cleanup();
}

function specter(dim, loc) {
  const mode = String(opt("shrouded_specter"));
  if (mode === "off") return;
  try { dim.spawnParticle(`ftf:${mode}`, add(loc, { x: 0, y: 0.5, z: 0 })); } catch {}
}

/** models/remove */
export function remove() {
  const s = cur;
  if (!s || s.removing) return;
  const e = s.entity;
  if (!e?.isValid) { cleanup(); return; }
  // GHC teleport jumpscare
  if (opt("give_him_control") === true && !s.teleportJumpscared && chance(1)) {
    const fixated = players().find((p) => p.dimension.id === s.dim && p.getEntitiesFromViewDirection({ maxDistance: 128 })[0]?.entity?.id === e.id);
    if (fixated) {
      s.teleportJumpscared = true;
      const f = forwardOf(fixated.getRotation().y);
      e.teleport(add(fixated.location, f), { facingLocation: fixated.location });
      s.spotted = false;
      suddenScare(fixated);
      return;
    }
  }
  s.removing = true;
  const loc = e.location, dim = e.dimension;
  if (s.spottedSign) placeSign(dim, loc, e.getRotation().y);
  if (s.slip === 0) specter(dim, loc);
  if (s.nightmarePlayer) world.getEntity(s.nightmarePlayer)?.removeTag("ftf:having_nightmare");
  cleanup();
  try { e.remove(); } catch {}
}

/** models/despawn: timer ran out without being seen */
function despawn() {
  if (!cur) return;
  const e = cur.entity;
  if (e?.isValid && !cur.spotted) { const p = nearestPlayer(e.dimension, e.location); if (p) grant(p, "missed_herobrine"); }
  cur.spottedSign = false;
  remove();
}

// ---------------------------------------------------------------- punch / scares (spec §2.8, §2.9)
export function punch() {
  const e = cur?.entity;
  if (!e?.isValid) return;
  const p = nearestPlayer(e.dimension, e.location, 6);
  if (!p) return;
  e.teleport(e.location, { facingLocation: p.location });
  e.setProperty("ftf:anim", "punch");
  later(() => { if (e.isValid) e.setProperty("ftf:anim", "idle"); }, 15);
  later(() => { try { p.applyDamage(1, { cause: EntityDamageCause.entityAttack, damagingEntity: e }); } catch {} }, 4);
}

/** sudden_scare/init */
export function suddenScare(player) {
  const s = cur;
  if (!s) return;
  const e = s.entity;
  s.spotted = true;
  grant(player, "get_jumpscared");
  if (opt("malicious_malfunction") === true) {
    jumpscare(player);
    player.playSound("ftf.events.sudden_scare.crash");
    // Harmless replacement for the Java client crash: black fade + fake error title.
    system.runTimeout(() => {
      try { player.camera.fade({ fadeTime: { fadeInTime: 0.05, holdTime: 3, fadeOutTime: 0.5 }, fadeColor: { red: 0, green: 0, blue: 0 } }); } catch {}
      player.onScreenDisplay.setTitle("§4Unknown Entity Error", { subtitle: "§7Minecraft has detected an unknown presence in this world.", fadeInDuration: 0, stayDuration: 60, fadeOutDuration: 10 });
    }, 3);
    return;
  }
  if (opt("sudden_scare") === true) { jumpscare(player); return; }
  remove();
}

function jumpscare(player) {
  const e = cur?.entity;
  if (!e?.isValid) { remove(); return; }
  try { player.teleport(player.location, { facingLocation: e.getHeadLocation() }); } catch {}
  if (opt("give_him_control") === true) later(() => punch(), 2);
  e.dimension.playSound("ftf.events.sudden_scare.jumpscare", e.location);
  later(() => remove(), 10);
  setActivity(2, 360, 300);
}

// ---------------------------------------------------------------- signs (spec §2.16)
const SIGN_MSG = {
  local: [["", "I'm watching...", "", ""], ["", "Help", "", ""], ["", "I see you.", "", ""], ["", "Hello there,", "@p", ""], ["", "WAKE UP.", "", ""],
    ["", "You are not", "alone.", ""], ["", "Listen closely.", "", ""], ["", "Do you hear it", "too?", ""], ["", "Stop.", "", ""],
    ["", "Do I frighten", "you?", ""], ["", "You can't escape", "this nightmare.", ""], ["What are you", "looking at", "candlehead?", ""]],
  swedish: [["", "Jag ser dig...", "", ""], ["", "Hjälp", "", ""], ["", "Jag ser dig.", "", ""], ["", "Hallå där,", "@p", ""], ["", "VAKNA.", "", ""],
    ["", "Du är inte ensam.", "", ""], ["", "Lyssna noga.", "", ""], ["Hör du det", "också?", "", ""], ["", "Stopp.", "", ""],
    ["", "Skrämmer jag dig?", "", ""], ["Du kan inte fly", "denna", "mardrömm.", ""], ["", "Vad glor du på,", "stakskalle?", ""]],
};
export function woodForBiome(dim, loc) {
  if (opt("biome_boards") !== true) return "oak";
  let id = "";
  try { id = dim.getBiome(loc).id; } catch {}
  if (/taiga/.test(id)) return "spruce";
  if (/birch/.test(id)) return "birch";
  if (/jungle/.test(id)) return "jungle";
  if (/savanna/.test(id)) return "acacia";
  if (/roofed_forest|dark_forest/.test(id)) return "darkoak";
  if (/mangrove/.test(id)) return "mangrove";
  if (/cherry/.test(id)) return "cherry";
  if (/crimson_forest/.test(id)) return "crimson";
  if (/warped_forest/.test(id)) return "warped";
  return "oak";
}
function signRotation(yaw) {
  let y = ((yaw + 180) % 360 + 360) % 360 - 180;
  if (y >= 160 || y < -159) return 8;
  if (y >= 120) return 6; if (y >= 70) return 4; if (y >= 30) return 2; if (y >= -29) return 0;
  if (y >= -69) return 14; if (y >= -119) return 12; return 10;
}
export function placeSign(dim, loc, yaw) {
  const b = getBlock(dim, loc);
  if (!isReplaceable(b) || !b) return;
  const wood = woodForBiome(dim, loc);
  const id = wood === "oak" ? "minecraft:standing_sign" : `minecraft:${wood}_standing_sign`;
  const lang = String(opt("lurking_language")) === "swedish" ? "swedish" : "local";
  const msg = [...pick(SIGN_MSG[lang])];
  const p = nearestPlayer(dim, loc);
  for (let i = 0; i < 4; i++) if (msg[i] === "@p") msg[i] = p ? p.name : "";
  try {
    b.setPermutation(BlockPermutation.resolve(id, { ground_sign_direction: signRotation(yaw) }));
    const sign = b.getComponent("minecraft:sign");
    sign?.setText(msg.join("\n"));
    sign?.setWaxed(true);
  } catch (e) { debug(`sign failed ${e}`, false); }
}

function holdItem(e, item) {
  try {
    e.runCommand(`replaceitem entity @s slot.weapon.mainhand 0 ${item}`);
    e.setProperty("ftf:holding", true);
  } catch {}
}
export function holdIronPickaxe() { const e = cur?.entity; if (e?.isValid) holdItem(e, "iron_pickaxe"); }

// ---------------------------------------------------------------- spot detection (spec §2.5)
/**
 * Line of sight from the player's eyes to a target point via getBlockFromRay, passing through see-through blocks.
 * @returns {{visible:boolean, glass:boolean}}
 */
export function lineOfSight(player, target) {
  const dim = player.dimension;
  let origin = player.getHeadLocation();
  const total = dist(origin, target);
  const dir = norm(sub(target, origin));
  let travelled = 0, glass = false;
  for (let i = 0; i < 12; i++) {
    const remaining = total - travelled;
    if (remaining <= 0.3) return { visible: true, glass };
    let hit;
    try { hit = dim.getBlockFromRay(origin, dir, { maxDistance: remaining, includeLiquidBlocks: false, includePassableBlocks: false }); } catch { return { visible: false, glass }; }
    if (!hit) return { visible: true, glass };
    const hp = add(hit.block.location, hit.faceLocation);
    const d = dist(origin, hp);
    if (d >= remaining - 0.3) return { visible: true, glass };
    if (!isSeeThrough(hit.block)) return { visible: false, glass };
    if (GLASS_RX.test(hit.block.typeId)) glass = true;
    // step through this block
    let step = d + 0.05;
    const bl = hit.block.location;
    while (step < remaining) {
      const pt = add(origin, scale(dir, step));
      if (Math.floor(pt.x) !== bl.x || Math.floor(pt.y) !== bl.y || Math.floor(pt.z) !== bl.z) break;
      step += 0.05;
    }
    travelled += step;
    origin = add(origin, scale(dir, step));
  }
  return { visible: false, glass };
}

function spotPlayerTick(player) {
  const s = cur;
  if (!s) return;
  const e = s.entity;
  if (!e?.isValid || player.dimension.id !== s.dim) return;
  const eye = e.getHeadLocation();
  const d = dist(player.location, e.location);
  if (d > 80) return;
  const view = player.getViewDirection();
  const ang = angleBetween(view, sub(eye, player.getHeadLocation()));
  // suspenseful sighting sting (0.40 cone ~ 23.1 deg)
  const sus = String(opt("suspenseful_sighting"));
  if (sus !== "off" && s.preset.spot_fov > 0 && s.spotted && !s.suspensefulSpotted && ang <= fovToAngle(40)) {
    s.suspensefulSpotted = true;
    let snd = "default";
    if (sus === "dynamic" && s.skin === "slenderman") snd = "slenderman";
    player.playSound(`ftf.events.suspenseful_sighting.${snd}`);
  }
  if (s.spotted || s.preset.spot_fov <= 0) return;
  if (ang > fovToAngle(s.preset.spot_fov)) return;
  const los = lineOfSight(player, add(eye, { x: 0, y: -0.1, z: 0 }));
  if (!los.visible) return;
  grant(player, "first_encounter");
  if (opt("from_the_pants") === true) grant(player, "encounter_pants");
  if (s.lightReactant) return; // dwelling shadow: cannot be spotted
  spotted(player, los.glass && opt("window_watcher") === true);
}

function spotted(player, throughGlass) {
  const s = cur;
  if (!s) return;
  if (!throughGlass) {
    s.spotted = true;
    if (opt("give_him_control") === true) setActivity(2, 360, 300);
    vanish(player);
    return;
  }
  // Window watcher: linger until the player looks away.
  if (s.glassRemove !== undefined) system.clearRun(s.glassRemove);
  s.glassRemove = system.runTimeout(() => { if (cur === s) remove(); }, 4);
  if (!s.signRolled) {
    s.signRolled = true;
    if (opt("sinister_signs") === true && rollDefault("sinister_signs_chance")) {
      later(() => {
        const e = s.entity;
        if (!e?.isValid) return;
        const wood = woodForBiome(e.dimension, e.location);
        holdItem(e, wood === "oak" ? "oak_sign" : wood === "darkoak" ? "dark_oak_sign" : `${wood}_sign`);
        s.spottedSign = true;
      }, 40);
    }
  }
}

function vanish(player) {
  const s = cur;
  if (!s) return;
  const e = s.entity;
  if (!e?.isValid) { cleanup(); return; }
  const noise = String(opt("sighting_noise"));
  if (noise !== "off" && chance(50)) {
    const at = add(e.location, { x: 0, y: 8, z: 0 });
    if (noise === "ghast") e.dimension.playSound("mob.ghast.scream", at, { volume: 0.7, pitch: 1 });
    else e.dimension.playSound("ambient.cave", at, { volume: 0.7, pitch: 0.1 });
  }
  // slip behind (spec §2.6.3)
  const yaw = e.getRotation().y;
  const f = forwardOf(yaw), l = leftOf(yaw);
  const base = add(floorV(e.location), { x: 0.5, y: 1.5, z: 0.5 });
  const solid = (v) => { const b = getBlock(e.dimension, v); return !!b && !isSeeThrough(b); };
  const free = (v) => isPassable(getBlock(e.dimension, v));
  for (const side of [1, -1]) {
    const lat = scale(l, side);
    const wall = add(base, add(lat, f));
    if (solid(wall) && solid(add(wall, { x: 0, y: -1, z: 0 })) && free(add(base, add(lat, { x: 0, y: 1, z: 0 }))) && free(add(base, lat))) {
      s.slip = side;
      s.facePlayer = false;
      e.setProperty("ftf:anim", "walk");
      s.slipVec = scale(lat, 0.1);
      e.setRotation({ x: 0, y: yawTo({ x: 0, y: 0, z: 0 }, lat) });
      later(() => remove(), 14);
      return;
    }
  }
  const key = s.preset.custom ? undefined : VANISH_DELAY_KEYS[s.preset.type];
  let delay = key ? Number(opt(key)) : (s.preset.vanishing_delay ?? 0.5);
  if (!Number.isFinite(delay)) delay = 0.5;
  if (opt("give_him_control") === true && (s.preset.type === "lurking" || s.preset.type === "stalking") &&
      playersNear(e.dimension, e.location, 20).length === 0 && chance(1)) {
    later(() => { if (e.isValid) e.setProperty("ftf:tilt", true); }, 20);
    delay = 3;
  }
  later(() => remove(), ticks(delay));
}

// ---------------------------------------------------------------- per-tick Herobrine logic (spec §2.4)
export function herobrineTick() {
  const s = cur;
  if (!s) return;
  const e = s.entity;
  if (!e?.isValid) { cleanup(); return; }
  const dim = e.dimension;
  const loc = e.location;
  if (s.slip !== 0) {
    try { e.teleport(add(loc, s.slipVec), { rotation: e.getRotation() }); } catch {}
  } else if (s.facePlayer) {
    const p = nearestPlayer(dim, loc);
    if (p) {
      const yaw = yawTo(loc, p.location);
      const pitch = s.preset.type === "nightmare" ? 15 : pitchTo(e.getHeadLocation(), p.location);
      try { e.setRotation({ x: pitch, y: yaw }); } catch {}
    }
  }
  if (s.removing) return;
  const head = getBlock(dim, add(loc, { x: 0, y: 1, z: 0 }));
  const feet = getBlock(dim, loc);
  const inLiquid = !!feet?.isLiquid;
  const headBlocked = !!head && (!isPassable(head) || isLeaves(head)) && !head.isLiquid;
  const anyNear = playersNear(dim, loc, s.preset.max_distance).length > 0;
  if ((headBlocked && s.preset.mode !== "still") || inLiquid || !anyNear) { remove(); return; }
  if (s.lightReactant) {
    const close = playersNear(dim, loc, 3);
    if (close.length) { for (const p of close) grant(p, "encounter_dwelling_shadow"); remove(); return; }
    try { if (dim.getLightLevel(floorV(add(loc, { x: 0, y: 1, z: 0 }))) >= 8) s.lightReactant = false; } catch {}
  }
  // nightmare ends when the player leaves the bed
  if (s.nightmarePlayer) {
    const p = world.getEntity(s.nightmarePlayer);
    if (!p || !(/** @type {any} */ (p).isSleeping)) { remove(); return; }
  }
  // proximity jumpscare (player tick in Java)
  if (s.preset.jumpscare && !s.spotted) {
    const p = playersNear(dim, loc, 2)[0];
    if (p) { suddenScare(p); return; }
  }
  for (const p of players()) spotPlayerTick(p);
}

// ---------------------------------------------------------------- sighting sense (spec §2.12), once a second
export function senseTick() {
  const s = cur;
  if (!s?.sense || opt("sighting_sense") !== true) return;
  const e = s.entity;
  if (!e?.isValid) return;
  for (const wolf of e.dimension.getEntities({ type: "minecraft:wolf", location: e.location, maxDistance: 80 })) {
    if (!wolf.hasComponent("minecraft:is_tamed")) continue;
    try { wolf.setRotation({ x: 0, y: yawTo(wolf.location, e.location) }); } catch {}
    if (chance(35)) e.dimension.playSound("mob.wolf.growl", wolf.location, { volume: 0.8 });
    for (const p of playersNear(wolf.dimension, wolf.location, 15)) grant(p, "encounter_dog_sense");
  }
}

// ---------------------------------------------------------------- nightmare (spec §2.10), polled once a second
const sleeping = new Set();
export function nightmareTick() {
  for (const p of world.getAllPlayers()) {
    const asleep = /** @type {any} */ (p).isSleeping === true;
    if (asleep && !sleeping.has(p.id)) {
      sleeping.add(p.id);
      if (state.started_events && opt("nightmare_sighting") === true && !cur) {
        const freq = String(opt("sighting_frequency"));
        const n = { common: 25, default: 100, rare: 200, scarce: 500 }[freq];
        if (n === undefined || randInt(1, n) === 1) {
          const ent = sighting(PRESETS.nightmare, p);
          if (ent) { p.addTag("ftf:having_nightmare"); grant(p, "experience_nightmare"); }
        }
      }
    } else if (!asleep) sleeping.delete(p.id);
  }
}

// ---------------------------------------------------------------- player hits / shots (spec §2.9)
export function onHerobrineHit(player) {
  if (!cur) { removeHerobrine(); return; }
  try { player.applyDamage(2, { cause: EntityDamageCause.selfDestruct }); } catch {}
  grant(player, "punch_herobrine");
  remove();
  setActivity(4, 360, 300);
}
export function onHerobrineShot() {
  remove();
  setActivity(4, 360, 300);
}

/** Custom sighting API (spec §2.23). */
export function customSighting(player, args) {
  const preset = {
    type: "custom", custom: true, mode: String(args.position ?? "still"), max_distance: Number(args.max_distance ?? 64),
    spot_fov: Number(args.spot_fov ?? 70), jumpscare: args.jumpscare === true || args.jumpscare === "true",
    light_reactant: args.light_reactant === true || args.light_reactant === "true", despawn: Number(args.despawn_timer ?? 30),
    sneaky_strike: args.sneaky_strike === true || args.sneaky_strike === "true", vanishing_delay: Number(args.vanishing_delay ?? 0.5),
  };
  if (timerHandle !== undefined) { system.clearRun(timerHandle); timerHandle = undefined; }
  const e = sighting(preset, player);
  if (e && cur && args.face_player === false) cur.facePlayer = false;
  return e;
}
export function retryLast(player) { return runPreset(lastPreset, player); }
export function hasSighting() { return !!cur; }
export function sightingEntity() { return cur?.entity; }
