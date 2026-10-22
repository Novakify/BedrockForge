// From The Fog (Bedrock) - world events: block-event framework (§2.18-2.19), footsteps (§2.15), ghost miner (§2.14),
// haunted herd (§2.13), shrine (§2.17), dreadful donation (§2.19a), ghost doors, structures (§2.21, §4).
import { world, system, ItemStack, BlockPermutation, BlockVolume } from "@minecraft/server";
import {
  add, sub, scale, floorV, forwardOf, leftOf, yawTo, dist, angleBetween, fovToAngle, randInt, pick, players, playersNear, nearestPlayer,
  getBlock, isLoaded, isPassable, loadJSON, saveJSON, debug, job,
} from "./util.js";
import { opt } from "./config.js";
import { state, startEvents, setActivity, grant } from "./state.js";
import { rollDefault, rollCommon, removeHerobrine, sighting, PRESETS, isUnderground, holdIronPickaxe, canSeeSky, lineOfSight } from "./sightings.js";

// ---------------------------------------------------------------- stored block positions
/** @typedef {{x:number,y:number,z:number,yaw:number,pitch:number,dim:string}} Pos */
const MAX_POS = 200;
/** @param {string} tag @returns {Pos[]} */
export const getPositions = (tag) => loadJSON(`ftf:pos:${tag}`, []);
/** @param {string} tag @param {Pos[]} list */
const setPositions = (tag, list) => saveJSON(`ftf:pos:${tag}`, list.slice(0, MAX_POS));

/** @param {string} tag @param {Pos} pos */
export function storePosition(tag, pos) {
  const list = getPositions(tag).filter((p) => !(p.x === pos.x && p.y === pos.y && p.z === pos.z && p.dim === pos.dim));
  list.unshift(pos);
  setPositions(tag, list);
}

const MATCH = {
  bed: (id) => id === "minecraft:bed",
  candle: (id) => /candle$/.test(id),
  chest: (id) => id === "minecraft:chest",
  door: (id) => /_door$/.test(id) && !/trapdoor/.test(id),
  lantern: (id) => id === "minecraft:lantern" || id === "minecraft:soul_lantern",
  torch: (id) => id === "minecraft:torch",
  fire: (id) => id === "minecraft:fire",
  forest: () => true,
};
export function tagForBlock(id) {
  for (const [tag, fn] of Object.entries(MATCH)) if (tag !== "forest" && tag !== "fire" && fn(id)) return tag;
  return undefined;
}

/** Remove entries whose block no longer matches (only checks loaded chunks). */
function prune(tag) {
  const list = getPositions(tag).filter((p) => {
    if (tag === "forest") return true;
    const dim = world.getDimension(p.dim);
    if (!isLoaded(dim, p)) return true;
    const b = getBlock(dim, p);
    if (!b) return true;
    return tag === "fire" ? isShrine(dim, p) : MATCH[tag](b.typeId);
  });
  setPositions(tag, list);
  return list;
}

/** events/random/init: pick a random loaded entry and run the executable. */
function randomEvent(option, tag, fn) {
  const list = prune(tag).filter((p) => isLoaded(world.getDimension(p.dim), p));
  if (!list.length) return;
  const p = pick(list);
  try { fn(world.getDimension(p.dim), p); } catch (e) { debug(`${option} failed: ${e}`, false); }
}

const noOneWithin = (dim, loc, r) => playersNear(dim, loc, r).length === 0;

// Replace matching blocks in a cube around a point, spread over ticks.
function* cubeJob(dim, c, r, filterTypes, mapper) {
  for (let y0 = c.y - r; y0 <= c.y + r; y0 += 8) {
    const vol = new BlockVolume({ x: c.x - r, y: Math.max(y0, dim.heightRange.min), z: c.z - r }, { x: c.x + r, y: Math.min(y0 + 7, c.y + r, dim.heightRange.max - 1), z: c.z + r });
    let list;
    try { list = dim.getBlocks(vol, { includeTypes: filterTypes }, true); } catch { yield; continue; }
    for (const loc of list.getBlockLocationIterator()) {
      try { const b = dim.getBlock(loc); if (b) mapper(b); } catch {}
    }
    yield;
  }
}

