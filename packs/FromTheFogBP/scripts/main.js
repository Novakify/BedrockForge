// From The Fog (Bedrock) - entry point.
// Unofficial, non-commercial Bedrock adaptation of "From The Fog" by LunarEclipseStudios (CC BY-NC-SA 4.0).
import { world, system, Player, CommandPermissionLevel, CustomCommandStatus, WeatherType } from "@minecraft/server";
import { loadConfig, opt, ghcHasBackup, ghcInit, ghcCleanup, ghcRandomize } from "./config.js";
import { state, loadState, saveState, startEvents, grant, setActivity } from "./state.js";
import {
  HEROBRINE, sightingTimerTick, herobrineTick, senseTick, nightmareTick, removeHerobrine, hasSighting, onHerobrineHit,
  onHerobrineShot, customSighting, sighting, PRESETS, setRaining, runPreset,
} from "./sightings.js";
import {
  tagForBlock, storePosition, onFirePlaced, checkDonationChest, doorGhostTick, fiveMinuteTick, herdTick, forestTick,
  structureMarkerTick, scoutStructureSites, removeGhosts,
} from "./events.js";
import { openConfig, hooks } from "./ui.js";
import { debug, floorV } from "./util.js";

let ready = false;

// ---------------------------------------------------------------- commands
system.beforeEvents.startup.subscribe((ev) => {
  ev.customCommandRegistry.registerCommand(
    { name: "ftf:config", description: "Open the From The Fog settings", permissionLevel: CommandPermissionLevel.GameDirectors },
    (origin) => {
      const p = origin.sourceEntity;
      if (!(p instanceof Player)) return { status: CustomCommandStatus.Failure, message: "Run this as a player." };
      system.run(() => openConfig(p));
      return { status: CustomCommandStatus.Success };
    });
});

system.afterEvents.scriptEventReceive.subscribe((ev) => {
  const p = ev.sourceEntity instanceof Player ? ev.sourceEntity : undefined;
  switch (ev.id) {
    case "ftf:config": if (p) openConfig(p); break;
    case "ftf:debug":
      if (p) { if (p.hasTag("ftf:debug")) { p.removeTag("ftf:debug"); p.playSound("tile.piston.in"); } else { p.addTag("ftf:debug"); p.playSound("tile.piston.out"); } }
      break;
    case "ftf:summon": // custom sighting API (spec §2.23): /scriptevent ftf:summon {"position":"creeping",...}
      if (p) { let args = {}; try { args = JSON.parse(ev.message || "{}"); } catch {} customSighting(p, args); }
      break;
    case "ftf:sighting": // debug: /scriptevent ftf:sighting stalking
      if (p) { const t = ev.message.trim() || "creeping"; if (PRESETS[t] && ["nightmare", "ghost_miner", "shrine_surprise"].includes(t)) sighting(PRESETS[t], p); else runPreset(t, p); }
      break;
    case "ftf:start": startEvents(); break;
    case "ftf:events": state.five_min_left = 1; break;
    case "ftf:remove": removeHerobrine(); break;
  }
});

// Fog Journal item opens the menu.
world.afterEvents.itemUse.subscribe((ev) => {
  if (ev.itemStack.typeId === "ftf:fog_journal") openConfig(ev.source);
});

// ---------------------------------------------------------------- block detection (spec §2.18)
world.afterEvents.playerPlaceBlock.subscribe((ev) => {
  const b = ev.block;
  const tag = tagForBlock(b.typeId);
  if (!tag) return;
  const r = ev.player.getRotation();
  storePosition(tag, { ...b.location, yaw: r.y, pitch: r.x, dim: b.dimension.id });
});

