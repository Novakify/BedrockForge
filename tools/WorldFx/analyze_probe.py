#!/usr/bin/env python3
"""Turn a Renderer Foundation World FX probe folder into answers + previews.

    python3 -I tools/WorldFx/analyze_probe.py <mods/RendererFoundation/logs/worldfx/<time>> [--vanilla <blocks dir>]

Reads report.txt, *.raw + *.json and stamps.json from the probe (RF src/WorldFx.cpp, v1 or v2) and writes
<probe>/analysis/:
  <snapshot>.png                                 every colour / depth snapshot the probe took (stages + HUD boundary)
  <snapshot>_alpha.png                           the colour target's alpha channel (is it free for a per-pixel ID?)
  atlasN.png + atlasN_tags.png                   the terrain atlas, tagged textures outlined (only if the probe saved it)
  preview_outlines.png / preview_cel.png         what cel outlines would look like on THIS frame
  preview_ao.png                                 soft corner shadows (ambient occlusion) from the same depth
  tags.json                                      atlas rectangles per tagged texture
  verdict.md                                     go / no-go per milestone with the reasons, frame order, hook point
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

ROOT = HERE.parent.parent
PACK_TEX = ROOT / "packs" / "WorldFxTagTest" / "textures" / "blocks"


# ---------------------------------------------------------------- raw decoding
def half(u16: np.ndarray) -> np.ndarray:
    return u16.view(np.float16).astype(np.float32)


def small_float(bits: np.ndarray, mbits: int) -> np.ndarray:
    m = (bits & ((1 << mbits) - 1)).astype(np.float32)
    e = ((bits >> mbits) & 31).astype(np.int32)
    v = np.where(e == 0, np.ldexp(m, -14 - mbits), np.ldexp(m + (1 << mbits), e - 15 - mbits))
    return np.where(e == 31, 65000.0, v).astype(np.float32)


def load_raw(folder: Path, name: str):
    """-> (header, array or None). Colour: uint8 RGBA (8-bit targets) or float RGB; depth: float64 (h, w)."""
    hdr = json.loads((folder / f"{name}.json").read_text())
    raw = folder / f"{name}.raw"
    if not hdr.get("saved", True) or not raw.exists():
        return hdr, None
    data = raw.read_bytes()
    w, h, rb = hdr["width"], hdr["height"], hdr["rowBytes"]
    rows = np.frombuffer(data, np.uint8)[: rb * h].reshape(h, rb)
    lay = hdr["layout"]
    if lay in ("rgba8", "bgra8"):
        px = rows[:, : w * 4].reshape(h, w, 4).copy()
        if lay == "bgra8":
            px = px[..., [2, 1, 0, 3]]
        return hdr, px
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


# ---------------------------------------------------------------- depth
def depth_info(d: np.ndarray) -> dict:
    far1 = float(np.mean(d >= 0.999999))
    near0 = float(np.mean(d <= 1e-6))
    reversed_z = near0 > far1  # sky at 0 = reversed Z
    sky = d <= 1e-6 if reversed_z else d >= 0.999999
    solid = ~sky
    uniq = int(len(np.unique(d[solid]))) if solid.any() else 0
    return {"reversed": reversed_z, "sky": sky, "solid": float(solid.mean()), "uniq": uniq}


def linear_depth(d: np.ndarray, reversed_z: bool) -> np.ndarray:
    """Distance up to a constant factor. Standard Z: d = f/(f-n) * (1 - n/z) -> z ~ 1 / (1 - d) for f >> n."""
    if reversed_z:
        return 1.0 / np.maximum(d, 1e-7)
    return 1.0 / np.maximum(1.0 - d, 1e-7)


def keeps_world(earlier: np.ndarray, e_info: dict, later: np.ndarray) -> float:
    """Share of the earlier snapshot's solid pixels that the later one still covers at the same depth or nearer.
    ~1.0 = the later snapshot still holds that world (plus whatever was drawn in front since); low = it was cleared."""
    if later.shape != earlier.shape:
        later = np.array(Image.fromarray(later.astype(np.float32), "F").resize((earlier.shape[1], earlier.shape[0]), Image.NEAREST), np.float64)
    solid = ~e_info["sky"]
    if not solid.any():
        return 0.0
    eps = 2e-6
    ok = later[solid] >= earlier[solid] - eps if e_info["reversed"] else later[solid] <= earlier[solid] + eps
    return float(np.mean(ok))


def depth_png(d: np.ndarray, info: dict) -> np.ndarray:
    out = np.zeros(d.shape, np.uint8)
    solid = ~info["sky"]
    if solid.any():
        lz = np.log(linear_depth(d, info["reversed"])[solid])
        lo, hi = np.percentile(lz, 1), np.percentile(lz, 99)
        out[solid] = (255 - 235 * np.clip((lz - lo) / max(hi - lo, 1e-9), 0, 1)).astype(np.uint8)
    return out


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


def ambient_occlusion(d: np.ndarray, sky: np.ndarray, reversed_z: bool, radius_px=10) -> np.ndarray:
    """Screen-space AO preview: soft shadow where the surface bends toward the camera (inner corners, block steps).
    Works on u ~ near/z, which is affine across a flat surface on screen, so tilted flat faces get no false shadow:
    for opposite samples a, b around a pixel, (a + b) / 2 == u on a plane and > u in a concave corner. -> light 0..1."""
    u = np.maximum(d if reversed_z else 1.0 - d, 1e-7)
    occ = np.zeros_like(u)
    n = 0
    for ang in np.linspace(0, np.pi, 6, endpoint=False):
        for rr in (radius_px * 0.5, radius_px):
            dx, dy = int(round(np.cos(ang) * rr)), int(round(np.sin(ang) * rr))
            if dx == 0 and dy == 0:
                continue
            a = np.roll(np.roll(u, dy, axis=0), dx, axis=1)
            b = np.roll(np.roll(u, -dy, axis=0), -dx, axis=1)
            rel_a, rel_b = a / u - 1.0, b / u - 1.0
            valid = (np.abs(rel_a) < 0.3) & (np.abs(rel_b) < 0.3)  # other objects far in front / behind: no halo
            occ += np.clip((rel_a + rel_b) * 0.5 * 40.0, 0, 1) * valid
            n += 1
    light = 1.0 - 0.6 * np.clip(occ / max(n, 1) * 2.5, 0, 1)
    light[sky] = 1.0
    return light


def box_blur(x: np.ndarray, r: int) -> np.ndarray:
    k = 2 * r + 1
    c = np.cumsum(np.cumsum(np.pad(x, r, mode="edge"), axis=0), axis=1)
    c = np.pad(c, ((1, 0), (1, 0)))
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def cel_shade(rgb8: np.ndarray, solid: np.ndarray, bands=4) -> np.ndarray:
    """Cel preview on solid pixels only (the sky keeps its gradient): band the SMOOTHED brightness (the lighting), keep
    the texture detail, +25% saturation. Only an approximation for previews - the real cel look bands the light inside
    the block shader (shaders/src/Anime), where light and texture are still separate."""
    x = rgb8.astype(np.float32) / 255.0
    lum = x @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    lb = box_blur(lum, max(3, rgb8.shape[0] // 90))
    f = lb * bands
    q = (np.floor(f) + np.clip((f - np.floor(f) - 0.8) / 0.2, 0, 1)) / bands + 0.5 / bands
    x = x * (q / np.maximum(lb, 1e-4))[..., None]
    mean = x.mean(-1, keepdims=True)
    x = mean + (x - mean) * 1.25
    out = rgb8.copy()
    out[solid] = (np.clip(x, 0, 1) * 255).astype(np.uint8)[solid]
    return out


# ---------------------------------------------------------------- atlas helpers
def find_vanilla(atlas: np.ndarray, tex: np.ndarray, max_hits=8) -> list[tuple[int, int]]:
    """Positions where the 16x16 vanilla texture sits in the atlas (RGB compared with the low 3 bits masked off)."""
    h, w = tex.shape[:2]
    A = atlas & 0xF8
    T = tex & 0xF8
    H, W = A.shape[:2]
    if H < h or W < w:
        return []
    m = np.ones((H - h + 1, W - w + 1), bool)
    for k in range(min(4, w)):  # candidates from the first 4 texels of row 0
        m &= np.all(A[: H - h + 1, k: k + W - w + 1, :3] == T[0, k, :3], axis=-1)
    visible = tex[..., 3] > 0
    scored = []
    for y, x in zip(*np.nonzero(m)):
        blk = A[y: y + h, x: x + w]
        score = float(np.mean(np.all(blk[..., :3] == T[..., :3], -1)[visible]))
        if score > 0.97:
            scored.append((score, int(x), int(y)))
    hits: list[tuple[int, int]] = []  # flat textures also match 1 texel off (edge padding): best per neighbourhood
    for score, x, y in sorted(scored, reverse=True):
        if all(abs(x - hx) > 2 or abs(y - hy) > 2 for hx, hy in hits):
            hits.append((x, y))
            if len(hits) >= max_hits:
                break
    return hits


def compare_rect(atlas: np.ndarray, q: dict, tex: np.ndarray) -> dict:
    """How the game stored one of our textures: exact RGB? exact alpha? premultiplied?"""
    blk = atlas[q["y"]: q["y"] + q["h"], q["x"]: q["x"] + q["w"]].astype(int)
    if blk.shape != tex.shape:
        return {"ok": False}
    t = tex.astype(int)
    pm = t[..., :3] * t[..., 3:4] // 255
    return {"ok": True,
            "rgb_exact": float(np.mean(np.all(blk[..., :3] == t[..., :3], -1))),
            "alpha_exact": float(np.mean(blk[..., 3] == t[..., 3])),
            "premultiplied": float(np.mean(np.all(np.abs(blk[..., :3] - pm) <= 1, -1)))}


# ---------------------------------------------------------------- report parsing
def terrain_pipelines(report: str) -> list[dict]:
    sec = report.split("## Terrain (RenderChunk) pipelines", 1)
    if len(sec) < 2:
        return []
    body = sec[1].split("\n## ", 1)[0]
    out = []
    for m in re.finditer(r"pso (\S+)\s+draws (\d+)\s+prims (\d+)\n\s+ps (\S+)\s+(.*)\n\s+vs (\S+)\s+(.*)\n\s+vs out: (.*)\n\s+in: (.*)\n\s+(.*)\n", body):
        t = m[10].strip()
        kind = "transparent (blended)" if "blend on" in t else "cutout (alpha test)" if "cull none" in t else "opaque"
        out.append({"pso": m[1], "draws": int(m[2]), "prims": int(m[3]), "ps": m[4], "ps_name": m[5].strip(), "vs_name": m[7].strip(),
                    "vs_out": m[8].strip(), "inputs": m[9].strip(), "targets": t, "kind": kind,
                    "replaced": "REPLACED" in m[5]})
    return out


def frame_order(report: str) -> list[list]:
    """Compact frame order from the v2 timeline: [family, RT, DS, draws] merged while consecutive runs agree."""
    sec = report.split("## Frame timeline", 1)
    if len(sec) < 2:
        return []
    body = sec[1].split("\ntargets:", 1)[0]
    order: list[list] = []
    for m in re.finditer(r"^#(\d+) list \S+ t\d+\s+(RT\S+) (DS\S+)\s+x(\d+) \((\d+) tris\)\s+(.+)$", body, re.M):
        label = m[6]
        fam = label.split(".material.bin")[0] if ".material.bin" in label else label.split("  ")[0]
        if order and order[-1][:3] == [fam, m[2], m[3]]:
            order[-1][3] += int(m[4])
        else:
            order.append([fam, m[2], m[3], int(m[4])])
    return order


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
    first = report.splitlines()[1] if report.count("\n") > 1 else "(no report)"
    v += ["Frame: " + first, ""]

    # ---- frame order (v2 probes)
    order = frame_order(report)
    if order:
        steps = [f"{f} [{rt} {ds}] x{n}" for f, rt, ds, n in order[:70]]
        v += ["## Frame order (material families as recorded; RT/DS = colour / depth target ids from the report)",
              " -> ".join(steps) + (" -> ..." if len(order) > 70 else ""), ""]

    # ---- snapshots: stages in order, the HUD boundary last
    colours: list[tuple[str, dict, np.ndarray]] = []
    depths: list[tuple[str, dict, np.ndarray, dict]] = []
    names = sorted({p.stem for p in P.glob("stage*_*.json")},
                   key=lambda n: (int(re.match(r"stage(\d+)", n)[1]), n))
    names += [n for n in ("world_colour", "colour_at_ui", "world_depth") if (P / f"{n}.json").exists()]
    v += ["## Snapshots (in frame order; the last ones are right before the HUD)"]
    for name in names:
        hdr, img = load_raw(P, name)
        if img is None:
            v.append(f"- {name}: {hdr.get('resourceFormat')} {hdr['width']}x{hdr['height']} - not decoded")
            continue
        if hdr["layout"].startswith("depth"):
            info = depth_info(img)
            Image.fromarray(depth_png(img, info)).save(out / f"{name}.png")
            depths.append((name, hdr, img, info))
            v.append(f"- {name}: depth {img.shape[1]}x{img.shape[0]} {hdr['resourceFormat']}, {'REVERSED' if info['reversed'] else 'standard'} Z, "
                     f"solid {100 * info['solid']:.1f}% of the screen, {info['uniq']} distinct values")
        else:
            rgb = to_rgb8(img, hdr["layout"])
            Image.fromarray(rgb).save(out / f"{name}.png")
            colours.append((name, hdr, rgb))
            line = f"- {name}: colour {rgb.shape[1]}x{rgb.shape[0]} {hdr['resourceFormat']}"
            if img.dtype == np.uint8:  # what do the game's shaders leave in the alpha channel?
                vals, cnt = np.unique(img[..., 3], return_counts=True)
                top = sorted(zip(cnt.tolist(), vals.tolist()), reverse=True)[:4]
                line += " | alpha: " + ", ".join(f"{val} on {100 * c / img[..., 3].size:.1f}%" for c, val in top)
                Image.fromarray(img[..., 3]).save(out / f"{name}_alpha.png")
            v.append(line)
    v.append("")

    # ---- where is the world's depth complete?
    v += ["## Depth: where can a World FX pass read the finished world?"]
    best = None
    usable = [d for d in depths if d[3]["solid"] > 0.01 and d[3]["uniq"] > 200]
    if usable:
        # walk the snapshots in frame order: a later one is "complete" while it still holds the earlier world
        best = usable[0]
        lost = []
        for d in depths[depths.index(best) + 1:]:
            kept = keeps_world(best[2], best[3], d[2])
            if kept >= 0.9 and d[3]["uniq"] > 200:
                best = d
            else:
                lost.append(f"{d[0]} (keeps {100 * kept:.0f}% of it)")
        hud = next((d for d in depths if d[0] == "world_depth"), None)
        if hud is not None and hud is best:
            v.append("- the world's depth is intact right before the HUD -> the pass can run at the first UI draw")
        else:
            v.append(f"- last snapshot with the complete world depth: **{best[0]}**. Lost after it: {', '.join(lost) or 'nothing later was captured'}"
                     " -> depth is cleared or overwritten there (typically the hand pass): the pass has to read depth BEFORE that family draws")
        if len(usable) == 1 and len(depths) == 1:
            v.append("- only one depth snapshot: cannot tell whether it is the whole world or what a later clear left behind - check depth png by eye")
        if hud is not None:
            v.append(f"- depth at the HUD boundary: solid {100 * hud[3]['solid']:.1f}% of the screen; in {best[0]}: {100 * best[3]['solid']:.1f}%")
    elif depths:
        v.append("- no snapshot holds usable depth -> **NO-GO yet** for depth looks (outlines, AO, refraction)")
    else:
        v.append("- no depth snapshot at all (see the report: DSV unknown, or nothing followed the terrain)")
    v.append("")

    # ---- previews on this frame
    colour = next((c for c in colours if c[0] == "colour_at_ui"), None) or (colours[-1] if colours else None)
    if best is not None and colour is not None:
        d, info = best[2], best[3]
        rgb = colour[2]
        if d.shape != rgb.shape[:2]:  # render scale: bring depth to the colour size
            d = np.array(Image.fromarray(d.astype(np.float32), "F").resize((rgb.shape[1], rgb.shape[0]), Image.NEAREST), np.float64)
            v.append(f"- depth is {best[2].shape[1]}x{best[2].shape[0]} but colour is {rgb.shape[1]}x{rgb.shape[0]} (render scale): resized")
        sky = d <= 1e-6 if info["reversed"] else d >= 0.999999
        mask = outline_mask(d, sky, info["reversed"])
        ol = rgb.copy()
        ol[mask] = (ol[mask] * 0.08).astype(np.uint8)
        Image.fromarray(ol).save(out / "preview_outlines.png")
        light = ambient_occlusion(d, sky, info["reversed"], radius_px=max(6, rgb.shape[0] // 60))
        ao = (rgb.astype(np.float32) * light[..., None]).astype(np.uint8)
        Image.fromarray(ao).save(out / "preview_ao.png")
        cel = cel_shade(ao, ~sky)
        cel[mask] = (12, 8, 20)
        Image.fromarray(cel).save(out / "preview_cel.png")
        v += [f"## Previews on this frame (colour {colour[0]} + depth {best[0]})",
              f"- preview_outlines.png (ink lines, {100 * mask.mean():.1f}% of the screen), preview_ao.png (soft corner shadows), "
              "preview_cel.png (AO + cel bands + ink)", "- **Depth looks (outlines, AO, haze): GO**", ""]
    else:
        v += ["## Previews", "- need one colour and one usable depth snapshot -> none written", ""]

    # ---- terrain pipelines
    pipes = terrain_pipelines(report)
    v += ["## Terrain pipelines (what a block shader / glass pass replaces)"]
    if pipes:
        for p in pipes[:12]:
            v.append(f"- {p['kind']} | draws {p['draws']} | {'pixel shader REPLACED by a pack | ' if p['replaced'] else ''}vs out `{p['vs_out']}` | {p['targets']}")
    else:
        v.append("- none parsed (shader names not ready? pipelines made before the hooks?)")
    uv = [p for p in pipes if "TEXCOORD0" in p["vs_out"]]
    kinds = sorted({p["kind"] for p in pipes})
    v += [f"- passes seen: {', '.join(kinds) or '-'}; pipelines handing the atlas UV (TEXCOORD0) to the pixel shader: {len(uv)}/{len(pipes)}", ""]

    # ---- atlas + tags
    v += ["## Terrain atlas + tag test pack"]
    stamps = json.loads((P / "stamps.json").read_text()) if (P / "stamps.json").exists() else []
    tags: dict = {}
    van_dir = a.vanilla
    for hj in sorted(P.glob("atlas*.json")):
        name = hj.stem
        hdr, img = load_raw(P, name)
        hits = [s for s in stamps if s["copy"] == name]
        r = wfx_stamps.rects(hits, ids)
        found = 0
        for tid, e in sorted(r.items()):
            rr = e.get("rects") or e.get("rects_from_size") or []
            if rr:
                found += 1
                tl_alpha = sorted({tuple(t[3]) for t in e["tl"]})
                tags.setdefault(e["name"], {"id": tid, "group": ids.get(tid, {}).get("group"), "atlas": name, "rects": rr,
                                            "soft_kept": bool(e["tl"]), "hard_kept": bool(e["br"]),
                                            "soft_alpha": [list(t) for t in tl_alpha]})
        line = f"- {name}: {hdr['resourceFormat']} {hdr['width']}x{hdr['height']} mips {hdr['mips']} array {hdr['array']} | tagged textures found {found}/{len(ids)}"
        if img is None or img.dtype != np.uint8:
            v.append(line + (" | picture not saved by the probe (server-safe default): positions come from stamps.json"
                             if not hdr.get("saved", True) else " | not RGBA8: pixels not searched"))
            continue
        Image.fromarray(img, "RGBA").save(out / f"{name}.png")
        tagged = Image.fromarray(img, "RGBA").convert("RGB")
        dr = ImageDraw.Draw(tagged)
        stored = {"rgb_exact": [], "alpha_exact": [], "premultiplied": []}
        for tname, t in tags.items():
            if t["atlas"] != name:
                continue
            for q in t["rects"]:
                dr.rectangle([q["x"] - 1, q["y"] - 1, q["x"] + q["w"], q["y"] + q["h"]], outline=(255, 0, 255))
                f = PACK_TEX / f"{tname}.png"
                if f.exists():
                    c = compare_rect(img, q, np.array(Image.open(f).convert("RGBA")))
                    if c["ok"]:
                        for k in stored:
                            stored[k].append(c[k])
        van_found = {}
        if van_dir is None:
            try:
                import make_tag_pack
                van_dir = make_tag_pack.fetch_vanilla()
            except Exception as e:  # offline: skip the vanilla search
                v.append(f"  - vanilla textures unavailable ({e}); vanilla search skipped")
        if van_dir and Path(van_dir).exists():  # works without the tag pack, e.g. when a pack above ours replaces glass
            for nm in ("glass", "glass_red", "glass_white", "glass_pane_top", "tinted_glass", "wool_colored_red", "concrete_red"):
                f = Path(van_dir) / f"{nm}.png"
                if f.exists():
                    pos = find_vanilla(img, np.array(Image.open(f).convert("RGBA")))
                    if pos:
                        van_found[nm] = pos
                        for (x, y) in pos:
                            dr.rectangle([x - 1, y - 1, x + 16, y + 16], outline=(0, 255, 255))
        tagged.save(out / f"{name}_tags.png")
        v.append(line + f" | vanilla matches: {', '.join(f'{k} x{len(p)}' for k, p in van_found.items()) or 'none'}")
        if stored["rgb_exact"]:
            v.append(f"  - how the game stored our textures: RGB exact on {100 * np.mean(stored['rgb_exact']):.1f}% of texels, alpha exact on "
                     f"{100 * np.mean(stored['alpha_exact']):.1f}%, looks premultiplied on {100 * np.mean(stored['premultiplied']):.1f}%")
    (out / "tags.json").write_text(json.dumps(tags, indent=1) + "\n")
    groups: dict = {}
    for n, t in tags.items():
        groups.setdefault(t["group"], []).append(n)
    want: dict = {}
    for i in ids.values():
        want.setdefault(i["group"], []).append(i["name"])
    for g, gnames in want.items():
        got = groups.get(g, [])
        miss = [n for n in gnames if n not in got]
        v.append(f"- group {g}: {len(got)}/{len(gnames)} located" + (f" (missing: {', '.join(miss[:6])}{'...' if len(miss) > 6 else ''})" if miss else ""))
    soft_lost = [n for n, t in tags.items() if not t["soft_kept"]]
    if tags and soft_lost:
        v.append(f"- soft stamps lost on {len(soft_lost)} textures (the game changes RGB under alpha < 255): {', '.join(soft_lost[:8])}")
    # alpha tags: does the atlas keep the alpha we put on opaque textures?
    by_name = {i["name"]: i for i in ids.values()}
    a_ok, a_bad = [], []
    for n, t in tags.items():
        tag = by_name.get(n, {}).get("alpha_tag")
        if tag is None or not t["soft_alpha"]:
            continue
        (a_ok if all(al == [tag] * 4 for al in t["soft_alpha"]) else a_bad).append((n, tag, t["soft_alpha"][0]))
    if a_ok or a_bad:
        v.append(f"- **alpha tags** (opaque textures carrying an ID in alpha): kept exactly on {len(a_ok)}/{len(a_ok) + len(a_bad)}"
                 + (f"; changed, e.g. {a_bad[0][0]} wanted {a_bad[0][1]} got {a_bad[0][2]}" if a_bad else "")
                 + " -> " + ("a block shader can read the tag straight from the texture (check in game that tagged concrete looks normal)"
                             if a_ok and not a_bad else "the atlas does not keep them: use atlas rectangles instead"))
    glass_known = any(t["group"] in ("glass", "glass_pane") for t in tags.values())
    m2 = bool(uv) and glass_known
    v += ["", f"**Refraction glass: {'GO' if m2 else 'NO-GO yet'}** - "
          + ("terrain pixel shaders get the atlas UV and the glass rectangles are known (tags.json)" if m2 else
             ("no glass located in the atlas (tag pack not active / overridden?)" if uv else "no terrain pipeline with TEXCOORD0 parsed")), ""]
    (out / "verdict.md").write_text("\n".join(v) + "\n")
    print("\n".join(v))
    print(f"\nwritten: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
