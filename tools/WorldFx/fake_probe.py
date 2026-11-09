#!/usr/bin/env python3
"""Write a FAKE World FX probe folder (same files src/WorldFx.cpp writes) to test analyze_probe.py without the game.

A small voxel Bed Wars island (end stone, team wool, concrete pillars, a glass wall, an oak bridge) is ray-cast with
the tag pack's real textures, then written in the probe's v2 layout: two stage snapshots (before the first Actor draw,
before the hand pass), the HUD boundary (colour_at_ui BGRA8 like the swap chain, world_depth D24 standard Z), a fake
terrain atlas holding every tagged texture (RGBA8) + stamps.json, and a report.txt with a frame timeline.

    python3 -I tools/WorldFx/fake_probe.py /tmp/fakeprobe && python3 -I tools/WorldFx/analyze_probe.py /tmp/fakeprobe
    python3 -I tools/WorldFx/fake_probe.py /tmp/fp2 --hand-clears-depth --no-atlas-picture   # the awkward cases
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_stamps  # noqa: E402  (fake_atlas, load)
import wfx_stamps  # noqa: E402

NEAR, FAR = 0.05, 1000.0


def world() -> tuple[np.ndarray, list[str]]:
    names = ["air", "end_stone", "wool_colored_red", "concrete_blue", "glass", "planks_oak", "obsidian", "wool_colored_lime"]
    g = np.zeros((32, 16, 32), np.int32)  # x, y, z
    g[4:20, 0:3, 6:22] = 1                   # island
    g[4:20, 3, 6:22] = 2                     # red wool top
    g[6:9, 4:9, 16:19] = 3                   # blue concrete pillar
    g[12:15, 4:7, 18:20] = 6                 # obsidian bed defence
    g[8:18, 4:8, 10] = 4                     # glass wall
    g[19:30, 3, 12:15] = 5                   # oak bridge
    g[26:31, 0:4, 8:20] = 7                  # lime island across the bridge
    return g, names


def render(w=960, h=540):
    grid, names = world()
    ids, tex = test_stamps.load()
    by_name = {v["name"]: tex[i] for i, v in ids.items()}
    texs = [None] + [by_name[n] for n in names[1:]]
    eye = np.array([1.5, 9.5, -6.0])
    yaw, pitch, fov = np.radians(24), np.radians(-24), np.radians(70)
    fwd = np.array([np.sin(yaw) * np.cos(pitch), np.sin(pitch), np.cos(yaw) * np.cos(pitch)])
    right = np.cross([0, 1, 0], fwd)
    right /= np.linalg.norm(right)
    up = np.cross(fwd, right)
    ys, xs = np.mgrid[0:h, 0:w]
    t = np.tan(fov / 2)
    px = (2 * (xs + 0.5) / w - 1) * t * w / h
    py = (1 - 2 * (ys + 0.5) / h) * t
    d = fwd[None, None] + px[..., None] * right + py[..., None] * up
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    n = h * w
    D = d.reshape(n, 3)
    pos = np.tile(eye, (n, 1))
    cell = np.floor(pos).astype(np.int64)
    step = np.sign(D).astype(np.int64)
    step[step == 0] = 1
    with np.errstate(divide="ignore"):
        inv = np.where(D != 0, 1.0 / D, 1e30)
    tmax = ((cell + (step > 0)) - pos) * inv
    tdelta = np.abs(inv)
    alive = np.ones(n, bool)
    hit_t = np.full(n, np.inf)
    colour = np.zeros((n, 3))
    face = np.zeros(n, np.int64)
    for _ in range(140):
        idx = np.nonzero(alive)[0]
        if not len(idx):
            break
        ax = np.argmin(tmax[idx], axis=1)
        tt = tmax[idx, ax]
        cell[idx, ax] += step[idx, ax]
        tmax[idx, ax] += tdelta[idx, ax]
        face[idx] = ax
        c = cell[idx]
        inside = (c[:, 0] >= 0) & (c[:, 0] < grid.shape[0]) & (c[:, 1] >= 0) & (c[:, 1] < grid.shape[1]) & (c[:, 2] >= 0) & (c[:, 2] < grid.shape[2])
        far = (tt > 120) | ((~inside) & (c[:, 1] < -2))
        alive[idx[far]] = False
        ok = idx[inside & ~far]
        cc = cell[ok]
        b = grid[cc[:, 0], cc[:, 1], cc[:, 2]]
        for bid in np.unique(b[b > 0]):
            sel = ok[b == bid]
            ax2 = face[sel]
            tt2 = tmax[sel, ax2] - tdelta[sel, ax2]
            p = pos[sel] + D[sel] * tt2[:, None]
            # face UV
            u = np.where(ax2 == 0, p[:, 2], p[:, 0]) % 1.0
            v = np.where(ax2 == 1, p[:, 2], 1 - (p[:, 1] % 1.0)) % 1.0
            tx = texs[bid]
            th, tw = tx.shape[:2]
            s = tx[np.clip((v * th).astype(int), 0, th - 1), np.clip((u * tw).astype(int), 0, tw - 1)]
            solid = s[:, 3] > 0
            shade = np.where(ax2 == 1, 1.0, np.where(ax2 == 0, 0.78, 0.62))
            fin = sel[solid]
            colour[fin] = s[solid, :3] * shade[solid, None]
            hit_t[fin] = tt2[solid]
            alive[fin] = False
    sky = ~np.isfinite(hit_t)
    sky_col = np.array([120, 170, 255]) * (0.75 + 0.25 * np.clip(D[:, 1] + 0.3, 0, 1))[:, None]
    colour[sky] = sky_col[sky]
    zview = hit_t * (D @ fwd)
    depth = np.where(sky, 1.0, FAR / (FAR - NEAR) * (1 - NEAR / np.maximum(zview, NEAR)))
    return colour.reshape(h, w, 3).clip(0, 255).astype(np.uint8), depth.reshape(h, w)


def write_raw(folder: Path, name: str, rows: np.ndarray, layout: str, fmt: str, w: int, h: int, atlas=False, saved=True):
    rb = rows.shape[1]
    if saved:
        (folder / f"{name}.raw").write_bytes(np.ascontiguousarray(rows).tobytes())
    (folder / f"{name}.json").write_text(json.dumps({"name": name, "width": w, "height": h, "rowBytes": rb, "rows": h,
                                                     "copyFormat": fmt, "resourceFormat": fmt, "layout": layout, "mips": 5 if atlas else 1,
                                                     "array": 1, "slice": 0, "atlas": atlas, "saved": saved}) + "\n")


def d24(depth: np.ndarray) -> np.ndarray:
    h, w = depth.shape
    return (np.clip(depth, 0, 1) * 16777215).astype(np.uint32).view(np.uint8).reshape(h, w * 4)


def bgra(rgb: np.ndarray) -> np.ndarray:
    h, w = rgb.shape[:2]
    return np.dstack([rgb[..., 2], rgb[..., 1], rgb[..., 0], np.full((h, w), 255, np.uint8)]).reshape(h, w * 4)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", default="/tmp/fakeprobe")
    ap.add_argument("--hand-clears-depth", action="store_true",
                    help="simulate a hand pass that clears depth first: the HUD-boundary depth then only holds the hand")
    ap.add_argument("--no-atlas-picture", action="store_true", help="like the probe's default: stamps.json only, no atlas pixels")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    rgb, depth = render()
    h, w = depth.shape
    # the hand: a slab in the lower right, very near
    hand = np.zeros((h, w), bool)
    hand[int(h * 0.62):, int(w * 0.70): int(w * 0.92)] = True
    rgb_hand = rgb.copy()
    rgb_hand[hand] = (201, 150, 120)
    cleared = np.ones_like(depth)
    depth_hud = np.where(hand, 0.02, cleared if a.hand_clears_depth else depth)
    depth_before_hand = cleared if a.hand_clears_depth else depth
    # stage 1: before the first Actor draw (terrain done) | stage 2: before the hand | then the HUD boundary
    write_raw(out, "stage1_before_Actor_colour", bgra(rgb), "bgra8", "BGRA8", w, h)
    write_raw(out, "stage1_before_Actor_depth", d24(depth), "depth24", "D24S8", w, h)
    write_raw(out, "stage2_before_ItemInHand_colour", bgra(rgb), "bgra8", "BGRA8", w, h)
    write_raw(out, "stage2_before_ItemInHand_depth", d24(depth_before_hand), "depth24", "D24S8", w, h)
    write_raw(out, "colour_at_ui", bgra(rgb_hand), "bgra8", "BGRA8", w, h)
    write_raw(out, "world_depth", d24(depth_hud), "depth24", "D24S8", w, h)
    ids, tex = test_stamps.load()
    atlas, _ = test_stamps.fake_atlas(tex)
    write_raw(out, "atlas0", atlas.reshape(atlas.shape[0], -1), "rgba8", "RGBA8", atlas.shape[1], atlas.shape[0], atlas=True,
              saved=not a.no_atlas_picture)
    hits = wfx_stamps.find(atlas)
    for x in hits:
        x["copy"] = "atlas0"
    (out / "stamps.json").write_text(json.dumps(hits, indent=1) + "\n")
    vs_out = "SV_Position0.xyzw@o0 COLOR1.xyzw@o1 COLOR0.xyzw@o2 TEXCOORD2.xy@o3 TEXCOORD1.zw@o3 COLOR2.xyzw@o4 TEXCOORD0.xy@o5 TEXCOORD3.xyz@o6 COLOR3.xyzw@o7"
    (out / "report.txt").write_text(f"""# World FX M0 probe v2 (FAKE, tools/WorldFx/fake_probe.py{', hand clears depth' if a.hand_clears_depth else ''})