const TORCHES = ["minecraft:torch"];
const CANDLES = ["candle", "white_candle", "orange_candle", "magenta_candle", "light_blue_candle", "yellow_candle", "lime_candle", "pink_candle", "gray_candle",
  "light_gray_candle", "cyan_candle", "purple_candle", "blue_candle", "brown_candle", "green_candle", "red_candle", "black_candle"].map((c) => "minecraft:" + c);

// ---------------------------------------------------------------- executables (§2.19)
function burningBase(dim, p) {
  const below = getBlock(dim, { x: p.x, y: p.y - 1, z: p.z });
  if (!below || !/(planks|wool|log|wood|leaves|carpet|bookshelf|hay|_slab|_stairs|fence|scaffolding|bamboo|dried_kelp|target|beehive|bee_nest|composter|lectern|barrel|chest|crafting_table)/.test(below.typeId)) return;
  if (!noOneWithin(dim, p, 50) || !rollDefault("burning_base_chance")) return;
  for (let dx = -1; dx <= 1; dx++) for (let dy = 0; dy <= 1; dy++) for (let dz = -1; dz <= 1; dz++) {
    const b = getBlock(dim, { x: p.x + dx, y: p.y + dy, z: p.z + dz });
    if (b?.isAir) b.setType("minecraft:fire");
  }
  debug("burning base");
}
function disappearingTorches(dim, p) {
  if (!noOneWithin(dim, p, 50) || !rollDefault("disappearing_torches_chance")) return;
  job(cubeJob(dim, p, 25, TORCHES, (b) => b.setType("minecraft:air")));
  debug("disappearing torches");
}
function crimsonCurse(dim, p, lanterns) {
  if (!noOneWithin(dim, p, 50) || !rollDefault("crimson_curse_chance")) return;
  job(cubeJob(dim, p, 25, TORCHES, (b) => {
    const facing = b.permutation.getState("torch_facing_direction");
    b.setPermutation(BlockPermutation.resolve("minecraft:redstone_torch", { torch_facing_direction: /** @type {any} */ (facing ?? "top") }));
  }));
  if (lanterns && opt("soul_shift") === true) job(cubeJob(dim, p, 25, ["minecraft:lantern"], (b) => {
    const hanging = b.permutation.getState("hanging");
    b.setPermutation(BlockPermutation.resolve("minecraft:soul_lantern", { hanging: !!hanging }));
  }));
  debug("crimson curse");
}
function chilledCandles(dim, p) {
  if (!noOneWithin(dim, p, 50) || !rollDefault("chilled_candles_chance")) return;
  job(cubeJob(dim, p, 25, CANDLES, (b) => { if (b.permutation.getState("lit")) b.setPermutation(b.permutation.withState("lit", false)); }));
  debug("chilled candles");
}
function leaflessGrove(dim, p) {
  if (!noOneWithin(dim, p, 78) || !rollDefault("leafless_grove_chance")) return;
  const f = forwardOf(p.yaw);
  for (let i = 0; i < 3; i++) {
    const c = floorV(add(p, scale(f, 50 * i)));
    if (!isLoaded(dim, c)) break;
    job(groveJob(dim, c));
  }
  debug("leafless grove");
}
function* groveJob(dim, c) {
  const r = 25, ry = 50;
  for (let y0 = c.y - ry; y0 <= c.y + ry; y0 += 6) {
    const lo = Math.max(y0, dim.heightRange.min), hi = Math.min(y0 + 5, c.y + ry, dim.heightRange.max - 1);
    if (lo > hi) continue;
    const vol = new BlockVolume({ x: c.x - r, y: lo, z: c.z - r }, { x: c.x + r, y: hi, z: c.z + r });
    let list;
    try { list = dim.getBlocks(vol, { includeTypes: LEAVES, includeTags: ["log"] }, true); } catch { yield; continue; }
    for (const loc of list.getBlockLocationIterator()) {
      try {
        const b = dim.getBlock(loc);
        if (!b) continue;
        const border = Math.abs(loc.x - c.x) >= r - 1 || Math.abs(loc.z - c.z) >= r - 1;
        if (/_leaves$/.test(b.typeId)) { if (!b.permutation.getState("persistent_bit")) b.setType("minecraft:air"); }
        else if (border) b.setType("minecraft:air");
      } catch {}
    }
    yield;
  }
}
const LEAVES = ["oak_leaves", "spruce_leaves", "birch_leaves", "jungle_leaves", "acacia_leaves", "dark_oak_leaves", "mangrove_leaves", "cherry_leaves", "azalea_leaves", "azalea_leaves_flowered", "pale_oak_leaves"].map((x) => "minecraft:" + x);

