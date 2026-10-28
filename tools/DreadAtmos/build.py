#!/usr/bin/env python3
"""Dread Atmosphere - build a Vibrant Visuals horror grade from the game's own vanilla settings.

  python build.py <game data/resource_packs dir> -o <out dir>

Reads the vanilla color_grading / atmospherics / lighting settings (brarchives), applies the Dread transform to every
biome group (desaturated, darker, colder, lower sun, darker nights) and writes an RP + a tiny BP that pushes the Dread
fog (distance fog for Fancy graphics, volumetric fog for Vibrant Visuals) onto every player.
The BP also spawns ambient falling leaves + dust (particles + textures in the RP); tuning constants at the top of BP_SCRIPT.
Pack versions stay 1.0.0: worlds pin [1,0,0] and development packs are re-read on every world load.
Original work for Bedrock Forge (MIT). Only numbers derived from vanilla defaults are shipped, no vanilla files.
"""
import argparse, base64, json, uuid
from pathlib import Path

NS = uuid.UUID("6b0f1a52-6d0e-4c1b-9f3e-d7ad0a7d5e01")

def objects(path):
    t = Path(path).read_bytes().decode("latin1"); out = []; i = 0; dec = json.JSONDecoder()
    while (i := t.find('{"format_version"', i)) >= 0:
        o, e = dec.raw_decode(t[i:]); out.append(o); i += e
    return out

def desat_rgb(c, amount, dim):
    r, g, b = c; l = 0.2126 * r + 0.7152 * g + 0.0722 * b
    mix = lambda x: (x + (l - x) * amount) * dim
    # slight cold tint
    return [round(min(255, max(0, mix(r) * 0.94))), round(min(255, max(0, mix(g) * 0.98))), round(min(255, max(0, mix(b) * 1.06)))]

def map_keyframes(v, f):
    if isinstance(v, dict) and v and all(_isnum(k) for k in v): return {k: f(x) for k, x in v.items()}
    return f(v)

def _isnum(s):
    try: float(s); return True
    except ValueError: return False

def grade(o, strength):
    s = o["minecraft:color_grading_settings"]
    cg = s.setdefault("color_grading", {})
    mid = cg.setdefault("midtones", {})
    lerp = lambda a, b: round(a + (b - a) * strength, 4)
    c0 = mid.get("contrast", [1.15] * 3)[0]
    mid["contrast"] = [lerp(c0, 1.32)] * 3
    mid["gain"] = [lerp(1.0, 0.82)] * 3
    mid["gamma"] = [lerp(2.2, 2.45)] * 3
    mid["offset"] = [lerp(0.0, -0.012)] * 3
    mid["saturation"] = [lerp(mid.get("saturation", [1.05])[0], 0.42)] * 3
    t = cg.setdefault("temperature", {"enabled": True, "type": "color_temperature"})
    t["temperature"] = round(lerp(t.get("temperature", 6500), 7800))
    return o

def atmos(o, strength):
    s = o["minecraft:atmosphere_settings"]
    for key in ("sky_zenith_color", "sky_horizon_color"):
        if key in s: s[key] = map_keyframes(s[key], lambda c: desat_rgb(c, 0.75 * strength, 1 - 0.45 * strength) if isinstance(c, list) else c)
    if "rayleigh_strength" in s: s["rayleigh_strength"] = map_keyframes(s["rayleigh_strength"], lambda x: round(x * (1 - 0.5 * strength), 4))
    if "sun_mie_strength" in s: s["sun_mie_strength"] = map_keyframes(s["sun_mie_strength"], lambda x: round(x * 0.5, 4))
    return o

def light(o, strength):
    s = o["minecraft:lighting_settings"]
    dl = s.get("directional_lights", {})
    orb = dl.get("orbital", dl)
    for body, scale in (("sun", 1 - 0.55 * strength), ("moon", 1 - 0.6 * strength)):
        b = orb.get(body)
        if not isinstance(b, dict): continue
        if "illuminance" in b: b["illuminance"] = map_keyframes(b["illuminance"], lambda x: round(x * scale, 4))
        if "color" in b: b["color"] = map_keyframes(b["color"], lambda c: desat_rgb(c, 0.55 * strength, 1.0) if isinstance(c, list) else c)
    if "ambient" in s and isinstance(s["ambient"].get("illuminance"), (int, float)):
        s["ambient"]["illuminance"] = round(s["ambient"]["illuminance"] * (1 - 0.6 * strength), 5)
        s["ambient"]["color"] = "#B8C4D6"
    if "emissive" in s: s["emissive"]["desaturation"] = 0.35
    return o

