#!/usr/bin/env python3
"""Self-test for the World FX tag stamps (no game needed).

1. every texture in packs/WorldFxTagTest carries its own id (top-left soft + bottom-right hard stamp)
2. a fake terrain atlas (random placement, 1-texel edge padding like mip-friendly atlases) -> every id is found with
   the right rectangle, nothing else is found
3. what happens if the game premultiplies alpha into the atlas (soft stamps on alpha < 255 die, hard ones survive)
4. BGRA byte order
5. optional: --cpp <Renderer Foundation src/WorldFx.cpp> compiles the C++ findStamps standalone and checks it finds
   exactly the same stamps as the Python port

    python3 -I tools/WorldFx/test_stamps.py [--cpp ../rf/src/WorldFx.cpp]
"""
from __future__ import annotations

import argparse
import json
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import wfx_stamps  # noqa: E402

ROOT = HERE.parent.parent
PACK = ROOT / "packs" / "WorldFxTagTest" / "textures" / "blocks"


def load():
    meta = json.loads((HERE / "tag_ids.json").read_text())["ids"]
    ids = {int(k): v for k, v in meta.items()}
    tex = {i: np.array(Image.open(PACK / f"{v['name']}.png").convert("RGBA")) for i, v in ids.items()}
    return ids, tex


def fake_atlas(tex: dict[int, np.ndarray], size=1024, seed=7):
    """Place each texture on a 18-texel grid (16 + 1 padding each side, edges duplicated) in random order."""
    rng = random.Random(seed)
    atlas = np.zeros((size, size, 4), np.uint8)
    atlas[..., :3] = rng.randrange(256)  # junk background
    cells = [(x, y) for y in range(0, size - 18, 18) for x in range(0, size - 18, 18)]
    rng.shuffle(cells)
    where = {}
    for (i, t), (cx, cy) in zip(tex.items(), cells):
        h, w = t.shape[:2]
        padded = np.pad(t, ((1, 1), (1, 1), (0, 0)), mode="edge")
        atlas[cy:cy + h + 2, cx:cx + w + 2] = padded
        where[i] = (cx + 1, cy + 1, w, h)
    # sprinkle random noise blocks too (other textures)
    for _ in range(300):
        x, y = rng.randrange(size - 16), rng.randrange(size - 16)
        if any(abs(x - wx) < 20 and abs(y - wy) < 20 for wx, wy, _, _ in where.values()):
            continue
        atlas[y:y + 16, x:x + 16] = np.random.default_rng(rng.randrange(1 << 30)).integers(0, 256, (16, 16, 4), np.uint8)
    return atlas, where