// ---------------------------------------------------------------- dreadful donation (§2.19a)
const DONATION_NAMES = { local: ["Help.", "I see you.", "You are not alone."], swedish: ["Hjälp.", "Jag ser dig.", "Du är inte ensam."] };
function dreadfulDonation(dim, p) {
  if (!noOneWithin(dim, p, 25) || !rollDefault("dreadful_donation_chance")) return;
  const inv = getBlock(dim, p)?.getComponent("minecraft:inventory");
  const container = inv?.container;
  if (!container) return;
  const lang = String(opt("lurking_language")) === "swedish" ? "swedish" : "local";
  const heir = opt("haunted_heirlooms") === true;
  const r = randInt(1, 190 + (heir ? 5 : 0));
  let item;
  if (r <= 70 || r <= 100) {
    item = new ItemStack(r <= 70 ? "minecraft:redstone_torch" : "minecraft:music_disc_11", 1);
    item.nameTag = pick(DONATION_NAMES[lang]);
    item.setLore(["§8ftf:named_donation"]);
  } else if (r <= 190) {
    item = new ItemStack(pick(["minecraft:mossy_cobblestone", "minecraft:rotten_flesh", "minecraft:sandstone"]), 1);
    item.setLore(["§8ftf:donation"]);
  } else {
    item = new ItemStack(pick(["ftf:disc_herobrine_lullaby", "ftf:disc_deathbed", "ftf:disc_hes_from_the_fog"]), 1);
    item.setLore(["§8ftf:heirloom"]);
  }
  container.addItem(item);
  debug("dreadful donation");
}

/** Called when a player opens a chest: grant donation achievements. */
export function checkDonationChest(player, block) {
  const c = block.getComponent("minecraft:inventory")?.container;
  if (!c) return;
  for (let i = 0; i < c.size; i++) {
    const lore = c.getItem(i)?.getLore().join(" ") ?? "";
    if (lore.includes("ftf:")) grant(player, "get_donation");
    if (lore.includes("ftf:named_donation")) grant(player, "get_named_donation");
    if (lore.includes("ftf:heirloom")) grant(player, "get_haunted_heirloom");
  }
}

// ---------------------------------------------------------------- ghost doors
let ghostTimer;
function ghostDoors(dim, p) {
  if (!rollDefault("ghost_doors_chance")) return;
  removeGhosts();
  removeHerobrine();
  const spot = add({ x: p.x + 0.5, y: p.y, z: p.z + 0.5 }, scale(forwardOf(p.yaw), -2));
  try { dim.spawnEntity("ftf:door_ghost", spot); } catch { return; }
  ghostTimer = system.runTimeout(removeGhosts, 600);
  debug("ghost door");
}
export function removeGhosts() {
  if (ghostTimer !== undefined) { system.clearRun(ghostTimer); ghostTimer = undefined; }
  for (const d of ["overworld", "nether", "the_end"]) for (const g of world.getDimension(d).getEntities({ type: "ftf:door_ghost" })) { try { g.remove(); } catch {} }
}
export function doorGhostTick() {
  for (const d of ["overworld", "nether", "the_end"]) for (const g of world.getDimension(d).getEntities({ type: "ftf:door_ghost" })) {
    const b = getBlock(g.dimension, g.location);
    if (b && (/_door$/.test(b.typeId) || b.typeId === "minecraft:bed")) { try { g.remove(); } catch {} }
  }
}

