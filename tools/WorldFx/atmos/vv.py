#!/usr/bin/env python3
"""Vibrant Visuals layer of an atmosphere's resource pack (by Novak).

    python3 -I tools/WorldFx/atmos/vv.py cloudsea        (build_pack.py also runs it)

Writes the VV data files into packs/WorldFx<Name>/: atmospherics (sky colours, sun glow), lighting (sun / moon colour
and strength), the volumetric fog (a cloud deck under the islands), colour grading, and every overworld biome routed
to them. This is the part that works for everyone with VV on - the Hive included, no Bedrock Forge needed.

With Renderer Foundation on top, RF draws the real Cloud Sea sky in VV and drives these same systems live from the
match (RF src/VvLive.cpp: the deck is re-anchored under the map's islands, the air takes a broken bed's colour, the
clouds rise for the final fight). These files are the base it starts from, tuned to read as the same atmosphere.

Facts these values rest on (tested in game on 1.26.51, Research/VibrantVisuals/docs/VV_TESTED.md):
  * keyframe time: 0.0 = noon, 0.25 = sunset, 0.5 = midnight, 0.75 = sunrise
  * 1.26.51 has no data-driven VV defaults: routing a system replaces the engine's own values entirely
  * the horizon colour is also the colour distant terrain fades into; the sun's glow mixes it with the zenith's
  * the light's colour tints everything, the sun disc and water highlights included
  * fog density is gentle: 0.35 reads as light haze - a deck needs 1-3
  * colour temperature: lower K = warmer
  * packs are read once per game session: restart the game to see a change
"""
from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
TPL = HERE / "vv_templates"
NS = "novak_cs"


def kf(d):
    return {f"{k:.4f}": v for k, v in sorted(d.items())}


def load(name):
    return json.loads((TPL / name).read_text(encoding="utf-8"))


def mirror(d):
    """sunset keys (0.0 .. 0.5) mirrored onto sunrise (0.5 .. 1.0): the same day backwards"""
    out = dict(d)
    for k, v in d.items():
        if 0.0 < k < 0.5:
            out[round(1.0 - k, 4)] = v
    if 0.0 in d:
        out[1.0] = d[0.0]
    return out


