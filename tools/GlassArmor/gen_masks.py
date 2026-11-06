#!/usr/bin/env python3
"""Glass Armor: build the shape data the RF glass-armor shader needs, from Mojang's vanilla armor textures.

Vanilla armor's shape IS its texture's alpha: geometry.player.armor.* are plain inflated cubes, and the painted pixels
decide what shows (helmet eye slits, chestplate neck notch, boot cut-outs...). So the shader keeps the game's own armor
geometry (exact pose, exact inflate) and only needs, per texel of the 64x32 armor texture:

  * sdf  : signed distance (texels, + inside) to the edge of the painted area, split per cube face, at 8x
           resolution (512x256) -> the neon outline traces the real pixel edges and each face gets its own rim
  * face : the cube face that texel belongs to (x, y, w, h of its rect in texels) -> face-local coordinates for the
           swirl centre / bevel / edge-flow enchant

Input : bedrock-samples resource_pack/textures/models/armor/<mat>_1|2.(png|tga)
Output: <out>/<mat>_<layer>.sdf.f16   (512*256 little-endian float16, row 0 = texture top)
        <out>/face_<layer>.u8         (64*32*4 bytes: rect x, y, w, h; 0,0,0,0 = no face)
        <out>/manifest.json

Usage: python3 -I tools/GlassArmor/gen_masks.py <bedrock-samples>/resource_pack/textures/models/armor <out>
Same algorithm as docs/glass_armor/preview_v6.html (buildSdf), so what was approved in the preview is what ships.
"""
import json
import struct
import sys
from pathlib import Path

from PIL import Image

MATERIALS = {"leather": "leather", "chain": "chain", "iron": "iron", "copper": "copper", "gold": "gold",
             "diamond": "diamond", "netherite": "netherite", "turtle": "turtle"}
F = 8
SW, SH = 64 * F, 32 * F
# geometry.player.armor.base cubes per texture layer: (u, v, w, h, d)
CUBES = {1: [(0, 0, 8, 8, 8), (32, 0, 8, 8, 8), (16, 16, 8, 12, 4), (40, 16, 4, 12, 4), (0, 16, 4, 12, 4)],
         2: [(16, 16, 8, 12, 4), (0, 16, 4, 12, 4)]}


def face_rects(u, v, w, h, d):
    return [(u + d, v, w, d), (u + d + w, v, w, d), (u, v + d, d, h), (u + d, v + d, w, h), (u + d + w, v + d, d, h),
            (u + 2 * d + w, v + d, w, h)]


def face_map(layer):
    fid = [-1] * (64 * 32)
    rects = []
    for c in CUBES[layer]:
        for r in face_rects(*c):
            for y in range(r[1], r[1] + r[3]):
                for x in range(r[0], r[0] + r[2]):
                    fid[y * 64 + x] = len(rects)
            rects.append(r)
    return fid, rects


def chamfer(D, inside, f, is_in):
    def same(i, j):
        return inside[i] == inside[j] and (not inside[i] or f[i] == f[j])
    fw = ((-1, 0, 1.0), (-1, -1, 1.4142), (0, -1, 1.0), (1, -1, 1.4142))
    for y in range(SH):
        for x in range(SW):
            i = y * SW + x
            if inside[i] != is_in:
                continue
            for dx, dy, w in fw:
                X, Y = x + dx, y + dy
                if 0 <= X < SW and Y >= 0:
                    j = Y * SW + X
                    if same(i, j) and D[j] + w < D[i]:
                        D[i] = D[j] + w
    for y in range(SH - 1, -1, -1):
        for x in range(SW - 1, -1, -1):
            i = y * SW + x
            if inside[i] != is_in:
                continue
            for dx, dy, w in fw:
                X, Y = x - dx, y - dy
                if 0 <= X < SW and Y < SH:
                    j = Y * SW + X
                    if same(i, j) and D[j] + w < D[i]:
                        D[i] = D[j] + w


def build_sdf(alpha, layer):
    fid, _ = face_map(layer)
    N = SW * SH
    inside, f = bytearray(N), [-1] * N
    for y in range(SH):
        for x in range(SW):
            tx, ty, i = x // F, y // F, y * SW + x
            f[i] = fid[ty * 64 + tx]
            inside[i] = 1 if f[i] >= 0 and alpha[ty * 64 + tx] else 0
    INF = 1e9
    dIn, dOut = [INF] * N, [INF] * N
    for y in range(SH):
        for x in range(SW):
            i = y * SW + x
            edge = False
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                X, Y = x + dx, y + dy
                if not (0 <= X < SW and 0 <= Y < SH):
                    edge = True
                    break
                j = Y * SW + X
                if not (inside[i] == inside[j] and (not inside[i] or f[i] == f[j])):
                    edge = True
                    break
            if edge:
                if inside[i]:
                    dIn[i] = 0.5
                else:
                    dOut[i] = 0.5
    chamfer(dIn, inside, f, 1)
    chamfer(dOut, inside, f, 0)
    return [min(dIn[i], 64) / F if inside[i] else -min(dOut[i], 64) / F for i in range(N)]


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    man = {"sdf": {"width": SW, "height": SH, "format": "float16", "units": "texels, + inside"},
           "face": {"width": 64, "height": 32, "format": "rgba8 = rect x,y,w,h"}, "materials": {}}
    for layer in (1, 2):
        fid, rects = face_map(layer)
        fm = bytearray(64 * 32 * 4)
        for i, k in enumerate(fid):
            if k >= 0:
                fm[i * 4:i * 4 + 4] = bytes(rects[k])
        (out / f"face_{layer}.u8").write_bytes(bytes(fm))
    for mat, stem in MATERIALS.items():
        man["materials"][mat] = []
        for layer in (1, 2):
            p = next((src / f"{stem}_{layer}{ext}" for ext in (".png", ".tga") if (src / f"{stem}_{layer}{ext}").exists()), None)
            if p is None:
                continue
            im = Image.open(p).convert("RGBA")
            if im.size != (64, 32):
                print(f"skip {p.name}: {im.size}")
                continue
            alpha = [1 if im.getpixel((x, y))[3] > 0 else 0 for y in range(32) for x in range(64)]
            sdf = build_sdf(alpha, layer)
            (out / f"{mat}_{layer}.sdf.f16").write_bytes(b"".join(struct.pack("<e", v) for v in sdf))
            man["materials"][mat].append(layer)
            print(f"{mat}_{layer}: {sum(alpha)} painted texels")
    (out / "manifest.json").write_text(json.dumps(man, indent=1))


if __name__ == "__main__":
    main()