// ---------------------------------------------------------------- fearful footsteps (§2.15)
export function fearfulFootsteps(player) {
  if (!rollCommon("fearful_footsteps_chance")) return;
  removeHerobrine();
  const yaw = player.getRotation().y;
  const f = forwardOf(yaw), l = leftOf(yaw);
  const starts = [scale(l, 4), scale(l, -4), scale(l, 6), scale(l, -6), scale(f, -4)];
  const startOff = pick(starts);
  const start = add(player.location, startOff);
  const under = getBlock(player.dimension, add(start, { x: 0, y: -1, z: 0 }));
  if (!under) return;
  const id = under.typeId;
  let snd;
  if (id === "minecraft:grass_block") snd = "step.grass";
  else if (/deepslate/.test(id)) snd = "step.deepslate";
  else if (/^minecraft:(stone|granite|diorite|andesite|tuff|polished_granite|polished_diorite|polished_andesite)$/.test(id)) snd = "step.stone";
  else if (/^minecraft:(sand|red_sand)$/.test(id)) snd = "step.sand";
  else if (/planks$/.test(id)) snd = "step.wood";
  if (!snd) return;
  const dim = player.dimension;
  for (let i = 0; i < 4; i++) {
    system.runTimeout(() => dim.playSound(snd, add(start, scale(f, i)), { volume: 0.2 }), i * 6);
  }
  debug(`fearful footsteps (${snd})`);
}

