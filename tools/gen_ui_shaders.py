#!/usr/bin/env python3
"""Spatial GUI M0 spike: write config/ui_shaders.txt for Renderer Foundation.

  python gen_ui_shaders.py <game data/renderer/materials> <out ui_shaders.txt> [--lz <dir with Bedrock Forge's lz.py>]

FNV-1a 64 of every SM6.0 / SM6.5 fragment shader (raw DXIL container) in the vanilla UI* (JSON UI) and Gameface*
(OreUI) materials. RF hashes the pixel shader of every pipeline the game creates and tags the matches as UI.
Needs lazurite (pip install lazurite) + Bedrock Forge's tools/ShaderForge/lz.py (1.26 material layout fix).
"""
import argparse, glob, os, sys
from collections import Counter


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("materials"); ap.add_argument("out"); ap.add_argument("--lz", default=".")
    a = ap.parse_args()
    sys.path.insert(0, a.lz)
    import lz  # noqa: E402
    rows, seen = [], set()
    for path in sorted(glob.glob(os.path.join(a.materials, "*.material.bin"))):
        name = os.path.basename(path)[: -len(".material.bin")]
        group = "jsonui" if name.startswith("UI") else "gameface" if name.startswith("Gameface") else None
        if not group:
            continue
        m = lz.load(path)
        for p in m.passes:
            for v in p.variants:
                for s in v.shaders:
                    if s.stage.name != "Fragment" or s.platform.name not in ("Direct3D_SM60", "Direct3D_SM65"):
                        continue
                    h = fnv1a64(bytes(s.bgfx_shader.shader_bytes))
                    if h in seen:
                        continue
                    seen.add(h)
                    rows.append(f"{h:016x} {group} {name}/{p.name} {s.platform.name}")
    hdr = ["# Renderer Foundation - Spatial GUI M0 spike: FNV-1a 64 of every vanilla UI fragment shader (DXIL).",
           "# Generated from data/renderer/materials (UI* = JSON UI, Gameface* = OreUI). Format: <hash> <group> <material>/<pass> <platform>"]
    with open(a.out, "w") as f:
        f.write("\n".join(hdr + rows) + "\n")
    print(len(rows), dict(Counter(r.split()[1] for r in rows)))


if __name__ == "__main__":
    main()
