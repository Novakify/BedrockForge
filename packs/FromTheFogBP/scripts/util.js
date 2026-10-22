// From The Fog (Bedrock) - shared helpers: vectors, randomness, chance tables, block sets, persistence.
// Adaptation of "From The Fog" by LunarEclipseStudios (CC BY-NC-SA 4.0).
import { world, system, GameMode } from "@minecraft/server";

// ---------------------------------------------------------------- vectors
/** @typedef {{x:number,y:number,z:number}} Vec */
export const add = (a, b) => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });
export const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
export const scale = (a, s) => ({ x: a.x * s, y: a.y * s, z: a.z * s });
export const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
export const len = (a) => Math.sqrt(dot(a, a));
export const norm = (a) => { const l = len(a) || 1; return scale(a, 1 / l); };
export const dist = (a, b) => len(sub(a, b));
export const floorV = (a) => ({ x: Math.floor(a.x), y: Math.floor(a.y), z: Math.floor(a.z) });
export const center = (a) => ({ x: Math.floor(a.x) + 0.5, y: a.y, z: Math.floor(a.z) + 0.5 });

/** Minecraft yaw convention: yaw 0 looks toward +Z, yaw 90 toward -X. */
export const forwardOf = (yaw) => { const r = (yaw * Math.PI) / 180; return { x: -Math.sin(r), y: 0, z: Math.cos(r) }; };
/** Java local "^1 ^ ^" (left of the facing direction). */
export const leftOf = (yaw) => { const r = (yaw * Math.PI) / 180; return { x: Math.cos(r), y: 0, z: Math.sin(r) }; };
export const yawTo = (from, to) => (-Math.atan2(to.x - from.x, to.z - from.z) * 180) / Math.PI;
export const pitchTo = (from, to) => {
  const d = sub(to, from);
  return (-Math.atan2(d.y, Math.sqrt(d.x * d.x + d.z * d.z)) * 180) / Math.PI;
};
/** Angle in degrees between two directions. */
export const angleBetween = (a, b) => (Math.acos(Math.max(-1, Math.min(1, dot(norm(a), norm(b))))) * 180) / Math.PI;
/** Java `distance=..0.<fov>` chord between unit points -> cone half-angle in degrees (spec §2.2). */
export const fovToAngle = (spotFov) => (spotFov <= 0 ? -1 : 2 * Math.asin(Math.min(1, spotFov / 100 / 2)) * (180 / Math.PI));

// ---------------------------------------------------------------- randomness
export const randInt = (a, b) => Math.floor(Math.random() * (b - a + 1)) + a;
export const pick = (arr) => arr[Math.floor(Math.random() * arr.length)];
export const chance = (pct) => Math.random() * 100 < pct;

// Chance tables (spec §1.5). The "common" table is the one Java meant for footsteps/tapestries/sense (bug A1 fixed: the roll really happens).
export const DEFAULT_TABLE = { insane: 5, common: 25, default: 100, rare: 200, scarce: 500 };
export const COMMON_TABLE = { insane: 3, common: 6, default: 25, rare: 50, scarce: 125 };

// ---------------------------------------------------------------- time / players
export const isNight = () => { const t = world.getTimeOfDay(); return t >= 13000 && t < 24000; };
export const isDark = () => { const t = world.getTimeOfDay(); return t >= 13000 && t < 22000; };
export const ticks = (seconds) => Math.max(1, Math.round(seconds * 20));

export function players() {
  return world.getAllPlayers().filter((p) => p.isValid && p.getGameMode() !== GameMode.Spectator);
}
export function playersNear(dim, loc, radius) {
  return players().filter((p) => p.dimension.id === dim.id && dist(p.location, loc) <= radius);
}
export function nearestPlayer(dim, loc, maxR = 1e9) {
  let best, bd = maxR;
  for (const p of players()) {
    if (p.dimension.id !== dim.id) continue;
    const d = dist(p.location, loc);
    if (d <= bd) { bd = d; best = p; }
  }
  return best;
}

