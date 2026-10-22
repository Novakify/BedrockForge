#!/usr/bin/env python3
"""Dread Atmosphere - build a Vibrant Visuals horror grade from the game's own vanilla settings.

  python build.py <game data/resource_packs dir> -o <out dir>

Reads the vanilla color_grading / atmospherics / lighting settings (brarchives), applies the Dread transform to every
biome group (desaturated, darker, colder, lower sun, darker nights) and writes an RP + a tiny BP that pushes the Dread
fog (distance fog for Fancy graphics, volumetric fog for Vibrant Visuals) onto every player.
Original work for Bedrock Forge (MIT). Only numbers derived from vanilla defaults are shipped, no vanilla files.
"""
import argparse, json, uuid
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

BP_SCRIPT = """// Dread Atmosphere - pushes the Dread fog onto every player (original, MIT).
import { world, system } from "@minecraft/server";
const ID = "bedrock_forge:dread_fog", TAG = "bf_dread";
function apply(p) { try { p.runCommand(`fog @s remove ${TAG}`); p.runCommand(`fog @s push ${ID} ${TAG}`); } catch {} }
world.afterEvents.playerSpawn.subscribe((e) => apply(e.player));
system.runTimeout(() => { for (const p of world.getAllPlayers()) apply(p); }, 40);
"""

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
    m, rp_uuid = manifest("Dread Atmosphere", "Horror grade for Vibrant Visuals + Dread fog (Bedrock Forge)", "resources", ver,
                          extra={"capabilities": ["pbr"]})
    (rp / "manifest.json").write_text(json.dumps(m, indent=1))
    (bp / "scripts").mkdir(parents=True, exist_ok=True)
    (bp / "scripts" / "main.js").write_text(BP_SCRIPT)
    m, _ = manifest("Dread Atmosphere BP", "Applies the Dread fog to every player (Bedrock Forge)", "script", ver,
                    deps=[{"uuid": rp_uuid, "version": ver}, {"module_name": "@minecraft/server", "version": "2.3.0"}])
    m["modules"][0].update({"language": "javascript", "entry": "scripts/main.js"})
    (bp / "manifest.json").write_text(json.dumps(m, indent=1))
    print("wrote", rp, bp)

if __name__ == "__main__":
    main()
