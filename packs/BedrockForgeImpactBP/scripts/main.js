// Bedrock Forge Impact - behavior pack script (official Script API, no native code needed)
//
// When a player melee-hits a mob, their OWN view is posterized for ~150ms using per-player
// Vibrant Visuals colour grading (@minecraft/server-graphics), then restored:
//   tick 0 : saturation 0 + max contrast  -> near black/white world (silhouettes)
//   tick 1 : blown-out white flash         -> the "inverted" beat
//   tick 2 : back to posterized
//   tick 3 : reset to whatever the resource packs define
// Also plays the original impact SFX and a short vanilla /camerashake.
// The burst mask (sparkle / focus lines) is drawn by the BedrockForgeCore native mod on top.
//
// Settings: type /bf:impact in chat. Saved per player.
// Needs Vibrant Visuals for the black/white part; on Fancy graphics that part does nothing (harmless).

import { world, system, Player, CommandPermissionLevel, CustomCommandStatus } from "@minecraft/server";
import { getPlayerColorGrading } from "@minecraft/server-graphics";
import { ModalFormData, FormCancelationReason } from "@minecraft/server-ui";

const DEFAULTS = {
  enabled: true,
  posterize: true,   // black/white world (Vibrant Visuals)
  flash: true,       // white beat in the middle
  sound: true,
  shake: 0.35,       // /camerashake intensity, 0 = off
  minDamage: 1,      // ignore tiny hits
  cooldownTicks: 4,  // at most one impact per 4 ticks (200ms)
};
const PROP = "bf:impact_settings";
const SOUND = "bf.impact";

const V = (x) => ({ x, y: x, z: x });
const busy = new Map(); // player id -> tick when they may trigger again

// ---------------------------------------------------------------- settings
function load(player) {
  try {
    const raw = player.getDynamicProperty(PROP);
    if (typeof raw === "string") return { ...DEFAULTS, ...JSON.parse(raw) };
  } catch {}
  return { ...DEFAULTS };
}
function save(player, s) { player.setDynamicProperty(PROP, JSON.stringify(s)); }

async function openSettings(player, attempt = 0) {
  const s = load(player);
  const form = new ModalFormData()
    .title("Bedrock Forge - Impact Frames")
    .toggle("Impact frames on", { defaultValue: s.enabled })
    .toggle("Black/white world (needs Vibrant Visuals)", { defaultValue: s.posterize })
    .toggle("White flash beat", { defaultValue: s.flash })
    .toggle("Impact sound", { defaultValue: s.sound })
    .slider("Camera shake (x0.1)", 0, 15, { defaultValue: Math.round(s.shake * 10), valueStep: 1 })
    .slider("Minimum damage", 0, 10, { defaultValue: s.minDamage, valueStep: 1 })
    .slider("Cooldown (ticks, 20 = 1s)", 0, 20, { defaultValue: s.cooldownTicks, valueStep: 1 })
    .submitButton("Save");
  const res = await form.show(player);
  if (res.canceled) {
    // Chat is usually still closing right after the command; retry for ~1s.
    if (res.cancelationReason === FormCancelationReason.UserBusy && attempt < 20) {
      system.runTimeout(() => openSettings(player, attempt + 1), 1);
    }
    return;
  }
  const [enabled, posterize, flash, sound, shake10, minDamage, cooldownTicks] = res.formValues ?? [];
  save(player, { enabled: !!enabled, posterize: !!posterize, flash: !!flash, sound: !!sound,
                 shake: Number(shake10) / 10, minDamage: Number(minDamage), cooldownTicks: Number(cooldownTicks) });
  player.sendMessage("§b[Bedrock Forge]§r impact settings saved.");
}

system.beforeEvents.startup.subscribe((ev) => {
  ev.customCommandRegistry.registerCommand(
    { name: "bf:impact", description: "Open Bedrock Forge impact-frame settings", permissionLevel: CommandPermissionLevel.Any },
    (origin) => {
      const p = origin.sourceEntity;
      if (!(p instanceof Player)) return { status: CustomCommandStatus.Failure, message: "Run this as a player." };
      system.run(() => openSettings(p));
      return { status: CustomCommandStatus.Success };
    });
});

// ---------------------------------------------------------------- the effect
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

function impact(player, s) {
  const now = system.currentTick;
  if ((busy.get(player.id) ?? 0) > now) return;
  busy.set(player.id, now + s.cooldownTicks);

  if (s.sound) { try { player.playSound(SOUND, { volume: 1, pitch: 0.95 + Math.random() * 0.1 }); } catch {} }
  if (s.shake > 0) {
    try { player.runCommand(`camerashake add @s ${s.shake.toFixed(2)} 0.18 rotational`); }
    catch (e) { console.warn(`[BF] camerashake failed: ${e}`); }
  }
  if (!s.posterize) return;

  let g;
  try { g = gradingFor(player); } catch (e) { console.warn(`[BF] colour grading unavailable: ${e}`); return; }
  const sequence = s.flash ? ["poster", "white", "poster"] : ["poster", "poster", "poster"];
  sequence.forEach((look, i) => {
    system.runTimeout(() => {
      try { reset(g); apply(g, look); } catch (e) { console.warn(`[BF] apply failed: ${e}`); }
    }, i);
  });
  system.runTimeout(() => { try { reset(g); } catch {} }, sequence.length);
}

world.afterEvents.entityHurt.subscribe((ev) => {
  const attacker = ev.damageSource?.damagingEntity;
  if (!(attacker instanceof Player)) return;
  if (ev.hurtEntity?.id === attacker.id) return;
  const s = load(attacker);
  if (!s.enabled || (ev.damage ?? 0) < s.minDamage) return;
  impact(attacker, s);
});

// Clean up if a player leaves mid-impact.
world.afterEvents.playerLeave.subscribe((ev) => busy.delete(ev.playerId));

console.warn("[BF] Bedrock Forge Impact behavior pack loaded (/bf:impact for settings)");
