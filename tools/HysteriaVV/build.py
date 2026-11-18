#!/usr/bin/env python3
"""Hysteria VV - Vibrant Visuals settings RP that pairs with the private HysteriaVV shaderpack (real VV shadows).

  python build.py <game data/resource_packs dir> -o <out dir>

Reads the vanilla 1.21.90 lighting / shadow brarchives and writes "Hysteria VV" (RP, capabilities pbr):
  - shadows: soft shadows instead of blocky (Photon/Hysteria use filtered soft shadows)
  - lighting (every overworld biome file): sun path tilted like Hysteria's sunPathRotation, daytime sun colour =
    Hysteria's noon tint x sunlight_color, moon colour = Hysteria's moon tint, moon:sun illuminance ratio =
    Hysteria's moon exposure / sun exposure. Sunrise/sunset keyframes stay vanilla (VV's own physics).
Atmospherics and colour grading stay vanilla: the HysteriaVV ColorPostProcessing material replaces VV's grade and
tonemapper with Hysteria's, so VV's color_grading numbers no longer reach the screen (exposure still does).
Original work for Bedrock Forge (MIT). Ships only numbers, no vanilla files and no Hysteria code.
"""
import argparse, json, uuid
from pathlib import Path

NS = uuid.UUID("2d6c1f0e-8a41-4b7e-9c55-4f1d3a9e7b02")

# --- numbers resolved from Hysteria v1.2.1 defaults (see the private port's common.hlsli) -----------------------
SUN_PATH_ROTATION_DEG = 40.0            # sunPathRotation = -40 (orbit tilt; sign: VV offsets the orbit the other way)
SUNLIGHT_COLOR = (1.051, 0.985, 0.940)  # linear
NOON_TINT_SRGB = (0.74, 0.70, 0.72)     # sunTintNoon
MOON_TINT_SRGB = (1.00, 0.74, 0.30)     # moonTint
SUN_EXPOSURE = 7.0 * 0.85               # base_scale
MOON_EXPOSURE = 0.66 * 0.20             # full moon
NOON_KEYS = ("0.000000", "1.000000")    # VV lighting keyframes: 0 / 1 = noon


def objects(path):
    t = Path(path).read_bytes().decode("latin1"); out = []; i = 0; dec = json.JSONDecoder()
    while (i := t.find('{"format_version"', i)) >= 0:
        o, e = dec.raw_decode(t[i:]); out.append(o); i += e
    return out


def tint_rgb255(srgb_tint, mul=(1.0, 1.0, 1.0)):
    lin = [(c ** 2.2) * m for c, m in zip(srgb_tint, mul)]
    top = max(lin)
    return [round(255 * (x / top) ** (1 / 2.2)) for x in lin]


def light(o):
    s = o["minecraft:lighting_settings"]
    orb = s.get("directional_lights", {}).get("orbital")
    if not isinstance(orb, dict): return o
    orb["orbital_offset_degrees"] = SUN_PATH_ROTATION_DEG
    sun, moon = orb.get("sun", {}), orb.get("moon", {})
    noon = tint_rgb255(NOON_TINT_SRGB, SUNLIGHT_COLOR)
    if isinstance(sun.get("color"), dict):
        for k in NOON_KEYS: sun["color"][k] = noon
    if isinstance(moon.get("color"), dict):
        mc = tint_rgb255(MOON_TINT_SRGB, SUNLIGHT_COLOR)
        moon["color"] = {k: mc for k in moon["color"]}
    ill = sun.get("illuminance")
    sun_peak = max(ill.values()) if isinstance(ill, dict) else ill
    if isinstance(moon.get("illuminance"), dict) and sun_peak:
        target = sun_peak * MOON_EXPOSURE / SUN_EXPOSURE
        mpeak = max(moon["illuminance"].values()) or 1.0
        moon["illuminance"] = {k: round(v / mpeak * target, 4) for k, v in moon["illuminance"].items()}
    return o


def manifest(name, desc, version):
    u = lambda s: str(uuid.uuid5(NS, name + s))
    return {"format_version": 2,
            "header": {"name": name, "description": desc, "uuid": u("h"), "version": version, "min_engine_version": [1, 21, 90]},
            "modules": [{"type": "resources", "uuid": u("m"), "version": version}],
            "capabilities": ["pbr"],
            "metadata": {"authors": ["Bedrock Forge"], "license": "MIT"}}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("rp_root", type=Path); ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()
    src = a.rp_root / "vanilla_1.21.90" / "__brarchive"
    rp = a.out / "HysteriaVVRP"
    (rp / "shadows").mkdir(parents=True, exist_ok=True)
    sh = objects(src / "shadows.brarchive")[0]
    sh["minecraft:shadow_settings"] = {"shadow_style": "soft_shadows"}
    (rp / "shadows" / "global.json").write_text(json.dumps(sh, indent=1))
    (rp / "lighting").mkdir(parents=True, exist_ok=True)
    n = 0
    for o in objects(src / "lighting.brarchive"):
        ident = o["minecraft:lighting_settings"]["description"]["identifier"]
        if any(x in ident for x in ("nether", "end_")): continue  # Nether / End untouched
        short = ident.split(":", 1)[1]
        (rp / "lighting" / ("global.json" if short == "default_lighting" else short + ".json")).write_text(json.dumps(light(o), indent=1))
        n += 1
    (rp / "manifest.json").write_text(json.dumps(manifest("Hysteria VV", "Vibrant Visuals settings for the HysteriaVV shaderpack (Bedrock Forge)", [1, 0, 0]), indent=1))
    print(f"wrote {rp}: soft shadows, {n} lighting files")


if __name__ == "__main__":
    main()