world.afterEvents.playerInteractWithBlock.subscribe((ev) => {
  const item = ev.itemStack?.typeId;
  const b = ev.block;
  if (item === "minecraft:flint_and_steel" || item === "minecraft:fire_charge") {
    const dim = b.dimension;
    const off = { Up: [0, 1, 0], Down: [0, -1, 0], North: [0, 0, -1], South: [0, 0, 1], East: [1, 0, 0], West: [-1, 0, 0] }[String(ev.blockFace)] ?? [0, 1, 0];
    const tryAt = [{ x: b.location.x + off[0], y: b.location.y + off[1], z: b.location.z + off[2] }, b.location];
    system.runTimeout(() => {
      for (const l of tryAt) { const f = dim.getBlock(l); if (f?.typeId === "minecraft:fire") { onFirePlaced(ev.player, dim, floorV(l)); break; } }
    }, 1);
  }
  if (b.typeId === "minecraft:chest" || b.typeId === "minecraft:trapped_chest") checkDonationChest(ev.player, b);
  // heirloom discs: scripted jukebox playback
  if (b.typeId === "minecraft:jukebox" && item?.startsWith("ftf:disc_")) {
    b.dimension.playSound(`ftf.music_disc.${item.slice(9)}`, b.center(), { volume: 4 });
  }
});

// ---------------------------------------------------------------- hits, shots, deaths
world.afterEvents.entityHitEntity.subscribe((ev) => {
  if (ev.hitEntity.typeId === HEROBRINE && ev.damagingEntity instanceof Player) onHerobrineHit(ev.damagingEntity);
});
world.afterEvents.projectileHitEntity.subscribe((ev) => {
  const hit = ev.getEntityHit()?.entity;
  if (hit?.typeId !== HEROBRINE) return;
  try { ev.projectile.clearVelocity(); ev.projectile.remove(); } catch {}
  onHerobrineShot();
});
world.afterEvents.entityDie.subscribe((ev) => {
  const e = ev.deadEntity;
  try {
    if (e.hasTag("ftf:previously_haunted_herd") && ev.damageSource.damagingEntity instanceof Player) grant(ev.damageSource.damagingEntity, "kill_a_haunted_herd_mob");
  } catch {}
});
world.afterEvents.weatherChange.subscribe((ev) => {
  if (ev.dimension === "minecraft:overworld") { setRaining(ev.newWeather !== WeatherType.Clear); world.setDynamicProperty("ftf:raining", ev.newWeather !== WeatherType.Clear); }
});

hooks.onGhcToggled = (p) => { if (p) sighting(PRESETS.creeping, p); };

// ---------------------------------------------------------------- loops
world.afterEvents.worldLoad.subscribe(() => {
  loadConfig();
  loadState();
  setRaining(world.getDynamicProperty("ftf:raining") === true);
  removeHerobrine(); // failsafe: no sighting survives a reload
  removeGhosts();
  ready = true;
});

system.runInterval(() => {
  if (!ready) return;
  try { herobrineTick(); } catch (e) { console.warn(`[ftf] tick: ${e}`); }
  try { herdTick(); } catch {}
}, 1);

let seconds = 0;
system.runInterval(() => {
  if (!ready) return;
  seconds++;
  // Give Him Control
  if (opt("give_him_control") === true && !ghcHasBackup()) ghcInit();
  else if (opt("give_him_control") !== true && ghcHasBackup()) ghcCleanup();
  sightingTimerTick();
  const day = world.getDay();
  if (opt("give_him_control") === true && state.logged_day !== -1 && day !== state.logged_day) { ghcRandomize(); debug("GHC: options randomised"); }
  if (day !== state.logged_day) { state.logged_day = day; saveState(); }
  if (!state.started_events && day >= Number(opt("haunting_delay"))) startEvents();
  // time-played achievements (play time counted per player)
  for (const p of world.getAllPlayers()) {
    const t = Number(p.getDynamicProperty("ftf:play_seconds") ?? 0) + 1;
    p.setDynamicProperty("ftf:play_seconds", t);
    const days = Math.floor(t / 1200);
    for (const d of [3, 10, 50, 100, 200]) if (days >= d) grant(p, `${d}_days`);
  }
  try { senseTick(); } catch {}
  try { nightmareTick(); } catch {}
  try { doorGhostTick(); } catch {}
  try { forestTick(); } catch {}
  try { structureMarkerTick(); } catch {}
  if (seconds % 30 === 0) { try { scoutStructureSites(); } catch {} }
  // the 5-minute event loop (pulled earlier by the activity multiplier)
  state.five_min_left--;
  if (state.five_min_left <= 0) {
    state.five_min_left = 300;
    saveState();
    if (!hasSighting()) removeHerobrine();
    try { fiveMinuteTick(); } catch (e) { console.warn(`[ftf] 5min: ${e}`); }
  } else if (seconds % 15 === 0) saveState();
}, 20);

export { setActivity };
