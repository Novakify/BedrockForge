// Bedrock Forge Impact - behavior pack script (official Script API, no native code needed)
//
// When a player lands a critical hit on a mob (any melee hit if critOnly is off), their OWN view is posterized for ~150ms using per-player
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
  critOnly: true,    // only critical hits (falling, not on ground / in water / climbing / riding, no blindness)
  posterize: true,   // black/white world (Vibrant Visuals)
  flash: true,       // white beat in the middle
  sound: true,
  shake: 0.35,       // /camerashake intensity, 0 = off
  minDamage: 1,      // ignore tiny hits
  cooldownTicks: 4,  // at most one impact per 4 ticks (200ms)
  vvShader: true,    // a Bedrock Forge shaderpack draws the VV impact frame (marker below); off = old posterize grading
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
    .toggle("Critical hits only", { defaultValue: s.critOnly })
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
  const [enabled, critOnly, posterize, flash, sound, shake10, minDamage, cooldownTicks] = res.formValues ?? [];
  save(player, { enabled: !!enabled, critOnly: !!critOnly, posterize: !!posterize, flash: !!flash, sound: !!sound,
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

// Fancy graphics impact frame: the Bedrock Forge shaderpacks turn the whole world white and the mob/player that was
// just hit (red hurt overlay) black while the "bf:impact" fog (magenta, BedrockForgeImpact RP) is on the stack.
const WHITE_FRAME_TICKS = 4; // fog fades in ~0.3 s; shaders react at ~40% -> ~0.3 s white
const fogEnd = new Map(); // player id -> tick the current white frame ends
// Vibrant Visuals: fog colour doesn't reach VV's final pass, so the BP also sets the player's VV colour-grading
// midtones gain to this marker for the same window; the shaderpacks' ColorPostProcessing (shaders/src/_shared/
// ColorPostVV.fragment.hlsli) detects it (vanilla gains are always 1). Without a BF shaderpack: turn vvShader off.
const VV_MARK = { x: 4, y: 0.05, z: 4 };
function vvMark(player, on) {
  try { const g = gradingFor(player); if (on) g.setMidtonesGain(VV_MARK); else g.resetMidtonesGain(); }
  catch (e) { if (on) console.warn(`[BF] VV impact marker failed: ${e}`); }
}
function whiteFrame(player, s, ticks = WHITE_FRAME_TICKS) {
  const now = system.currentTick;
  if ((fogEnd.get(player.id) ?? 0) > now) { fogEnd.set(player.id, now + ticks); return; } // already white: extend
  fogEnd.set(player.id, now + ticks);
  try { player.runCommand("fog @s push bf:impact bf_impact"); }
  catch (e) { console.warn(`[BF] impact fog failed: ${e}`); }
  const vv = s.vvShader;
  if (vv) vvMark(player, true);
  const tick = () => {
    if (!player.isValid) return;
    if (system.currentTick < (fogEnd.get(player.id) ?? 0)) { if (vv) vvMark(player, true); system.runTimeout(tick, 1); return; }
    try { player.runCommand("fog @s remove bf_impact"); } catch {}
    if (vv) vvMark(player, false);
  };
  system.runTimeout(tick, 1);
}

function impact(player, s, ticks = WHITE_FRAME_TICKS) {
  const now = system.currentTick;
  if ((busy.get(player.id) ?? 0) > now) return;
  busy.set(player.id, now + s.cooldownTicks);

  if (s.sound) { try { player.playSound(SOUND, { volume: 1, pitch: 0.95 + Math.random() * 0.1 }); } catch {} }
  if (s.shake > 0) {
    try { player.runCommand(`camerashake add @s ${s.shake.toFixed(2)} 0.18 rotational`); }
    catch (e) { console.warn(`[BF] camerashake failed: ${e}`); }
  }
  whiteFrame(player, s, ticks);
  if (!s.posterize || s.vvShader) return;

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

// Same conditions the game uses for a melee crit. Checked right after the hit, so the attacker state still matches.
/** @param {Player} p */
function isCrit(p) {
  try {
    if (p.isOnGround || p.isInWater || p.isClimbing || !p.isFalling) return false;
    if (p.getComponent("minecraft:riding")) return false;
    if (p.getEffect("blindness")) return false;
    return true;
  } catch { return false; }
}

world.afterEvents.entityHurt.subscribe((ev) => {
  const attacker = ev.damageSource?.damagingEntity;
  if (!(attacker instanceof Player)) return;
  if (ev.hurtEntity?.id === attacker.id) return;
  const s = load(attacker);
  if (!s.enabled || (ev.damage ?? 0) < s.minDamage) return;
  if (s.critOnly && !isCrit(attacker)) return;
  impact(attacker, s);
});

// Test hook: /scriptevent bf:impacttest [ticks]  (functions/bfivv) = one impact frame for the player who runs it
// (optional hold length, e.g. 60 = 3 s, to inspect it).
system.afterEvents.scriptEventReceive.subscribe((ev) => {
  if (!(ev.sourceEntity instanceof Player)) return;
  if (ev.id === "bf:vvmark") {
    const on = ev.message !== "off"; vvMark(ev.sourceEntity, on); return;
  }   // debug: hold the VV marker on/off
  if (ev.id !== "bf:impacttest") return;
  busy.delete(ev.sourceEntity.id);
  impact(ev.sourceEntity, { ...load(ev.sourceEntity), sound: false, shake: 0 }, Math.max(1, parseInt(ev.message) || WHITE_FRAME_TICKS));
});

// Clean up if a player leaves mid-impact.
world.afterEvents.playerLeave.subscribe((ev) => busy.delete(ev.playerId));
// Never leave a player stuck in the impact fog (e.g. world closed mid-frame).
world.afterEvents.playerSpawn.subscribe((ev) => { try { ev.player.runCommand("fog @s remove bf_impact"); } catch {} vvMark(ev.player, false); });

console.warn("[BF] Bedrock Forge Impact behavior pack loaded (/bf:impact for settings)");
