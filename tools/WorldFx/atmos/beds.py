"""World FX beds: the Bed Wars bed repainted - a quilted blanket in the team colour, a soft linen pillow, an oak
frame (by Novak). Uses the vanilla red bed as the class map (blanket / linen / wood), so every colour shares it."""
from __future__ import annotations

import numpy as np
from PIL import Image

from tex import ramp, rng, value_noise


def classes(red_rgba):
    a = red_rgba.astype(int)
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    mx, mn = a[..., :3].max(-1), a[..., :3].min(-1)
    blanket = (al > 0) & (mx - mn > 60) & (r > g + 40) & (g < 80)
    linen = (al > 0) & ~blanket & (mn > 150)
    wood = (al > 0) & ~blanket & ~linen
    return blanket, linen, wood


def quilt(h, w, seed):
    """diamond quilting: puffy cells, sunk stitched seams"""
    y, x = np.mgrid[0:h, 0:w].astype(float) + 0.5
    p = 6.0
    a = np.abs(((x + y) % p) - p / 2) / (p / 2)
    b = np.abs(((x - y) % p) - p / 2) / (p / 2)
    puff = np.minimum(a, b)
    sh = puff - np.roll(np.roll(puff, 1, 0), 1, 1)         # lit from the top-left: each cell a little pillow
    f = 0.36 + puff * 0.40 + sh * 0.9 + (value_noise(64, 4, seed, 1)[:h, :w] - 0.5) * 0.08
    seam = puff < 0.18
    st = seam & (((x + y).astype(int)) % 2 == 0)
    f = np.where(st, 0.20, f)
    return f


def paint(red_rgba, colour, wood_pal, shadow_hue=292.0, light_hue=62.0, seed=0, knit=None):
    blanket, linen, wood = classes(red_rgba)
    H, W = red_rgba.shape[:2]
    out = red_rgba.copy()
    bp = np.array(ramp(colour, n=6, lo=-0.16, hi=0.07, shadow_hue=shadow_hue, light_hue=light_hue, shadow_shift=0.2), np.uint8)
    lp = np.array([(176, 168, 176), (204, 198, 200), (226, 220, 218), (240, 236, 230), (250, 247, 240)], np.uint8)
    wp = np.array(wood_pal, np.uint8)
    q = quilt(H, W, seed)
    if knit is not None:                                   # the team's own wool: bed and bridge share one knit
        kt = np.tile(knit, (H // knit.shape[0] + 1, W // knit.shape[1] + 1, 1))[:H, :W]
        out[..., :3] = np.where(blanket[..., None], kt, out[..., :3])
    else:
        out[..., :3] = np.where(blanket[..., None], bp[np.clip((q * 6).astype(int), 0, 5)], out[..., :3])
    y, x = np.mgrid[0:H, 0:W]
    top_pillow = (x >= 6) & (x < 22) & (y >= 6) & (y < 14) & (red_rgba[..., 3] > 0)
    linen = (linen | top_pillow) & ~(blanket & ~top_pillow)
    wood = wood & ~top_pillow
    lin = 0.55 + (value_noise(64, 6, seed + 2, 2)[:H, :W] - 0.5) * 0.35
    px, py = x - 6, y - 6                                  # the pillow: a puffy rounded cushion inset 1 px
    inside = (px >= 1) & (px <= 14) & (py >= 1) & (py <= 6) & ~(((px == 1) | (px == 14)) & ((py == 1) | (py == 6)))
    dx = np.minimum(px - 1, 14 - px) / 6.5
    dy = np.minimum(py - 1, 6 - py) / 2.5
    dome = np.clip(np.minimum(dx, dy), 0, 1) ** 0.5
    pil = 0.35 + dome * 0.5 + np.where(px + py < 8, 0.08, -0.04)
    lin = np.where(top_pillow & inside, pil, np.where(top_pillow, 0.25, lin))
    out[..., :3] = np.where(linen[..., None], lp[np.clip((lin * 5).astype(int), 0, 4)], out[..., :3])
    gr = np.sin(y * 1.9 + (value_noise(64, 3, seed + 3, 1)[:H, :W] - 0.5) * 5) * 0.5 + 0.5
    wf = 0.40 + (gr - 0.5) * 0.35
    out[..., :3] = np.where(wood[..., None], wp[np.clip((wf * len(wp)).astype(int), 0, len(wp) - 1)], out[..., :3])
    return out


def export(pack, vanilla_bed_dir, dyes, wood_pal, knit_fn=None):
    src = np.array(Image.open(vanilla_bed_dir / "red.png").convert("RGBA"))
    dst = pack / "textures" / "entity" / "bed"
    dst.mkdir(parents=True, exist_ok=True)
    for i, (dye, col) in enumerate(dyes.items()):
        kn = knit_fn(col, i) if knit_fn else None
        Image.fromarray(paint(src, col, wood_pal, seed=i, knit=kn), "RGBA").save(dst / f"{dye}.png")
