#!/usr/bin/env python3
"""Turn a Renderer Foundation World FX probe folder into answers + previews.

    python3 -I tools/WorldFx/analyze_probe.py <mods/RendererFoundation/logs/worldfx/<time>> [--vanilla <blocks dir>]

Reads report.txt, *.raw + *.json and stamps.json from the probe (src/WorldFx.cpp) and writes <probe>/analysis/:
  colour.png / world_colour.png / depth.png      the world right before the HUD, and its depth buffer
  atlasN.png + atlasN_tags.png                   the terrain atlas, tagged textures outlined
  preview_outlines.png / preview_cel.png         what M1 cel outlines would look like on THIS frame
  tags.json                                      atlas rectangles per tagged texture (what M2 feeds the shader)
  verdict.md                                     M1 / M2 go / no-go with the reasons
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import wfx_stamps  # noqa: E402


# ---------------------------------------------------------------- raw decoding
def half(u16: np.ndarray) -> np.ndarray:
    return u16.view(np.float16).astype(np.float32)


def small_float(bits: np.ndarray, mbits: int) -> np.ndarray:
    m = (bits & ((1 << mbits) - 1)).astype(np.float32)
    e = ((bits >> mbits) & 31).astype(np.int32)
    v = np.where(e == 0, np.ldexp(m, -14 - mbits), np.ldexp(m + (1 << mbits), e - 15 - mbits))
    return np.where(e == 31, 65000.0, v).astype(np.float32)


def load_raw(folder: Path, name: str):
    hdr = json.loads((folder / f"{name}.json").read_text())
    data = (folder / f"{name}.raw").read_bytes()
    w, h, rb = hdr["width"], hdr["height"], hdr["rowBytes"]
    rows = np.frombuffer(data, np.uint8)[: rb * h].reshape(h, rb)
    lay = hdr["layout"]
    if lay in ("rgba8", "bgra8"):
        px = rows[:, : w * 4].reshape(h, w, 4).copy()
        if lay == "bgra8":
            px = px[..., [2, 1, 0, 3]]
        return hdr, px  # uint8 RGBA
    if lay == "rgb10a2":
        v = rows[:, : w * 4].copy().view(np.uint32).reshape(h, w)
        rgb = np.stack([(v & 1023), (v >> 10) & 1023, (v >> 20) & 1023], -1).astype(np.float32) / 1023.0
        return hdr, rgb
    if lay == "rgba16f":
        v = rows[:, : w * 8].copy().view(np.uint16).reshape(h, w, 4)
        return hdr, half(v)[..., :3]
    if lay == "r11g11b10f":
        v = rows[:, : w * 4].copy().view(np.uint32).reshape(h, w)
        return hdr, np.stack([small_float(v & 2047, 6), small_float((v >> 11) & 2047, 6), small_float((v >> 22) & 1023, 5)], -1)
    if lay == "depth24":
        v = rows[:, : w * 4].copy().view(np.uint32).reshape(h, w)
        return hdr, (v & 0xFFFFFF).astype(np.float64) / 16777215.0
    if lay == "depth32f":
        return hdr, rows[:, : w * 4].copy().view(np.float32).reshape(h, w).astype(np.float64)
    return hdr, None


def to_rgb8(img: np.ndarray, layout: str) -> np.ndarray:
    if img.dtype == np.uint8:
        return img[..., :3]
    x = np.clip(img.astype(np.float32), 0, None)
    if layout in ("rgba16f", "r11g11b10f"):  # scene-linear HDR (Vibrant Visuals): Reinhard + display gamma for viewing
        x = (x / (1 + x)) ** (1 / 2.2)
    return (np.clip(x, 0, 1) * 255 + 0.5).astype(np.uint8)


# ---------------------------------------------------------------- depth -> outlines
def linear_depth(d: np.ndarray, reversed_z: bool) -> np.ndarray:
    """Distance up to a constant factor. Standard Z: d = f/(f-n) * (1 - n/z) -> z ~ 1 / (1 - d) for f >> n."""
    if reversed_z:
        return 1.0 / np.maximum(d, 1e-7)
    return 1.0 / np.maximum(1.0 - d, 1e-7)


def outline_mask(d: np.ndarray, sky: np.ndarray, reversed_z: bool, strength=1.0) -> np.ndarray:
    z = linear_depth(d, reversed_z)
    zp = np.pad(z, 1, mode="edge")
    c = zp[1:-1, 1:-1]
    l, r, u, dn = zp[1:-1, :-2], zp[1:-1, 2:], zp[:-2, 1:-1], zp[2:, 1:-1]
    # silhouettes: a neighbour clearly farther than this pixel (relative, so far edges stay as thin as near ones)
    jump = np.maximum.reduce([l, r, u, dn]) / c - 1.0
    sil = jump > 0.06 / strength
    # creases (block edges on one surface): second derivative of depth, relative
    lap = np.abs(l + r - 2 * c) + np.abs(u + dn - 2 * c)
    crease = (lap / c) > 0.012 / strength
    m = (sil | crease) & ~sky
    # a sky neighbour of solid ground also counts (horizon silhouette)
    skp = np.pad(sky, 1, mode="edge")
    m |= ~sky & (skp[1:-1, :-2] | skp[1:-1, 2:] | skp[:-2, 1:-1] | skp[2:, 1:-1])
    return m


def cel_shade(rgb8: np.ndarray, solid: np.ndarray, bands=4) -> np.ndarray:
    """Cel bands + saturation on solid pixels only (the sky keeps its smooth gradient)."""
    x = rgb8.astype(np.float32) / 255.0
    lum = x @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    q = (np.floor(lum * bands) + 0.5) / bands
    x = x * (q / np.maximum(lum, 1e-4))[..., None]
    mean = x.mean(-1, keepdims=True)
    x = mean + (x - mean) * 1.25  # +25% saturation, anime look
    out = rgb8.copy()
    out[solid] = (np.clip(x, 0, 1) * 255).astype(np.uint8)[solid]
    return out


# ---------------------------------------------------------------- vanilla texture search (no tag pack needed)
def find_vanilla(atlas: np.ndarray, tex: np.ndarray, max_hits=8) -> list[tuple[int, int]]:
    """Positions where the 16x16 vanilla texture sits in the atlas (RGB compared with the low 3 bits masked off)."""
    h, w = tex.shape[:2]
    A = atlas & 0xF8
    T = tex & 0xF8
    H, W = A.shape[:2]
    if H < h or W < w:
        return []
    # candidates from the first 4 texels of row 0
    m = np.ones((H - h + 1, W - w + 1), bool)
    for k in range(min(4, w)):
        m &= np.all(A[: H - h + 1, k: k + W - w + 1] == T[0, k], axis=-1)
    visible = tex[..., 3] > 0
    scored = []
    for y, x in zip(*np.nonzero(m)):
        blk = A[y: y + h, x: x + w]
        score = float(np.mean(np.all(blk[..., :3] == T[..., :3], -1)[visible]))
        if score > 0.97:
            scored.append((score, int(x), int(y)))
    # flat textures also match 1 texel off (atlas edge padding): keep the best per neighbourhood
    hits: list[tuple[int, int]] = []
    for score, x, y in sorted(scored, reverse=True):
        if all(abs(x - hx) > 2 or abs(y - hy) > 2 for hx, hy in hits):
            hits.append((x, y))
            if len(hits) >= max_hits:
                break
    return hits


# ---------------------------------------------------------------- report parsing
def terrain_pipelines(report: str) -> list[dict]:
    sec = report.split("## Terrain (RenderChunk) pipelines", 1)
    if len(sec) < 2:
        return []
    body = sec[1].split("\n## ", 1)[0]
    out = []
    for m in re.finditer(r"pso (\S+)\s+draws (\d+)\s+prims (\d+)\n\s+ps (\S+)\s+(.*)\n\s+vs (\S+)\s+(.*)\n\s+vs out: (.*)\n\s+in: (.*)\n\s+(.*)\n", body):
        out.append({"pso": m[1], "draws": int(m[2]), "prims": int(m[3]), "ps": m[4], "ps_name": m[5].strip(), "vs_name": m[7].strip(),
                    "vs_out": m[8].strip(), "inputs": m[9].strip(), "targets": m[10].strip()})
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("probe", type=Path)
    ap.add_argument("--vanilla", type=Path, help="bedrock-samples resource_pack/textures/blocks (else ~/.cache, fetched)")
    ap.add_argument("--ids", type=Path, default=HERE / "tag_ids.json")
    a = ap.parse_args()
    P = a.probe
    out = P / "analysis"
    out.mkdir(exist_ok=True)
    report = (P / "report.txt").read_text(errors="replace") if (P / "report.txt").exists() else ""
    ids = {int(k): v for k, v in json.loads(a.ids.read_text())["ids"].items()}
    v: list[str] = ["# World FX probe analysis: " + P.name, ""]

    # ---- frame + boundary
    first = report.splitlines()[1] if report.count("\n") > 1 else "(no report)"
    v += ["Frame: " + first, ""]
    colour = world = depth = None
    for name in ("colour_at_ui", "world_colour", "world_depth"):
        if (P / f"{name}.json").exists():
            hdr, img = load_raw(P, name)
            if img is None:
                v.append(f"- {name}: layout {hdr['layout']} not decoded")
                continue
            if name == "world_depth":
                depth = img
            elif name == "colour_at_ui":
                colour = to_rgb8(img, hdr["layout"])
            else:
                world = to_rgb8(img, hdr["layout"])
    if colour is not None:
        Image.fromarray(colour).save(out / "colour.png")
    if world is not None:
        Image.fromarray(world).save(out / "world_colour.png")

    # ---- M1: depth -> outlines
    v += ["## M1 cel outlines (needs the world's depth right before the HUD)"]
    m1 = False
    if depth is not None:
        far1 = float(np.mean(depth >= 0.999999))
        near0 = float(np.mean(depth <= 1e-6))
        reversed_z = near0 > far1  # sky at 0 = reversed Z
        sky = depth <= 1e-6 if reversed_z else depth >= 0.999999
        solid = ~sky
        uniq = len(np.unique(depth[solid])) if solid.any() else 0
        dv = np.zeros(depth.shape, np.uint8)
        if solid.any():
            z = linear_depth(depth, reversed_z)
            lz = np.log(z[solid])
            lo, hi = np.percentile(lz, 1), np.percentile(lz, 99)
            dv[solid] = (255 - 235 * np.clip((np.log(z[solid]) - lo) / max(hi - lo, 1e-9), 0, 1)).astype(np.uint8)
        Image.fromarray(dv).save(out / "depth.png")
        v.append(f"- depth {depth.shape[1]}x{depth.shape[0]}, {'REVERSED' if reversed_z else 'standard'} Z, sky {100 * sky.mean():.1f}%, "
                 f"{uniq} distinct depth values on solid pixels")
        m1 = uniq > 1000
        if colour is not None:
            if depth.shape != colour.shape[:2]:  # render scale: bring depth to the colour size
                im = Image.fromarray(depth.astype(np.float32), "F").resize((colour.shape[1], colour.shape[0]), Image.NEAREST)
                d2 = np.array(im, np.float64)
                v.append(f"- depth is {depth.shape[1]}x{depth.shape[0]} but colour is {colour.shape[1]}x{colour.shape[0]} (render scale): resized")
            else:
                d2 = depth
            sky2 = d2 <= 1e-6 if reversed_z else d2 >= 0.999999
            mask = outline_mask(d2, sky2, reversed_z)
            ol = colour.copy()
            ol[mask] = (ol[mask] * 0.08).astype(np.uint8)
            Image.fromarray(ol).save(out / "preview_outlines.png")
            cel = cel_shade(colour, ~sky2)
            cel[mask] = (12, 8, 20)
            Image.fromarray(cel).save(out / "preview_cel.png")
            v.append(f"- previews: preview_outlines.png (ink outlines on this frame), preview_cel.png (+ cel bands); "
                     f"{100 * mask.mean():.1f}% of the screen is outline")
        v.append(f"- **M1: {'GO' if m1 else 'NO-GO'}** " + ("- depth is there and valid" if m1 else "- depth looks empty / constant"))
    else:
        v.append("- no world_depth copy -> **M1: NO-GO yet** (see the report's boundary section: DSV unknown, or no UI draw after the world)")
    v.append("")

    # ---- terrain pipelines
    pipes = terrain_pipelines(report)
    v += ["## Terrain pipelines (what M2 replaces)"]
    if pipes:
        for p in pipes[:12]:
            v.append(f"- {p['ps_name']} | draws {p['draws']} | vs out `{p['vs_out']}` | {p['targets']}")
    else:
        v.append("- none parsed (shader names not ready? pipelines made before the hooks?)")
    uv = [p for p in pipes if "TEXCOORD0" in p["vs_out"]]
    passes = sorted({w for p in pipes for w in ("Opaque", "AlphaTest", "Transparent") if w in p["ps_name"]})
    v += [f"- passes seen: {', '.join(passes) or '-'}; pipelines handing the atlas UV (TEXCOORD0) to the pixel shader: {len(uv)}/{len(pipes)}", ""]

    # ---- atlas + tags
    v += ["## Terrain atlas + tag test pack"]
    stamps = json.loads((P / "stamps.json").read_text()) if (P / "stamps.json").exists() else []
    tags: dict = {}
    van_dir = a.vanilla
    if van_dir is None:
        try:
            import make_tag_pack
            van_dir = make_tag_pack.fetch_vanilla()
        except Exception as e:  # offline: skip the vanilla search
            v.append(f"- vanilla textures unavailable ({e}); vanilla search skipped")
    for hj in sorted(P.glob("atlas*.json")):
        name = hj.stem
        hdr, img = load_raw(P, name)
        if img is None or img.dtype != np.uint8:
            v.append(f"- {name}: {hdr['resourceFormat']} {hdr['width']}x{hdr['height']} - not RGBA8, can't search")
            continue
        Image.fromarray(img, "RGBA").save(out / f"{name}.png")
        hits = [s for s in stamps if s["copy"] == name]
        r = wfx_stamps.rects(hits, ids)
        tagged = Image.fromarray(img, "RGBA").convert("RGB")
        dr = ImageDraw.Draw(tagged)
        found = 0
        for tid, e in sorted(r.items()):
            rr = e.get("rects") or e.get("rects_from_size") or []
            if rr:
                found += 1
                tags.setdefault(e["name"], {"id": tid, "group": ids.get(tid, {}).get("group"), "atlas": name, "rects": rr,
                                            "soft_kept": bool(e["tl"]), "hard_kept": bool(e["br"])})
                for q in rr:
                    dr.rectangle([q["x"] - 1, q["y"] - 1, q["x"] + q["w"], q["y"] + q["h"]], outline=(255, 0, 255))
        # vanilla glass search (works without the tag pack, e.g. if Hive overrides it)
        van_found = {}
        if van_dir and Path(van_dir).exists():
            for nm in ("glass", "glass_red", "glass_white", "glass_pane_top", "tinted_glass", "wool_colored_red", "concrete_red"):
                f = Path(van_dir) / f"{nm}.png"
                if f.exists():
                    pos = find_vanilla(img, np.array(Image.open(f).convert("RGBA")))
                    if pos:
                        van_found[nm] = pos
                        for (x, y) in pos:
                            dr.rectangle([x - 1, y - 1, x + 16, y + 16], outline=(0, 255, 255))
        tagged.save(out / f"{name}_tags.png")
        soft_lost = [n for n, t in tags.items() if t["atlas"] == name and not t["soft_kept"]]
        v.append(f"- {name}: {hdr['resourceFormat']} {hdr['width']}x{hdr['height']} mips {hdr['mips']} array {hdr['array']} | "
                 f"tagged textures found {found}/{len(ids)} | vanilla matches: {', '.join(f'{k} x{len(p)}' for k, p in van_found.items()) or 'none'}")
        if found and soft_lost:
            v.append(f"  - soft stamps lost on {len(soft_lost)} textures (game changes RGB under alpha < 255): {', '.join(soft_lost[:8])}")
    (out / "tags.json").write_text(json.dumps(tags, indent=1) + "\n")
    groups = {}
    for n, t in tags.items():
        groups.setdefault(t["group"], []).append(n)
    want = {}
    for i in ids.values():
        want.setdefault(i["group"], []).append(i["name"])
    for g, names in want.items():
        got = groups.get(g, [])
        miss = [n for n in names if n not in got]
        v.append(f"- group {g}: {len(got)}/{len(names)} located" + (f" (missing: {', '.join(miss[:6])}{'...' if len(miss) > 6 else ''})" if miss else ""))
    glass_known = any(t["group"] in ("glass", "glass_pane") for t in tags.values())
    m2 = bool(uv) and glass_known
    v += ["", f"**M2 refraction glass: {'GO' if m2 else 'NO-GO yet'}** - "
          + ("terrain pixel shaders get the atlas UV and the glass rectangles are known (tags.json)" if m2 else
             ("no glass located in the atlas (tag pack not active / overridden?)" if uv else "no terrain pipeline with TEXCOORD0 parsed")), ""]
    (out / "verdict.md").write_text("\n".join(v) + "\n")
    print("\n".join(v))
    print(f"\nwritten: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