def cpp_hits(cpp_src: Path, atlas: np.ndarray, bgra: bool) -> list[tuple]:
    src = cpp_src.read_text(encoding="utf-8")
    consts = re.search(r"constexpr int kTlA.*?\n.*?stampCheck\(int id\).*?\n", src, re.S)
    hit = re.search(r"struct Hit \{.*?\};\n", src, re.S)
    fn = re.search(r"std::vector<Hit> findStamps\(.*?\n\}\n", src, re.S)
    if not (consts and hit and fn):
        raise SystemExit("could not extract findStamps from " + str(cpp_src))
    h, w = atlas.shape[:2]
    prog = f"""#include <cstdint>
#include <cstdio>
#include <vector>
typedef unsigned long long UINT64;
{consts.group(0)}
{hit.group(0)}
{fn.group(0)}
int main() {{
    std::vector<uint8_t> d({w} * {h} * 4);
    if (std::fread(d.data(), 1, d.size(), stdin) != d.size()) return 2;
    for (auto const& h : findStamps(d.data(), {w} * 4, {w}, {h}, {'true' if bgra else 'false'}, 0))
        std::printf("%d %d %d %d %d %d %d %d\\n", h.id, h.x, h.y, h.br ? 1 : 0, h.a[0], h.a[1], h.a[2], h.a[3]);
    return 0;
}}
"""
    with tempfile.TemporaryDirectory() as td:
        c, exe = Path(td) / "t.cpp", Path(td) / "t"
        c.write_text(prog)
        subprocess.run(["g++", "-std=c++20", "-O2", "-o", str(exe), str(c)], check=True)
        out = subprocess.run([str(exe)], input=atlas.tobytes(), capture_output=True, check=True).stdout.decode()
    return [tuple(int(v) for v in ln.split()) for ln in out.splitlines() if ln.strip()]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpp", type=Path, help="Renderer Foundation src/WorldFx.cpp (parity check)")
    a = ap.parse_args()
    ids, tex = load()
    fails = 0

    # 1. each texture alone
    for i, t in tex.items():
        hits = wfx_stamps.find(t)
        got = sorted((h["id"], h["corner"]) for h in hits)
        if got != [(i, "br"), (i, "tl")]:
            print(f"FAIL texture {ids[i]['name']}: {got}")
            fails += 1
    print(f"1. {len(tex)} textures carry their own id: {'ok' if not fails else 'FAILED'}")

    # 1b. alpha tags (v0.2): every texel carries the tag except the 4 texels of the hard stamp (forced to 255)
    bad_tags = 0
    n_tags = 0
    for i, t in tex.items():
        tag = ids[i].get("alpha_tag")
        if tag is None:
            continue
        n_tags += 1
        vals, cnt = np.unique(t[..., 3], return_counts=True)
        got = dict(zip(vals.tolist(), cnt.tolist()))
        if got != {tag: t.shape[0] * t.shape[1] - 4, 255: 4}:
            print(f"FAIL alpha tag {ids[i]['name']}: {got}")
            bad_tags += 1
        soft = [h for h in wfx_stamps.find(t) if h["corner"] == "tl"]
        if not soft or soft[0]["alpha"] != [tag] * 4:
            print(f"FAIL alpha tag not reported by the soft stamp of {ids[i]['name']}: {soft}")
            bad_tags += 1
    print(f"1b. {n_tags} alpha-tagged textures carry their tag (and the soft stamp reports it): {'ok' if not bad_tags else 'FAILED'}")
    fails += bad_tags

    # 2. fake atlas
    atlas, where = fake_atlas(tex)
    hits = wfx_stamps.find(atlas)
    r = wfx_stamps.rects(hits, ids)
    bad = 0
    for i, (x, y, w, h) in where.items():
        rr = r.get(i, {}).get("rects", [])
        if {"x": x, "y": y, "w": w, "h": h, "slice": 0} not in rr:
            print(f"FAIL atlas id {i} {ids[i]['name']} expected {(x, y, w, h)} got {rr}")
            bad += 1
    stray = [k for k in r if k not in where]
    print(f"2. fake atlas: {len(where)} textures placed, {len(hits)} stamp hits, "
          f"{len(where) - bad} rects exact, stray ids {stray}: {'ok' if not bad and not stray else 'FAILED'}")
    fails += bad + len(stray)

    # 3. premultiplied atlas
    pm = atlas.copy()
    pm[..., :3] = (atlas[..., :3].astype(np.uint32) * atlas[..., 3:4] // 255).astype(np.uint8)
    r_pm = wfx_stamps.rects(wfx_stamps.find(pm), ids)
    soft_ok = sum(1 for i in where if r_pm.get(i, {}).get("tl"))
    hard_ok = sum(1 for i in where if r_pm.get(i, {}).get("br"))
    translucent = [i for i in where if ids[i]["min_alpha"] < 255]
    print(f"3. if the game premultiplied alpha: soft stamps left {soft_ok}/{len(where)}, hard {hard_ok}/{len(where)} "
          f"({len(translucent)} textures have alpha < 255 somewhere: stained glass, alpha-tagged concrete) - informative")

    # 4. BGRA
    bg = atlas[..., [2, 1, 0, 3]].copy()
    n_bg = len(wfx_stamps.find(bg, bgra=True))
    print(f"4. BGRA order: {n_bg} hits (RGBA had {len(hits)}): {'ok' if n_bg == len(hits) else 'FAILED'}")
    fails += n_bg != len(hits)

    # 5. C++ parity
    if a.cpp:
        py = sorted((h["id"], h["x"], h["y"], 1 if h["corner"] == "br" else 0, *h["alpha"]) for h in hits)
        cc = sorted(cpp_hits(a.cpp, atlas, False))
        cc_bg = sorted(cpp_hits(a.cpp, bg, True))
        same = py == cc == cc_bg
        print(f"5. C++ findStamps (compiled from {a.cpp.name}): {len(cc)} hits RGBA, {len(cc_bg)} BGRA, "
              f"identical to Python: {'ok' if same else 'FAILED'}")
        fails += not same

    print("ALL OK" if not fails else f"{fails} FAILURE(S)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