def fname(ident, default_name, suffix):
    short = ident.split(":", 1)[1]
    return default_name if short == "default" + suffix else short + ".json"

FOG = {
    "format_version": "1.21.90",
    "minecraft:fog_settings": {
        "description": {"identifier": "bedrock_forge:dread_fog"},
        "distance": {
            "air": {"fog_start": 0.08, "fog_end": 0.62, "fog_color": "#3B4047", "render_distance_type": "render"},
            "weather": {"fog_start": 0.02, "fog_end": 0.38, "fog_color": "#2E3238", "render_distance_type": "render"},
        },
        "volumetric": {
            "density": {"air": {"max_density": 0.16, "max_density_height": 56.0, "zero_density_height": 150.0}},
            "media_coefficients": {"air": {"scattering": [0.16, 0.165, 0.18], "absorption": [0.03, 0.03, 0.025]}},
            "henyey_greenstein_g": {"air": {"henyey_greenstein_g": 0.55}},
        },
    },
}

BP_SCRIPT = """// Dread Atmosphere (original, MIT).
//  1. Fog control. Default: CLEAR the Dread fog so shaderpacks such as Cursed Fog see the game's real fog colour.
//     To use the Dread fog again, set PUSH = true.
//  2. Ambient particles: leaves drift down from leaf blocks with air below them, and faint dust hangs in the air
//     around the player (bedrock_forge:falling_leaf / bedrock_forge:dust_mote in the resource pack).
import { world, system, MolangVariableMap } from "@minecraft/server";

// ---- tuning ----
const PUSH = false;          // true = push the Dread fog onto every player
const INTERVAL_TICKS = 4;    // how often particles are spawned (20 ticks = 1 s)
const LEAF_RADIUS = 16;      // blocks around the player searched for leaves
const LEAF_COLUMNS = 10;     // random columns checked per run
const LEAF_CHANCE = 0.5;     // chance a found leaf block drops a leaf
const DUST_PER_RUN = 2;      // dust density: motes spawned per run
const DUST_RADIUS = 8;       // dust spread around the player (blocks)

const ID = "bedrock_forge:dread_fog", TAG = "bf_dread";
function apply(p) {
  try { p.runCommand(`fog @s remove ${TAG}`); } catch {}
  if (PUSH) { try { p.runCommand(`fog @s push ${ID} ${TAG}`); } catch {} }
}
world.afterEvents.playerSpawn.subscribe((e) => apply(e.player));
system.runTimeout(() => { for (const p of world.getAllPlayers()) apply(p); }, 40);

// ---- ambient particles ----
const LEAVES = {
  "minecraft:oak_leaves": [0.30, 0.45, 0.17], "minecraft:jungle_leaves": [0.28, 0.50, 0.12],
  "minecraft:acacia_leaves": [0.36, 0.44, 0.15], "minecraft:dark_oak_leaves": [0.24, 0.38, 0.13],
  "minecraft:mangrove_leaves": [0.30, 0.44, 0.16], "minecraft:birch_leaves": [0.44, 0.55, 0.30],
  "minecraft:spruce_leaves": [0.25, 0.36, 0.25], "minecraft:azalea_leaves": [0.36, 0.48, 0.20],
  "minecraft:azalea_leaves_flowered": [0.62, 0.45, 0.62], "minecraft:cherry_leaves": [0.93, 0.66, 0.76],
  "minecraft:pale_oak_leaves": [0.62, 0.64, 0.58],
};

function leafColor(id) {
  const c = LEAVES[id] ?? [0.30, 0.44, 0.18];
  const j = 0.9 + Math.random() * 0.2;
  return { red: c[0] * j, green: c[1] * j, blue: c[2] * j };
}

system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    const dim = p.dimension;
    if (dim.id !== "minecraft:overworld") continue;
    const o = p.location;
    // random columns: scan up from the player's feet for the lowest leaf block with air under it
    for (let i = 0; i < LEAF_COLUMNS; i++) {
      const x = Math.floor(o.x + (Math.random() * 2 - 1) * LEAF_RADIUS);
      const z = Math.floor(o.z + (Math.random() * 2 - 1) * LEAF_RADIUS);
      let prevAir = false;
      for (let dy = -3; dy < 22; dy++) {
        const y = Math.floor(o.y) + dy;
        let b;
        try { b = dim.getBlock({ x, y, z }); } catch { break; }
        if (!b) break;
        if (b.typeId.includes("leaves")) {
          if (prevAir && Math.random() < LEAF_CHANCE) {
            const m = new MolangVariableMap();
            m.setColorRGB("variable.leaf", leafColor(b.typeId));
            try { dim.spawnParticle("bedrock_forge:falling_leaf", { x: x + 0.5, y: y - 0.06, z: z + 0.5 }, m); } catch {}
          }
          break;
        }
        prevAir = b.isAir;
      }
    }
    for (let i = 0; i < DUST_PER_RUN; i++) {
      const pos = { x: o.x + (Math.random() * 2 - 1) * DUST_RADIUS, y: o.y + Math.random() * 4 - 0.5, z: o.z + (Math.random() * 2 - 1) * DUST_RADIUS };
      try {
        const b = dim.getBlock(pos);
        if (b && b.isAir) dim.spawnParticle("bedrock_forge:dust_mote", pos);
      } catch {}
    }
  }
}, INTERVAL_TICKS);
"""

