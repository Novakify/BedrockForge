#!/usr/bin/env python3
"""Shine for Bedrock - asset conversion (Shine by tapeQ; conversion made with the author's permission, see PERMISSION.md).

    python tools/convert_assets.py <shine-3.0.jar> -o build/ShineBedrockRP

Builds a Bedrock resource pack from the jar's own assets. The output contains tapeQ's assets and is NEVER committed
(build/ is gitignored); only this script and the docs live in git.

  sounds      assets/shine/sounds.json + .ogg        -> sounds/shine/**, sounds/sound_definitions.json ("shine.<key>")
  particles   assets/shine/particles/*.json + frames  -> textures/shine/particle/<name>.png (grid atlas, frames downscaled
                                                         to fit) + particles/shine/<name>.json (Bedrock particle with a
                                                         molang-indexed sprite; motion/lifetime = placeholders, tuned later)
  textures    environment / effect(s) / block          -> textures/shine/** (used by the shader + sky phases)
"""
from __future__ import annotations

import argparse
import io
import json
import math
import shutil
import uuid
import zipfile
from pathlib import Path

from PIL import Image

MAX_ATLAS = 2048          # keep particle atlases modest for Bedrock
MAX_FRAME = 128           # downscale bigger sprites (fog/mist/dust are 256-512 px)


def load_json(data: bytes):
    return json.loads(data.decode("utf-8-sig"))


def manifest(name: str, desc: str) -> dict:
    return {
        "format_version": 2,
        "header": {"name": name, "description": desc, "uuid": str(uuid.uuid4()), "version": [3, 0, 0],
                   "min_engine_version": [1, 21, 0]},
        "modules": [{"type": "resources", "uuid": str(uuid.uuid4()), "version": [3, 0, 0]}],
        "metadata": {"authors": ["tapeQ"]},
    }


def convert_sounds(z: zipfile.ZipFile, out: Path, report: list[str]):
    defs = load_json(z.read("assets/shine/sounds.json"))
    sd = {}
    for key, entry in defs.items():
        sounds = []
        for s in entry.get("sounds", []):
            s = {"name": s} if isinstance(s, str) else dict(s)
            ns, path = s["name"].split(":", 1)
            src = f"assets/{ns}/sounds/{path}.ogg"
            if src not in z.namelist():
                report.append(f"sound missing: {src}")
                continue
            dst = out / "sounds" / ns / f"{path}.ogg"
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(z.read(src))
            b = {"name": f"sounds/{ns}/{path}"}
            for k_java, k_bed in (("volume", "volume"), ("pitch", "pitch"), ("weight", "weight"), ("stream", "stream")):
                if k_java in s:
                    b[k_bed] = s[k_java]
            sounds.append(b)
        if sounds:
            sd[f"shine.{key}"] = {"category": "ambient", "sounds": sounds}
    (out / "sounds").mkdir(parents=True, exist_ok=True)
    (out / "sounds" / "sound_definitions.json").write_text(
        json.dumps({"format_version": "1.14.0", "sound_definitions": sd}, indent=2))
    report.append(f"sounds: {len(sd)} events")