// ---------------------------------------------------------------- ghost miner (§2.14)
const SURROUNDINGS = /^minecraft:(stone|granite|diorite|andesite|tuff|deepslate|dirt|gravel|.*_ore|cobbled_deepslate)$/;
const MINEABLE = /^minecraft:(stone|granite|diorite|andesite|tuff|deepslate|dirt|gravel|.*_ore|cobbled_deepslate|calcite|water|flowing_water|cobblestone|coarse_dirt|clay|smooth_basalt)$/;
export function ghostMiner(player) {
  if (!isUnderground(player)) return;
  const dim = player.dimension;
  const yaw = player.getRotation().y;
  const l = leftOf(yaw);
  const eye = floorV(player.getHeadLocation());
  for (const side of [1, -1]) for (const dy of [0, -1]) {
    const b = getBlock(dim, add(add(eye, scale(l, side)), { x: 0, y: dy, z: 0 }));
    if (!b || !SURROUNDINGS.test(b.typeId)) return;
  }
  if (!rollDefault("ghost_miner_chance")) return;
  for (const p of playersNear(dim, player.location, 10)) grant(p, "encounter_ghost_miner");
  const side = randInt(0, 1) ? 1 : -1;
  const dir = scale(l, side);
  // snap to an axis so the tunnel is straight
  const ax = Math.abs(dir.x) > Math.abs(dir.z) ? { x: Math.sign(dir.x), y: 0, z: 0 } : { x: 0, y: 0, z: Math.sign(dir.z) };
  const start = add(floorV(player.location), scale(ax, 5));
  job(digTunnel(dim, start, ax));
  const minerPos = { x: start.x + ax.x + 0.5, y: start.y, z: start.z + ax.z + 0.5 };
  const e = sighting(PRESETS.ghost_miner, player, minerPos);
  if (!e) return;
  system.runTimeout(() => holdIronPickaxe(), 20);
  const at = () => (e.isValid ? e.location : minerPos);
  for (const t of [12, 16, 20, 24, 34, 38, 42, 46]) system.runTimeout(() => dim.playSound("hit.stone", at(), { volume: 0.1, pitch: 0.5 }), t);
  for (const t of [28, 50]) system.runTimeout(() => dim.playSound("dig.stone", at(), { volume: 0.1, pitch: 1 }), t);
  debug("ghost miner");
}
function* digTunnel(dim, start, ax) {
  const perp = { x: ax.z, y: 0, z: ax.x };
  for (let seg = 0; seg < 20; seg++) {
    for (let i = 0; i < 5; i++) {
      const c = add(start, scale(ax, seg * 5 + i));
      if (!isLoaded(dim, c)) return;
      for (const dy of [0, 1]) {
        const b = getBlock(dim, add(c, { x: 0, y: dy, z: 0 }));
        if (b && MINEABLE.test(b.typeId)) b.setType("minecraft:air");
      }
      // plug liquids in walls/ceiling and holes in the floor
      for (const o of [perp, scale(perp, -1)]) for (const dy of [0, 1]) {
        const w = getBlock(dim, add(c, add(o, { x: 0, y: dy, z: 0 })));
        if (w?.isLiquid) w.setType("minecraft:cobblestone");
      }
      const ceil = getBlock(dim, add(c, { x: 0, y: 2, z: 0 }));
      if (ceil?.isLiquid) ceil.setType("minecraft:cobblestone");
      const floor = getBlock(dim, add(c, { x: 0, y: -1, z: 0 }));
      if (floor && (floor.isAir || floor.isLiquid || floor.typeId === "minecraft:glow_lichen")) floor.setType("minecraft:cobblestone");
    }
    // one redstone torch per segment on a solid side wall
    const t = add(add(start, scale(ax, seg * 5 + 2)), { x: 0, y: 1, z: 0 });
    for (const [o, face] of [[perp, perp.x ? (perp.x > 0 ? "west" : "east") : (perp.z > 0 ? "north" : "south")], [scale(perp, -1), perp.x ? (perp.x > 0 ? "east" : "west") : (perp.z > 0 ? "south" : "north")]]) {
      const wall = getBlock(dim, add(t, o));
      const here = getBlock(dim, t);
      if (wall && !isPassable(wall) && !wall.isLiquid && here?.isAir) {
        try { here.setPermutation(BlockPermutation.resolve("minecraft:redstone_torch", { torch_facing_direction: /** @type {any} */ (face) })); } catch {}
        break;
      }
    }
    yield;
  }
}