draws 5120 | UI draws 310 (0 before any terrain draw) | terrain draws 2900 | draws with an unknown pipeline 0 | terrain draws without root tables 0
UI shader list loaded: yes | shader names: ready | atlas picture: {'not saved' if a.no_atlas_picture else 'saved (single-player test)'}

## World -> UI boundary
first UI draw after the world: UIText.material.bin Fragment  ps dd754c1f0d27d99c
  UI target: {w}x{h} BGRA8 mips 1 array 1  0000FAKE

## Stage snapshots (colour + depth before the first draw of a material family that follows the terrain)
stage1_before_Actor: first draw of Actor.material.bin Fragment  ps 71f7073b9d471dae after the terrain began
stage2_before_ItemInHand: first draw of ItemInHandColor.material.bin Opaque Fragment  ps 4d2dcb73efb8abd3 after the terrain began

## Frame timeline (7 runs; a run = consecutive draws on one command list with the same shader, state and targets)
#1 list 0000A001 t100  RT1 DS1  x2 (4 tris)  Sky.material.bin Opaque Fragment  ps 1111111111111111  | rt RGBA8 | ds D24S8 off | blend off | cull
#2 list 0000A001 t100  RT1 DS1  x2400 (900000 tris)  RenderChunk.material.bin Vertex + REPLACED pixel shader  ps 3eca37248f0cf8cd  | rt RGBA8 | ds D24S8 less_eq write | blend off | cull
#3 list 0000A001 t100  RT1 DS1  x40 (9000 tris)  Actor.material.bin Fragment  ps 71f7073b9d471dae  | rt RGBA8 | ds D24S8 less write | blend off | cull none
#4 list 0000A001 t100  RT1 DS1  x500 (40000 tris)  RenderChunk.material.bin Opaque Fragment  ps b4998623a8fb7998  | rt RGBA8 | ds D24S8 less_eq nowrite | blend on | cull
#5 list 0000A001 t100  RT1 DS1  x1 (444 tris)  ItemInHandColor.material.bin Opaque Fragment  ps 4d2dcb73efb8abd3  | rt RGBA8 | ds D24S8 less write | blend off | cull
#6 list 0000A002 t101  RT2 DS-  x1 (1 tris)  Blit.material.bin Fragment  ps 2222222222222222  | rt BGRA8 | ds - off | blend off | cull none
#7 list 0000A002 t101  RT2 DS-  x310 (620 tris)  UIText.material.bin Fragment  ps dd754c1f0d27d99c  | rt BGRA8 | ds - off | blend on | cull none
targets:
  RT1 = view 0x0000000000001000  0000F0A0  {w}x{h} RGBA8 mips 1 array 1
  RT2 = view 0x0000000000002000  0000F0B0  {w}x{h} BGRA8 mips 1 array 1
  DS1 = view 0x0000000000003000  0000F0C0  {w}x{h} D24S8 mips 1 array 1