def atlas(frames: list[Image.Image]) -> tuple[Image.Image, int, int, int]:
    """Grid atlas; returns (image, cell size, columns, rows)."""
    cell = max(max(f.size) for f in frames)
    cell = min(cell, MAX_FRAME)
    n = len(frames)
    cols = max(1, min(n, MAX_ATLAS // cell))
    rows = math.ceil(n / cols)
    while rows * cell > MAX_ATLAS and cell > 8:
        cell //= 2
        cols = max(1, min(n, MAX_ATLAS // cell))
        rows = math.ceil(n / cols)
    img = Image.new("RGBA", (cols * cell, rows * cell), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        f = f.convert("RGBA")
        if max(f.size) > cell:
            s = cell / max(f.size)
            f = f.resize((max(1, round(f.width * s)), max(1, round(f.height * s))), Image.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * cell
        img.paste(f, (x + (cell - f.width) // 2, y + (cell - f.height) // 2))
    return img, cell, cols, rows


def particle_json(name: str, n: int, cell: int, cols: int, rows: int) -> dict:
    w, h = cols * cell, rows * cell
    # one random sprite per particle (Shine picks variants); animated sets can switch index to age-based later
    idx = f"math.floor(variable.particle_random_1 * {n})" if n > 1 else "0"
    return {
        "format_version": "1.10.0",
        "particle_effect": {
            "description": {
                "identifier": f"shine:{name}",
                "basic_render_parameters": {"material": "particles_blend", "texture": f"textures/shine/particle/{name}"},
            },
            "components": {
                "minecraft:emitter_rate_instant": {"num_particles": 1},
                "minecraft:emitter_lifetime_once": {"active_time": 0.05},
                "minecraft:emitter_shape_point": {},
                "minecraft:particle_lifetime_expression": {"max_lifetime": "2 + variable.particle_random_2 * 2"},
                "minecraft:particle_initial_speed": 0.2,
                "minecraft:particle_motion_dynamic": {"linear_acceleration": [0, -0.2, 0], "linear_drag_coefficient": 1.0},
                "minecraft:particle_appearance_billboard": {
                    "size": ["0.08", "0.08"],
                    "facing_camera_mode": "rotate_xyz",
                    "uv": {
                        "texture_width": w, "texture_height": h,
                        "uv": [f"math.mod({idx}, {cols}) * {cell}", f"math.floor({idx} / {cols}) * {cell}"],
                        "uv_size": [cell, cell],
                    },
                },
                "minecraft:particle_appearance_lighting": {},
            },
        },
    }


def convert_particles(z: zipfile.ZipFile, out: Path, report: list[str]):
    names = sorted(n for n in z.namelist() if n.startswith("assets/shine/particles/") and n.endswith(".json"))
    for entry in names:
        name = Path(entry).stem
        tex = load_json(z.read(entry)).get("textures", [])
        frames = []
        for t in tex:
            ns, path = t.split(":", 1)
            src = f"assets/{ns}/textures/particle/{path}.png"
            if src in z.namelist():
                frames.append(Image.open(io.BytesIO(z.read(src))))
            else:
                report.append(f"particle {name}: frame not in jar ({t}) - vanilla texture, map by hand")
        if not frames:
            continue
        img, cell, cols, rows = atlas(frames)
        tp = out / "textures" / "shine" / "particle" / f"{name}.png"
        tp.parent.mkdir(parents=True, exist_ok=True)
        img.save(tp)
        pp = out / "particles" / "shine" / f"{name}.json"
        pp.parent.mkdir(parents=True, exist_ok=True)
        pp.write_text(json.dumps(particle_json(name, len(frames), cell, cols, rows), indent=2))
        report.append(f"particle {name}: {len(frames)} sprites -> {img.width}x{img.height} (cell {cell})")


def copy_textures(z: zipfile.ZipFile, out: Path, report: list[str]):
    n = 0
    for src in z.namelist():
        if not src.startswith("assets/shine/textures/") or src.endswith("/"):
            continue
        rel = src[len("assets/shine/textures/"):]
        if rel.startswith(("particle/", "gui/")):
            continue  # particles are atlased above; config-screen art isn't needed on Bedrock
        dst = out / "textures" / "shine" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(z.read(src))
        n += 1
    report.append(f"textures copied: {n}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jar")
    ap.add_argument("-o", "--out", default="build/ShineBedrockRP")
    a = ap.parse_args()
    out = Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    report: list[str] = []
    with zipfile.ZipFile(a.jar) as z:
        meta = load_json(z.read("fabric.mod.json"))
        (out / "manifest.json").write_text(json.dumps(manifest(
            f"Shine {meta['version']} (Bedrock conversion)",
            "Shine: VFX by tapeQ - Bedrock conversion, used with permission. All rights reserved by tapeQ."), indent=2))
        if "assets/shine/shine4.png" in z.namelist():
            (out / "pack_icon.png").write_bytes(z.read("assets/shine/shine4.png"))
        convert_sounds(z, out, report)
        convert_particles(z, out, report)
        copy_textures(z, out, report)
    (out.parent / f"{out.name}.report.txt").write_text("\n".join(report) + "\n")
    print("\n".join(r for r in report if not r.startswith("particle ") or "not in jar" in r)[:4000])
    print(f"-> {out}")


if __name__ == "__main__":
    main()