# Ambient particles used by BP_SCRIPT (original, MIT) + their 4x4 / 8x8 textures
PARTICLES = {
    "dust_mote": {'format_version': '1.10.0', 'particle_effect': {'description': {'identifier': 'bedrock_forge:dust_mote', 'basic_render_parameters': {'material': 'particles_blend', 'texture': 'textures/particle/bf_dust'}}, 'components': {'minecraft:emitter_rate_instant': {'num_particles': 1}, 'minecraft:emitter_lifetime_once': {'active_time': 0.05}, 'minecraft:emitter_shape_point': {'offset': [0, 0, 0], 'direction': ['math.random(-1,1)', 'math.random(-0.3,0.3)', 'math.random(-1,1)']}, 'minecraft:particle_lifetime_expression': {'max_lifetime': 'math.random(6, 12)'}, 'minecraft:particle_initial_speed': 'math.random(0.02, 0.08)', 'minecraft:particle_motion_dynamic': {'linear_acceleration': ['math.sin(v.particle_age * 40 + v.particle_random_1 * 360) * 0.03', 'math.sin(v.particle_age * 25 + v.particle_random_2 * 360) * 0.02', 'math.cos(v.particle_age * 35 + v.particle_random_3 * 360) * 0.03'], 'linear_drag_coefficient': 0.4}, 'minecraft:particle_appearance_billboard': {'size': ['0.012 + v.particle_random_4 * 0.012', '0.012 + v.particle_random_4 * 0.012'], 'facing_camera_mode': 'lookat_xyz', 'uv': {'texture_width': 4, 'texture_height': 4, 'uv': [0, 0], 'uv_size': [4, 4]}}, 'minecraft:particle_appearance_tinting': {'color': [0.85, 0.82, 0.74, 'math.min(v.particle_age, 1) * math.clamp(v.particle_lifetime - v.particle_age, 0, 1) * 0.55']}, 'minecraft:particle_appearance_lighting': {}}}},
    "falling_leaf": {'format_version': '1.10.0', 'particle_effect': {'description': {'identifier': 'bedrock_forge:falling_leaf', 'basic_render_parameters': {'material': 'particles_alpha', 'texture': 'textures/particle/bf_leaf'}}, 'curves': {}, 'components': {'minecraft:emitter_rate_instant': {'num_particles': 1}, 'minecraft:emitter_lifetime_once': {'active_time': 0.05}, 'minecraft:emitter_shape_box': {'offset': [0, 0, 0], 'half_dimensions': [0.45, 0.02, 0.45], 'direction': [0, -1, 0]}, 'minecraft:particle_lifetime_expression': {'max_lifetime': 'math.random(5, 9)'}, 'minecraft:particle_initial_speed': 0.2, 'minecraft:particle_initial_spin': {'rotation': 'math.random(0, 360)', 'rotation_rate': 'math.random(-90, 90)'}, 'minecraft:particle_motion_dynamic': {'linear_acceleration': ['0.9 + math.sin(v.particle_age * 140 + v.particle_random_1 * 360) * 1.6', '-0.5', '0.5 + math.cos(v.particle_age * 110 + v.particle_random_2 * 360) * 1.6'], 'linear_drag_coefficient': 2.2, 'rotation_acceleration': 'math.sin(v.particle_age * 200 + v.particle_random_3 * 360) * 160', 'rotation_drag_coefficient': 1.0}, 'minecraft:particle_motion_collision': {'collision_radius': 0.04, 'coefficient_of_restitution': 0.0, 'collision_drag': 40, 'expire_on_contact': False}, 'minecraft:particle_appearance_billboard': {'size': ['0.07 + v.particle_random_4 * 0.03', '0.07 + v.particle_random_4 * 0.03'], 'facing_camera_mode': 'rotate_xyz', 'uv': {'texture_width': 8, 'texture_height': 8, 'uv': [0, 0], 'uv_size': [8, 8]}}, 'minecraft:particle_appearance_tinting': {'color': ['v.leaf.r ?? 0.32', 'v.leaf.g ?? 0.46', 'v.leaf.b ?? 0.20', 'math.clamp((v.particle_lifetime - v.particle_age) * 1.5, 0, 1)']}, 'minecraft:particle_appearance_lighting': {}}}},
}
TEXTURES = {
    "bf_dust": "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAJ0lEQVR4nGP8//8/AwMDgz0DBBxkgXKyoAIMTAxogIWBgeEgEv8gACUKBjTsWrqlAAAAAElFTkSuQmCC",
    "bf_leaf": "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAZElEQVR4nF2PQRHEMAwD16USBuESkgqWmEGw6B6tM9fu0yOtxvBg22st8yEAJBlgjHEfI6ICl2231gCYc5KZ2D6mKzPpvZ/23huA11zt27YkS3IR/yGAzDyWmj6UpUzfj16m4gfW4Fc+QhHscgAAAABJRU5ErkJggg==",
}