// ---------------------------------------------------------------- haunted herd (§2.13) - simplified, see PORTING_NOTES
const HERD_TAG = "ftf:haunted_herd";
export function hauntedHerd(player) {
  if (!rollDefault("haunted_herd_chance")) return;
  const dim = player.dimension;
  if (dim.id !== "minecraft:overworld" || isUnderground(player)) { sighting(PRESETS.stalking, player); return; }
  const yaw = player.getRotation().y;
  const off = add(scale(forwardOf(yaw), randInt(25, 46) * (randInt(0, 1) ? 1 : -1)), scale(leftOf(yaw), randInt(-35, 35)));
  const p = add(player.location, off);
  let top;
  try { top = dim.getTopmostBlock({ x: Math.floor(p.x), z: Math.floor(p.z) }); } catch { return; }
  if (!top || top.isLiquid || /_leaves$/.test(top.typeId)) return;
  const loc = { x: top.x + 0.5, y: top.y + 1, z: top.z + 0.5 };
  let biome = "";
  try { biome = dim.getBiome(loc).id; } catch {}
  const mob = /swamp/.test(biome) ? "minecraft:frog" : pick(["minecraft:pig", "minecraft:cow"]);
  let e;
  try { e = dim.spawnEntity(mob, loc); } catch { return; }
  e.addTag(HERD_TAG);
  e.addEffect("slowness", 20000000, { amplifier: 255, showParticles: false });
  e.setRotation({ x: 0, y: yawTo(loc, player.location) });
  system.runTimeout(() => releaseHerd(e, false), 600);
  debug(`haunted herd ${mob}`);
}
function releaseHerd(e, real) {
  if (!e?.isValid || !e.hasTag(HERD_TAG)) return;
  e.removeTag(HERD_TAG);
  if (!real) { try { e.remove(); } catch {} return; }
  e.removeEffect("slowness");
  e.addTag("ftf:previously_haunted_herd");
}
/** Per tick: herd animals watch the player; when spotted they turn their head 180 degrees, then become ordinary. */
export function herdTick() {
  const dim = world.getDimension("overworld");
  for (const e of dim.getEntities({ tags: [HERD_TAG] })) {
    const near = nearestPlayer(dim, e.location);
    if (!near) continue;
    if (e.hasTag("ftf:herd_spinning")) {
      const r = e.getRotation();
      e.setRotation({ x: r.x, y: r.y + 5 });
      const n = Number(e.getDynamicProperty("ftf:spin") ?? 0) + 5;
      e.setDynamicProperty("ftf:spin", n);
      if (n >= 180) { e.removeTag("ftf:herd_spinning"); system.runTimeout(() => releaseHerd(e, true), 20); }
      continue;
    }
    if (dist(near.location, e.location) <= 7) { releaseHerd(e, true); continue; }
    e.setRotation({ x: 0, y: yawTo(e.location, near.location) });
    // spotted with the stalking cone (spot_fov 77)
    for (const p of players()) {
      if (p.dimension.id !== dim.id || dist(p.location, e.location) > 80) continue;
      const eye = e.getHeadLocation();
      if (angleBetween(p.getViewDirection(), sub(eye, p.getHeadLocation())) > fovToAngle(77)) continue;
      if (lineOfSight(p, eye).visible) { herdSpotted(e, p); break; }
    }
  }
}
export function herdSpotted(entity, player) {
  if (!entity.hasTag(HERD_TAG) || entity.hasTag("ftf:herd_spinning")) return;
  if (dist(player.location, entity.location) <= 20) releaseHerd(entity, true);
  else system.runTimeout(() => { if (entity.isValid) entity.addTag("ftf:herd_spinning"); }, 20);
}

// ---------------------------------------------------------------- malevolent shrine (§2.17)
export function isShrine(dim, p) {
  const at = (dx, dy, dz) => getBlock(dim, { x: p.x + dx, y: p.y + dy, z: p.z + dz })?.typeId;
  if (at(0, -1, 0) !== "minecraft:netherrack") return false;
  let ring = true;
  for (let dx = -1; dx <= 1; dx++) for (let dz = -1; dz <= 1; dz++) if ((dx || dz) && at(dx, -2, dz) !== "minecraft:gold_block") ring = false;
  if (ring) return true;
  return at(0, -2, 0) === "minecraft:netherrack" && at(0, -3, 0) === "minecraft:gold_block" && at(0, -4, 0) === "minecraft:gold_block";
}
const CAPTIONS = { local: ["I'm watching...", "I see you.", "Listen closely.", "Do you hear it too?"], swedish: ["Jag ser dig...", "Jag ser dig.", "Lyssna noga.", "Hör du det också?"] };
export function onFirePlaced(player, dim, loc) {
  if (!isShrine(dim, loc)) return;
  storePosition("fire", { ...floorV(loc), yaw: 0, pitch: 0, dim: dim.id });
  startEvents();
  if (opt("shrine_strike") === true) { try { dim.spawnEntity("minecraft:lightning_bolt", add(loc, { x: 0.5, y: 0, z: 0.5 })); } catch {} }
  grant(player, "build_shrine");
  setActivity(6, 360, 150);
  if (opt("caution_caption") === true) {
    const lang = String(opt("lurking_language")) === "swedish" ? "swedish" : "local";
    const text = pick(CAPTIONS[lang]);
    for (const p of playersNear(dim, loc, 15)) {
      p.onScreenDisplay.setTitle(" ", { subtitle: `§7§o${text}`, fadeInDuration: 10, stayDuration: 40, fadeOutDuration: 20 });
      p.playSound("ftf.event.caution_caption.local.message.1");
    }
  }
  if (opt("shrine_surprise") === true) sighting(PRESETS.shrine_surprise, player, { x: Math.floor(loc.x) + 0.5, y: Math.floor(loc.y), z: Math.floor(loc.z) + 0.5 });
  debug("shrine lit");
}
function rekindle() {
  const keep = [];
  for (const p of getPositions("fire")) {
    const dim = world.getDimension(p.dim);
    if (!isLoaded(dim, p)) { keep.push(p); continue; }
    if (!isShrine(dim, p)) continue;
    keep.push(p);
    const b = getBlock(dim, p);
    if (b?.isAir && noOneWithin(dim, p, 80)) b.setType("minecraft:fire");
  }
  setPositions("fire", keep);
}