def cloudsea():
    """Cloud Sea: a sunset world floating over a sea of cloud. Its RF sky is a permanent golden hour that sinks into
    dusk and a moonlit night as the match goes on; on a plain VV client the game's own clock runs the day instead, so
    every hour of it is pulled toward that palette - noon is warm and low-contrast (late afternoon, never blue-white),
    sunset is the signature amber over a violet zenith, and the night is a bright moonlit navy (never black)."""
    a = load("atmospherics.json")
    s = a["minecraft:atmosphere_settings"]
    s["description"]["identifier"] = f"{NS}:atmospherics"
    s["sky_zenith_color"] = kf(mirror({0.0: [54, 92, 172], 0.14: [62, 84, 160], 0.20: [74, 70, 148], 0.25: [70, 52, 128],
                                       0.29: [44, 34, 96], 0.34: [22, 24, 66], 0.5: [12, 18, 48]}))
    s["sky_horizon_color"] = kf(mirror({0.0: [255, 222, 186], 0.12: [255, 206, 160], 0.19: [255, 168, 104], 0.235: [255, 132, 60],
                                        0.26: [240, 98, 64], 0.29: [150, 72, 104], 0.33: [74, 66, 118], 0.5: [48, 58, 108]}))
    s["sun_mie_strength"] = kf(mirror({0.0: 1.4, 0.18: 2.6, 0.25: 4.0, 0.30: 1.6, 0.5: 0.0}))
    s["sun_glare_shape"] = kf(mirror({0.0: 9.0, 0.20: 5.0, 0.25: 3.2, 0.30: 6.0, 0.5: 6.0}))
    s["moon_mie_strength"] = kf(mirror({0.0: 0.0, 0.27: 0.0, 0.32: 1.6, 0.5: 1.8}))
    s["rayleigh_strength"] = kf(mirror({0.0: 6.0, 0.2: 5.0, 0.25: 4.0, 0.33: 4.0, 0.5: 4.0}))

    li = load("lighting.json")
    ls = li["minecraft:lighting_settings"]
    ls["description"]["identifier"] = f"{NS}:lighting"
    o = ls["directional_lights"]["orbital"]
    o["sun"]["color"] = kf(mirror({0.0: [255, 232, 206], 0.15: [255, 208, 156], 0.22: [255, 164, 96], 0.25: [255, 136, 70], 0.28: [255, 120, 80]}))
    # the moon: vanilla 0.4 -> 2.5 (night stays readable, the big full moon really lights the world);
    # amber while low, silver once it is up
    o["moon"]["illuminance"] = kf({0.0: 0.0, 0.2: 0.0, 0.24: 1.2, 0.32: 2.5, 0.68: 2.5, 0.76: 1.2, 0.8: 0.0, 1.0: 0.0})
    o["moon"]["color"] = kf({0.0: [255, 170, 110], 0.24: [255, 170, 110], 0.34: [214, 218, 255], 0.66: [214, 218, 255],
                             0.76: [255, 170, 110], 1.0: [255, 170, 110]})
    ls["ambient"] = {"color": "#FFE2C4", "illuminance": 0.06}

    fo = load("fog.json")
    fs = fo["minecraft:fog_settings"]
    fs["description"]["identifier"] = f"{NS}:fog"
    # the cloud deck: thick at y 10 and below, gone by y 46 - Hive Bed Wars / Sky Wars islands float well above it.
    # Warm forward scattering: the low sun lights it from inside and throws rays through the haze over it.
    fs["volumetric"] = {
        "density": {"air": {"max_density": 1.6, "max_density_height": 10.0, "zero_density_height": 46.0}},
        "media_coefficients": {"air": {"scattering": [0.10, 0.09, 0.08], "absorption": [0.0, 0.004, 0.008]}},
        "henyey_greenstein_g": {"air": {"henyey_greenstein_g": 0.80}},
    }

    g = load("color_grading.json")
    gs = g["minecraft:color_grading_settings"]
    gs["description"]["identifier"] = f"{NS}:color_grading"
    gs["color_grading"]["midtones"]["saturation"] = [1.12, 1.10, 1.08]
    gs["color_grading"]["midtones"]["contrast"] = [1.10, 1.10, 1.10]
    gs["color_grading"]["temperature"]["temperature"] = 5600
    gs["tone_mapping"]["operator"] = "hable"
    return {"atmospherics/cloud_sea.json": a, "lighting/cloud_sea.json": li, "fogs/cloud_sea_fog.json": fo,
            "color_grading/cloud_sea.json": g}


LOOKS = {"cloudsea": cloudsea}
ROUTE = {  # client_biome component -> (field, identifier)
    "minecraft:atmosphere_identifier": ("atmosphere_identifier", f"{NS}:atmospherics"),
    "minecraft:lighting_identifier": ("lighting_identifier", f"{NS}:lighting"),
    "minecraft:color_grading_identifier": ("color_grading_identifier", f"{NS}:color_grading"),
    "minecraft:fog_appearance": ("fog_identifier", f"{NS}:fog"),
}


def write(pack: Path, look: str) -> int:
    files = LOOKS[look]()
    for b in sorted((TPL / "biomes").glob("*.client_biome.json")):   # every overworld biome 1.26.51 knows (no unknown names)
        d = json.loads(b.read_text(encoding="utf-8"))
        c = d["minecraft:client_biome"].setdefault("components", {})
        for comp, (field, ident) in ROUTE.items():
            c[comp] = {field: ident}
        c.pop("minecraft:water_identifier", None)   # water stays the game's own
        files["biomes/" + b.name] = d
    for rel, obj in files.items():
        p = pack / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8", newline="\n")
    mf = pack / "manifest.json"
    if mf.exists():   # VV data needs the pbr capability (not "raytraced": that greys the pack out in VV)
        m = json.loads(mf.read_text())
        caps = m.setdefault("capabilities", [])
        if "pbr" not in caps:
            caps.append("pbr")
        mf.write_text(json.dumps(m, indent=2) + "\n")
    return len(files)


if __name__ == "__main__":
    look = sys.argv[1] if len(sys.argv) > 1 else "cloudsea"
    pack = ROOT / "packs" / {"cloudsea": "WorldFxCloudSea"}[look]
    print(f"{write(pack, look)} VV files written to {pack}")