def manifest(name, desc, kind, version, deps=(), extra=None):
    u = lambda s: str(uuid.uuid5(NS, name + s))
    m = {"format_version": 2,
         "header": {"name": name, "description": desc, "uuid": u("h"), "version": version, "min_engine_version": [1, 21, 90]},
         "modules": [{"type": kind, "uuid": u("m"), "version": version}],
         "metadata": {"authors": ["Bedrock Forge"], "license": "MIT"}}
    if deps: m["dependencies"] = list(deps)
    if extra: m.update(extra)
    return m, u("h")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("rp_root", type=Path); ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--strength", type=float, default=1.0); a = ap.parse_args()
    ver = [1, 0, 0]
    rp = a.out / "DreadAtmosphereRP"; bp = a.out / "DreadAtmosphereBP"
    src = a.rp_root / "vanilla_1.21.90" / "__brarchive"
    for folder, fn, key, default_name, suffix in (
            ("color_grading", grade, "minecraft:color_grading_settings", "color_grading.json", "_color_grading"),
            ("atmospherics", atmos, "minecraft:atmosphere_settings", "atmospherics.json", "_atmospherics"),
            ("lighting", light, "minecraft:lighting_settings", "global.json", "_lighting")):
        d = rp / folder; d.mkdir(parents=True, exist_ok=True)
        for o in objects(src / f"{folder}.brarchive"):
            ident = o[key]["description"]["identifier"]
            if any(n in ident for n in ("hell", "nether", "end_", "crimson", "warped", "basalt", "soulsand")):
                continue  # leave the Nether / End alone
            (d / fname(ident, default_name, suffix)).write_text(json.dumps(fn(o, a.strength), indent=1))
    (rp / "fogs").mkdir(parents=True, exist_ok=True)
    (rp / "fogs" / "dread_fog.json").write_text(json.dumps(FOG, indent=1))
    (rp / "particles").mkdir(parents=True, exist_ok=True)
    for n, o in PARTICLES.items(): (rp / "particles" / f"{n}.json").write_text(json.dumps(o, indent=1))
    (rp / "textures" / "particle").mkdir(parents=True, exist_ok=True)
    for n, b in TEXTURES.items(): (rp / "textures" / "particle" / f"{n}.png").write_bytes(base64.b64decode(b))
    m, rp_uuid = manifest("Dread Atmosphere", "Horror grade for Vibrant Visuals + Dread fog (Bedrock Forge)", "resources", ver,
                          extra={"capabilities": ["pbr"]})
    (rp / "manifest.json").write_text(json.dumps(m, indent=1))
    (bp / "scripts").mkdir(parents=True, exist_ok=True)
    (bp / "scripts" / "main.js").write_text(BP_SCRIPT)
    m, _ = manifest("Dread Atmosphere BP", "Dread fog control + ambient falling leaves and dust (Bedrock Forge)", "script", ver,
                    deps=[{"uuid": rp_uuid, "version": ver}, {"module_name": "@minecraft/server", "version": "2.3.0"}])
    m["modules"][0].update({"language": "javascript", "entry": "scripts/main.js"})
    (bp / "manifest.json").write_text(json.dumps(m, indent=1))
    print("wrote", rp, bp)

if __name__ == "__main__":
    main()