// ---------------------------------------------------------------- structures (§2.21, §4)
const STRUCTS = {
  mossy_pyramid: { templates: ["mossy_pyramid_1", "mossy_pyramid_2"], biomes: /forest|birch|extreme_hills|windswept|grove/, h: 6 },
  sand_pyramid: { templates: ["sand_pyramid_1", "sand_pyramid_2"], biomes: /desert|beach/, h: 6 },
  letter: { templates: ["letter_h", "letter_e", "letter_r", "letter_o", "letter_b", "letter_i", "letter_n"], biomes: /desert|plains/, h: 6 },
  wooden_cross: { templates: ["wooden_cross"], biomes: /forest|birch|extreme_hills|windswept|grove/, h: 7 },
};
const STRUCT_CHANCE = { common: 25, default: 100, rare: 200 };
/** Replacement for worldgen "base" markers: sample candidate surface spots 48-128 blocks from players in matching biomes. */
export function scoutStructureSites() {
  for (const p of players()) {
    const dim = p.dimension;
    if (dim.id !== "minecraft:overworld") continue;
    const a = Math.random() * Math.PI * 2, r = randInt(48, 128);
    const c = { x: Math.floor(p.location.x + Math.cos(a) * r), y: 0, z: Math.floor(p.location.z + Math.sin(a) * r) };
    if (!isLoaded(dim, { x: c.x, y: p.location.y, z: c.z })) continue;
    let top;
    try { top = dim.getTopmostBlock({ x: c.x, z: c.z }); } catch { continue; }
    if (!top || top.isLiquid || /_leaves$|log$/.test(top.typeId)) continue;
    let biome = "";
    try { biome = dim.getBiome(top.location).id; } catch {}
    for (const [name, s] of Object.entries(STRUCTS)) {
      if (!s.biomes.test(biome) || opt(name) === "off") continue;
      const list = loadJSON(`ftf:struct:${name}`, []);
      if (list.length >= 32) continue;
      list.push({ x: top.x, y: top.y + 1, z: top.z });
      saveJSON(`ftf:struct:${name}`, list);
      debug(`structure site for ${name} at ${top.x} ${top.y + 1} ${top.z}`);
    }
  }
}
function placeStructure(name) {
  const n = STRUCT_CHANCE[String(opt(name))];
  if (!n || randInt(1, n) !== 1) return;
  const list = loadJSON(`ftf:struct:${name}`, []);
  if (!list.length) return;
  const i = randInt(0, list.length - 1);
  const c = list.splice(i, 1)[0];
  saveJSON(`ftf:struct:${name}`, list);
  const dim = world.getDimension("overworld");
  if (!isLoaded(dim, c)) return;
  const s = STRUCTS[name];
  for (let dx = -2; dx <= 2; dx++) for (let dz = -2; dz <= 2; dz++) for (let dy = 0; dy < s.h; dy++) {
    const b = getBlock(dim, { x: c.x + dx, y: c.y + dy, z: c.z + dz });
    if (!b || !(b.isAir || isPassable(b))) return; // collision check
  }
  const tpl = pick(s.templates);
  const corner = name === "letter" || name === "wooden_cross" ? { x: c.x - 2, y: c.y, z: c.z - 1 } : (tpl.endsWith("_1") ? { x: c.x - 1, y: c.y, z: c.z - 1 } : { x: c.x - 2, y: c.y, z: c.z - 2 });
  try {
    world.structureManager.place(`ftf:${tpl}`, dim, corner);
    const markers = loadJSON("ftf:struct_markers", []);
    markers.push({ ...c, name });
    saveJSON("ftf:struct_markers", markers.slice(-64));
    debug(`placed ${tpl} at ${c.x} ${c.y} ${c.z}`);
  } catch (e) { debug(`structure ${tpl} failed: ${e}`, false); }
}
/** 1-second: structure markers near players grant achievements. */
export function structureMarkerTick() {
  const markers = loadJSON("ftf:struct_markers", []);
  if (!markers.length) return;
  for (const m of markers) for (const p of playersNear(world.getDimension("overworld"), m, 16)) {
    grant(p, "find_any_structure");
    grant(p, `find_${m.name}`);
  }
}