// ---------------------------------------------------------------- blocks
export function getBlock(dim, loc) {
  try { return dim.getBlock(floorV(loc)); } catch { return undefined; }
}
export function isLoaded(dim, loc) {
  try { return dim.isChunkLoaded(floorV(loc)); } catch { return false; }
}
const NO_COL_RX = /(^|:)(air|cave_air|void_air|light_block.*|short_grass|tall_grass|grass|fern|large_fern|deadbush|dead_bush|.*_flower|dandelion|poppy|blue_orchid|allium|azure_bluet|.*_tulip|oxeye_daisy|cornflower|lily_of_the_valley|wither_rose|sunflower|lilac|rose_bush|peony|torchflower|pink_petals|wildflowers|leaf_litter|.*sapling|.*_leaves|.*torch|.*carpet|snow_layer|vine|.*_vines.*|glow_lichen|sculk_vein|.*_mushroom|crimson_roots|warped_roots|nether_sprouts|.*_coral.*|.*_fan|seagrass|kelp|sweet_berry_bush|wheat|carrots|potatoes|beetroot|melon_stem|pumpkin_stem|sugar_cane|.*_button|lever|.*_pressure_plate|tripwire.*|redstone_wire|rail|.*_rail|.*sign|.*_banner|ladder|scaffolding|cobweb|web|bush|firefly_bush|short_dry_grass|tall_dry_grass|cactus_flower|hanging_roots|spore_blossom|small_dripleaf_block|big_dripleaf|pitcher_plant|.*_bulb|structure_void)$/;
/** Java `no_col_blocks`: passable for ground correction. */
export function isPassable(block) {
  if (!block) return false;
  if (block.isAir) return true;
  if (block.isLiquid) return false;
  return NO_COL_RX.test(block.typeId) || block.typeId.endsWith("_wall_sign");
}
export function isLeaves(block) { return !!block && /_leaves$/.test(block.typeId); }
export const GLASS_RX = /(glass|glass_pane)$/;
/** Java `sight_blocks`: blocks the spot ray passes through. */
export function isSeeThrough(block) {
  if (!block) return true;
  if (block.isAir || block.isLiquid) return true;
  const id = block.typeId;
  return isPassable(block) || GLASS_RX.test(id) || /(_fence|_fence_gate|_door|_trapdoor|iron_bars|chain|_wall_sign|_hanging_sign|lantern|candle|end_rod|lightning_rod|flower_pot|_slab)$/.test(id);
}
export function isReplaceable(block) {
  if (!block) return false;
  return block.isAir || /(short_grass|tall_grass|^minecraft:grass$|fern|snow_layer|_flower|dandelion|poppy|_tulip|oxeye_daisy|cornflower|azure_bluet|allium|blue_orchid|lily_of_the_valley|wheat|carrots|potatoes|beetroot|deadbush|dead_bush)$/.test(block.typeId);
}

// ---------------------------------------------------------------- persistence (world dynamic properties, JSON)
export function loadJSON(key, fallback) {
  try {
    const raw = world.getDynamicProperty(key);
    if (typeof raw === "string") return JSON.parse(raw);
  } catch {}
  return fallback;
}
export function saveJSON(key, value) {
  try { world.setDynamicProperty(key, JSON.stringify(value)); } catch (e) { console.warn(`[ftf] save ${key} failed: ${e}`); }
}

// ---------------------------------------------------------------- debug
export function debug(msg, ok = true) {
  for (const p of world.getAllPlayers()) {
    if (p.hasTag("ftf:debug")) p.sendMessage(`${ok ? "§a[+]" : "§c[x]"} §7[FTF] §f${msg}`);
  }
}

/** Run a block-mutating generator over many ticks. */
export function job(gen) { return system.runJob(gen); }
