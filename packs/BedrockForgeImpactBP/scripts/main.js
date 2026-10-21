// Bedrock Forge Impact - behavior pack script (official Script API, no native code needed)
//
// When a player melee-hits a mob, their OWN view is posterized for ~100-150ms using
// per-player Vibrant Visuals colour grading (@minecraft/server-graphics), then restored.
//   frame A (tick 0)  : saturation 0 + max contrast  -> near black/white world (silhouettes)
//   frame B (tick 1)  : blown-out white flash         -> the "inverted" beat
//   frame C (tick 2)  : back to frame A
//   tick 3            : reset to whatever the resource packs define
// Also plays the impact SFX and a short vanilla /camerashake.
// The burst mask (sparkle / focus lines) is drawn by the BedrockForgeCore native mod on top.
//
// Needs: Vibrant Visuals enabled. On Fancy graphics colour grading does nothing (harmless).

import { world, system, Player } from "@minecraft/server";
import { getPlayerColorGrading } from "@minecraft/server-graphics";

const CONFIG = {
  minDamage: 1,      // ignore tiny hits
  cooldownTicks: 4,  // at most one impact per 4 ticks (200ms)
  sequence: ["poster", "white", "poster"], // one entry per tick (50ms each)
  sound: "bf.impact",           // original SFX in the resource pack ("" = off)
  shake: { intensity: 0.35, seconds: 0.18, type: "rotational" }, // vanilla /camerashake (null = off)
};

const V = (x) => ({ x, y: x, z: x });
const busy = new Map(); // player id -> tick when they may trigger again

function gradingFor(player) {
  // The API takes a biome; use the one the player is standing in.
  return getPlayerColorGrading(player.dimension.getBiome(player.location), player);
}

function apply(g, look) {
  if (look === "poster") {
    for (const band of ["Shadows", "Midtones", "Highlights"]) {
      g[`set${band}Saturation`](V(0));
      g[`set${band}Contrast`](V(4));
    }
    g.setShadowsGain(V(0.2));   // crush darks to black
    g.setHighlightsGain(V(3));  // blow brights to white
    g.setMidtonesGamma(V(0.6));
  } else if (look === "white") {
    for (const band of ["Shadows", "Midtones", "Highlights"]) {
      g[`set${band}Saturation`](V(0));
      g[`set${band}Gain`](V(10));
      g[`set${band}Offset`](V(0.6));
    }
  }
}

function reset(g) {
  for (const band of ["Shadows", "Midtones", "Highlights"]) {
    for (const p of ["Saturation", "Contrast", "Gain", "Gamma", "Offset"]) {
      try { g[`reset${band}${p}`](); } catch {}
    }
  }
}

function impact(player) {
  const now = system.currentTick;
  if ((busy.get(player.id) ?? 0) > now) return;
  busy.set(player.id, now + CONFIG.cooldownTicks);

  if (CONFIG.sound) { try { player.playSound(CONFIG.sound, { volume: 1, pitch: 0.95 + Math.random() * 0.1 }); } catch {} }
  if (CONFIG.shake) {
    const k = CONFIG.shake;
    try { player.runCommand(`camerashake add @s ${k.intensity} ${k.seconds} ${k.type}`); } catch (e) { console.warn(`[BF] camerashake failed: ${e}`); }
  }

  let g;
  try { g = gradingFor(player); } catch (e) { console.warn(`[BF] colour grading unavailable: ${e}`); return; }

  CONFIG.sequence.forEach((look, i) => {
    system.runTimeout(() => {
      try { reset(g); apply(g, look); } catch (e) { console.warn(`[BF] apply failed: ${e}`); }
    }, i);
  });
  system.runTimeout(() => { try { reset(g); } catch {} }, CONFIG.sequence.length);
}

world.afterEvents.entityHurt.subscribe((ev) => {
  const attacker = ev.damageSource?.damagingEntity;
  if (!(attacker instanceof Player)) return;
  if (ev.hurtEntity?.id === attacker.id) return;
  if ((ev.damage ?? 0) < CONFIG.minDamage) return;
  impact(attacker);
});

// Clean up if a player leaves mid-impact.
world.afterEvents.playerLeave.subscribe((ev) => busy.delete(ev.playerId));

console.warn("[BF] Bedrock Forge Impact behavior pack loaded");