// ---------------------------------------------------------------- forest logging
const FOREST_RX = /forest|birch|grove|taiga|jungle|pale_garden/;
const forestLogged = new Set();
export function forestTick() {
  for (const p of players()) {
    if (forestLogged.has(p.id) || p.dimension.id !== "minecraft:overworld") continue;
    let id = "";
    try { id = p.dimension.getBiome(p.location).id; } catch { continue; }
    if (!FOREST_RX.test(id) || !canSeeSky(p.dimension, p.getHeadLocation())) continue;
    forestLogged.add(p.id);
    const r = p.getRotation();
    storePosition("forest", { ...floorV(p.location), yaw: r.y, pitch: r.x, dim: p.dimension.id });
  }
}

// ---------------------------------------------------------------- the 5-minute tick (spec §1.1)
export function fiveMinuteTick() {
  debug("5-minute tick");
  forestLogged.clear();
  for (const s of ["mossy_pyramid", "sand_pyramid", "letter", "wooden_cross"]) if (opt(s) !== "off") placeStructure(s);
  if (opt("dreadful_donation") === true) randomEvent("dreadful_donation", "chest", dreadfulDonation);
  const rp = pick(players());
  if (rp && opt("fearful_footsteps") === true) fearfulFootsteps(rp);
  if (opt("ghost_doors") === true) randomEvent("ghost_doors", "door", ghostDoors);
  if (!state.started_events) return;
  const rp2 = pick(players());
  if (rp2 && opt("ghost_miner") === true) ghostMiner(rp2);
  const rp3 = pick(players());
  if (rp3 && opt("haunted_herd") === true) hauntedHerd(rp3);
  if (opt("burning_base") === true) randomEvent("burning_base", "bed", burningBase);
  if (opt("disappearing_torches") === true) randomEvent("disappearing_torches", "torch", disappearingTorches);
  if (opt("leafless_grove") === true) {
    // forest entries are always consumed
    const list = getPositions("forest");
    if (list.length) { const i = randInt(0, list.length - 1); const p = list.splice(i, 1)[0]; setPositions("forest", list); const d = world.getDimension(p.dim); if (isLoaded(d, p)) leaflessGrove(d, p); }
  }
  if (opt("chilled_candles") === true) {
    for (let tries = 0; tries < 4; tries++) {
      const list = prune("candle").filter((p) => isLoaded(world.getDimension(p.dim), p));
      if (!list.length) break;
      const p = pick(list);
      const d = world.getDimension(p.dim);
      if (getBlock(d, p)?.permutation.getState("lit")) { chilledCandles(d, p); break; }
    }
  }
  if (opt("crimson_curse") === true) {
    const lanterns = randInt(0, 1) === 1;
    randomEvent("crimson_curse", lanterns ? "lantern" : "torch", (d, p) => crimsonCurse(d, p, lanterns));
  }
  if (opt("rekindling_shrine") === true) rekindle();
}