## Terrain (RenderChunk) pipelines this frame: 2
pso 0000F001  draws 2400  prims 900000
    ps 3eca37248f0cf8cd  REPLACED by a pack (not one of the game's shaders)
    vs 7d32a84a551fadfe  RenderChunk.material.bin Vertex Direct3D_SM65
    vs out: {vs_out}
    in: POSITION0:RGBA16F COLOR0:RGBA8 TEXCOORD0:fmt35 TEXCOORD1:fmt35
    rt RGBA8 | ds D24S8 less_eq write | blend off | cull  root 0000ROOT
pso 0000F002  draws 500  prims 40000
    ps b4998623a8fb7998  RenderChunk.material.bin Opaque Fragment Direct3D_SM65
    vs bf04858d24cd90c0  RenderChunk.material.bin Opaque Vertex Direct3D_SM65
    vs out: {vs_out}
    in: POSITION0:RGBA16F COLOR0:RGBA8 TEXCOORD0:fmt35 TEXCOORD1:fmt35
    rt RGBA8 | ds D24S8 less_eq nowrite | blend on | cull  root 0000ROOT

## Every material this frame (material file -> draws / prims / pipelines)
""")
    print(f"fake probe written to {out} ({w}x{h}, atlas {atlas.shape[1]}x{atlas.shape[0]}, {len(hits)} stamps"
          f"{', hand clears depth' if a.hand_clears_depth else ''}{', no atlas picture' if a.no_atlas_picture else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
