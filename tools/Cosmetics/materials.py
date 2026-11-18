"""Realistic knife materials, painted procedurally (by Novak) - every knife gets its OWN finish, the way real knives
are designed: Damascus, stonewash, mirror polish, titanium, cocobolo, brass, blued steel.

Blade painters work in BLADE-FACE space: u = along the blade (0 heel -> 1 tip), v = across it (0 spine -> 1 edge for a
single edge; 0 ridge -> 1 edge for a double edge). Every facet of one side samples the same face texture at its own
v-span, so a pattern (Damascus layers, grind lines) runs continuously over the flat, the grind and the edge bevel,
while the real geometry facets still catch the light differently.
Alpha carries the World FX actor material id (see tools/WorldFx/atmos/iconmodel.py): steel = metal (12).
"""
from __future__ import annotations

import math

import numpy as np

ID_METAL = 255 - 4 * 12
GRIND_V = 0.36      # where the primary grind starts (single edge); edge bevel from EDGE_V
EDGE_V = 0.86


def fbm(h, w, seed, octaves=4, base=4):
    """smooth value-noise fbm, 0..1, h x w"""
    r = np.random.default_rng(seed)
    out = np.zeros((h, w))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = base * 2 ** o
        g = r.random((n + 1, n + 1))
        y = np.linspace(0, n, h)[:, None]
        x = np.linspace(0, n, w)[None, :]
        y0, x0 = np.minimum(y.astype(int), n - 1), np.minimum(x.astype(int), n - 1)
        fy, fx = y - y0, x - x0
        fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
        v = (g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx) * (1 - fy) + (g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx) * fy
        out += amp * v
        tot += amp
        amp *= 0.5
    return out / tot


def rgba(rgb, a=255):
    out = np.zeros(rgb.shape[:2] + (4,), np.uint8)
    out[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    out[..., 3] = a
    return out


def mix(a, b, t):
    return a + (np.asarray(b, float) - a) * np.asarray(t)[..., None]


def blade_finish(rgb, v, single=True, polish_bevel=True):
    """what a real grind does to any finish: the edge bevel is freshly sharpened (bright, fine scratches across it,
    the pattern ground away), a crisp apex line at the very edge, a slightly darker shoulder where the bevel starts"""
    h = rgb.shape[0]
    bevel = np.clip((v - EDGE_V) / (1 - EDGE_V), 0, 1)[:, None] * np.ones(rgb.shape[:2])
    if polish_bevel:
        rgb = mix(rgb, [206, 208, 214], 0.85 * (bevel > 0))
    rgb = rgb + ((np.random.default_rng(3).random(rgb.shape[:2]) - 0.5) * 10 * (bevel > 0))[..., None]
    apex = (v > 0.975)[:, None] * np.ones(rgb.shape[:2], bool)
    rgb[apex] = [236, 238, 244]
    shoulder = (np.abs(v - EDGE_V) < 0.6 / h)[:, None] * np.ones(rgb.shape[:2], bool)
    rgb[shoulder] *= 0.7
    if single:
        gl = (np.abs(v - GRIND_V) < 0.6 / h)[:, None] * np.ones(rgb.shape[:2], bool)
        rgb[gl] *= 0.78                                          # the grind line: a crisp step, not a stripe
    return rgb


# ------------------------------------------------------------------ blade faces
def damascus(w, h, seed=5, layers=13, blade=True):
    """pattern-welded "random" Damascus, acid etched. Real Damascus is forged WITH the blade, so its layers run along
    the blade and flow like water or smoke (never a repeating stamp): here the layers follow the blade face (v across,
    u along), folded by a low-frequency domain warp plus a finer second warp for the tight ripples. Etch: the
    high-nickel layers stay bright satin, the carbon layers go matte charcoal; the edges between layers are crisp
    but antialiased. On the sharpened bevel the etch is polished back: same pattern, low contrast, brighter."""
    v = np.linspace(0, 1, h)[:, None]
    u = np.linspace(0, 1, w)[None, :]
    big = fbm(h, w, seed, 3, 2) - 0.5
    mid = fbm(h, w, seed + 1, 3, 5) - 0.5
    y = v * layers + big * 5.5 + mid * 1.6 + np.sin(u * 7.0 + big * 4) * 0.35
    px = layers / h                                                    # one texel in layer units (antialias width)
    width = 0.30 + 0.38 * fbm(h, w, seed + 3, 2, 3)                   # layer thickness varies along the bar
    tri = np.abs(((y % 1.0) + 1.0) % 1.0 - 0.5) * 2                    # 0 at layer centre .. 1 at the boundary
    f = np.clip((tri - (1 - width)) / max(px * 2.0, 0.05) + 0.5, 0, 1)  # thick bright layers, crisp + antialiased
    y2 = y * 3.0 + mid * 2.0                                           # the fine lamination between them
    tri2 = np.abs(((y2 % 1.0) + 1.0) % 1.0 - 0.5) * 2
    fine = np.clip((tri2 - 0.78) / max(px * 6.0, 0.08) + 0.5, 0, 1) * 0.55
    f = np.maximum(f, fine)
    dark, bright = np.array([62, 63, 70.0]), np.array([182, 184, 190.0])
    rgb = dark + (bright - dark) * f[..., None]
    rgb += (fbm(h, w, seed + 7, 2, 40)[..., None] - 0.5) * 8            # fine etch grain
    if not blade:                                                      # tang / ricasso / ring: same billet, no grinds
        return rgba(rgb, ID_METAL)
    bevel = np.clip((v - EDGE_V) / (1 - EDGE_V) * 4, 0, 1) * np.ones((h, w))
    pol = 150 + (rgb - 150) * 0.35 + 45
    rgb = rgb * (1 - bevel[..., None]) + pol * bevel[..., None]
    return rgba(blade_finish(rgb, v[:, 0], polish_bevel=False), ID_METAL)


def stonewash(w, h, seed=11):
    """stonewash = tumbled with abrasive stones: an even matte grey with a soft pebbled mottle and countless tiny
    scuffs in every direction; from a distance it reads as satin, close up it is gently mottled - LOW contrast."""
    r = np.random.default_rng(seed)
    v = np.linspace(0, 1, h)
    rgb = np.zeros((h, w, 3)) + [146, 148, 154.0]
    yy, xx = np.mgrid[0:h, 0:w]
    blot = np.zeros((h, w))
    for _ in range(int(w * h / 14)):                                   # pebble mottle: soft overlapping splats
        cx, cy, rad, a = r.random() * w, r.random() * h, 2.0 + r.random() * 4.0, (r.random() - 0.5) * 2
        x0, x1, y0, y1 = int(max(cx - 3 * rad, 0)), int(min(cx + 3 * rad + 1, w)), int(max(cy - 3 * rad, 0)), int(min(cy + 3 * rad + 1, h))
        blot[y0:y1, x0:x1] += a * np.exp(-((xx[y0:y1, x0:x1] - cx) ** 2 + (yy[y0:y1, x0:x1] - cy) ** 2) / (rad * rad))
    rgb += (blot / (np.abs(blot).max() + 1e-6))[..., None] * 12
    scuff = np.zeros((h, w))
    for _ in range(int(w * h / 40)):                                   # faint curved scuffs, any direction
        x, y, a, ln = r.random() * w, r.random() * h, r.random() * 2 * math.pi, 3 + r.random() * 6
        bend = (r.random() - 0.5) * 0.5
        tone = 1.0 if r.random() < 0.6 else -0.8
        for t in np.linspace(0, ln, int(ln * 2)):
            xi, yi = int(x + math.cos(a + bend * t) * t), int(y + math.sin(a + bend * t) * t)
            if 0 <= xi < w and 0 <= yi < h:
                scuff[yi, xi] = tone
    rgb += scuff[..., None] * 5
    return rgba(blade_finish(rgb, v), ID_METAL)


def mirror(w, h, seed=17, double=True, symmetric=False):
    """mirror polish: it reflects the room - a dark band, a bright window band, soft floor - along the blade, with
    faint polishing streaks; a double edge has the ridge bright and both bevels clean"""
    v = np.linspace(0, 1, h)
    if symmetric:                                       # edge (v=0) - ridge (v=0.5) - edge (v=1): fold to ridge->edge
        v = np.abs(2 * v - 1)[::-1] if False else 1 - np.abs(2 * v - 1)
        v = 1 - v
    k = np.interp(v, [0.0, 0.10, 0.34, 0.345, 0.55, 0.78, 1.0], [0.95, 0.62, 0.26, 0.90, 0.72, 0.38, 0.62])
    rgb = mix(np.zeros((h, w, 3)) + [44, 46, 54.0], [232, 234, 240], k[:, None] * np.ones((h, w)))
    rgb += np.stack([np.sin(np.arange(w) * 0.21 + j * 1.7) for j in range(h)])[..., None] * 3
    return rgba(blade_finish(rgb, v, single=not double), ID_METAL)


def spine_steel(w, h, tone=(96, 96, 104), jimp_to=0.2, jimp_from=0.0):
    rgb = np.zeros((h, w, 3)) + np.array(tone, float)
    v = np.linspace(-1, 1, h)[:, None]
    rgb += (1 - np.abs(v))[..., None] ** 2 * 40
    u = np.arange(w)
    j = (u >= w * jimp_from) & (u < w * jimp_to) & (u % 2 == 1)
    rgb[:, j] *= 0.45
    return rgba(rgb, ID_METAL)


# ------------------------------------------------------------------ handles / hardware
def cocobolo(w, h, seed=23):
    """cocobolo rosewood, oiled: orange-red body that deepens to purple-brown, dramatic dark streaks with crisp wavy
    edges running with the grain (along u), continuous fine grain lines, sparse pores; v goes round the section:
    the steel tang and a thin red G10 liner show at the spine and belly seams (v = 0 / 0.5), as on a full-tang knife."""
    v = (np.arange(h) + 0.5) / h
    u = np.linspace(0, 1, w)[None, :]
    big = fbm(h, w, seed, 3, 2) - 0.5
    y = v[:, None] * 7 + big * 2.6 + np.sin(u * 4.0 + big * 3) * 0.25   # streak coordinate
    sb = np.abs(((y % 1.0) + 1) % 1.0 - 0.5) * 2
    streak = np.clip((sb - (0.80 + 0.12 * fbm(h, w, seed + 4, 2, 3))) / 0.04, 0, 1) * (0.45 + 0.55 * fbm(h, w, seed + 6, 2, 4))
    yg = v[:, None] * 40 + big * 9 + (fbm(h, w, seed + 2, 2, 6) - 0.5) * 2
    fg = np.abs(((yg % 1.0) + 1) % 1.0 - 0.5) * 2
    grain = np.clip((fg - 0.72) / 0.22, 0, 1) ** 1.5
    tone = fbm(h, w, seed + 9, 3, 3)
    rgb = mix(np.zeros((h, w, 3)) + [172, 82, 40.0], [108, 40, 30], tone)
    rgb = mix(rgb, [196, 112, 56], np.clip((fbm(h, w, seed + 12, 3, 3) - 0.62) * 3, 0, 1) * 0.6)
    rgb = mix(rgb, [70, 28, 22], grain * 0.45)
    rgb = mix(rgb, [52, 22, 20], streak)
    pores = (np.random.default_rng(seed).random((h, w)) < 0.008)
    rgb[pores] *= 0.75
    return rgba(rgb, 255)


def g10_liner(w, h):
    """red G10 liner: fine layered fibreglass, a slightly lighter line at mid-thickness"""
    rgb = np.zeros((h, w, 3)) + [132, 22, 26.0]
    rgb += (np.random.default_rng(71).random((h, w)) - 0.5)[..., None] * 8
    return rgba(rgb, 255)


def titanium(w, h, seed=31, tone=(108, 110, 116)):
    """bead-blasted titanium: fine even grain, cool grey, the milled edges catch a brighter chamfer (v ends)"""
    v = np.linspace(0, 1, h)[:, None]
    rgb = np.zeros((h, w, 3)) + np.array(tone, float)
    rgb += (np.random.default_rng(seed).random((h, w)) - 0.5)[..., None] * 16
    rgb += (fbm(h, w, seed, 2, 4)[..., None] - 0.5) * 10
    edge = np.exp(-(np.minimum(v, 1 - v) / 0.08) ** 2)
    rgb = mix(rgb, [196, 198, 204], (0.7 * edge) * np.ones((h, w)))
    return rgba(rgb, ID_METAL)


def milled_grooves(w, h, seed=33):
    """titanium with diagonal milled grooves (balisong handle face): grooves darker, lands bright-blasted"""
    y, x = np.mgrid[0:h, 0:w]
    g = np.sin((x * 0.9 + y * 0.9) * 0.9) > 0.35
    rgb = np.zeros((h, w, 3)) + [112, 114, 120.0]
    rgb += (np.random.default_rng(seed).random((h, w)) - 0.5)[..., None] * 14
    rgb[g] *= 0.62
    return rgba(rgb, ID_METAL)


def bronze_ano(w, h, seed=37):
    """bronze-anodised titanium hardware (pivots, latch, zen pins): warm gold-bronze with a bright rim"""
    v = (np.arange(h) + 0.5) / h
    lit = 0.5 + 0.5 * np.cos(2 * math.pi * v)
    rgb = mix(np.zeros((h, w, 3)) + [104, 72, 34.0], [214, 170, 96], (lit[:, None] * np.ones((h, w))) ** 1.4)
    rgb += (np.random.default_rng(seed).random((h, w)) - 0.5)[..., None] * 8
    return rgba(rgb, ID_METAL)


def polished_steel(w, h, seed=41):
    v = (np.arange(h) + 0.5) / h
    lit = 0.5 + 0.5 * np.cos(2 * math.pi * v)
    rgb = mix(np.zeros((h, w, 3)) + [70, 72, 80.0], [228, 230, 236], (lit[:, None] * np.ones((h, w))) ** 1.2)
    return rgba(rgb, ID_METAL)


def brass_rings(w, h, rings=15, seed=43):
    """knurled brass ring grip: bright worn crests, darker patina in the grooves, a fine diamond knurl"""
    u = (np.arange(w) + 0.5) / w
    crest = (np.cos(u * 2 * math.pi * rings) * 0.5 + 0.5) ** 1.6
    rgb = mix(np.zeros((h, w, 3)) + [70, 52, 24.0], [222, 182, 104], crest[None, :] * np.ones((h, w)))
    y, x = np.mgrid[0:h, 0:w]
    knurl = ((x + y) % 3 == 0) | ((x - y) % 3 == 0)
    rgb[knurl] *= 0.86
    rgb = mix(rgb, [78, 86, 60], (fbm(h, w, seed, 2, 3) > 0.66) * 0.25)                 # a little verdigris
    return rgba(rgb, ID_METAL)


def blued_steel(w, h, seed=47):
    """hot-blued steel (the dagger guard): blue-black with a faint purple-blue sheen where it turns to the light"""
    v = np.linspace(0, 1, h)[:, None]
    rgb = np.zeros((h, w, 3)) + [24, 26, 33.0]
    rgb = mix(rgb, [86, 96, 128], (np.exp(-((v - 0.3) / 0.14) ** 2)) * np.ones((h, w)) * 0.85)
    rgb = mix(rgb, [150, 156, 176], (np.exp(-((v - 0.3) / 0.03) ** 2)) * np.ones((h, w)) * 0.6)
    rgb += (np.random.default_rng(seed).random((h, w)) - 0.5)[..., None] * 6
    return rgba(rgb, ID_METAL)


def brass_plain(w, h, seed=53):
    v = (np.arange(h) + 0.5) / h
    lit = 0.5 + 0.5 * np.cos(2 * math.pi * v)
    rgb = mix(np.zeros((h, w, 3)) + [92, 66, 28.0], [226, 188, 112], (lit[:, None] * np.ones((h, w))) ** 1.3)
    return rgba(rgb, ID_METAL)


def black_parts(w, h, seed=59):
    rgb = np.zeros((h, w, 3)) + [30, 30, 34.0]
    rgb += (np.random.default_rng(seed).random((h, w)) - 0.5)[..., None] * 8
    return rgba(rgb, ID_METAL)


def slot_floor(w, h, seed=61):
    """carbon-fibre spacer seen through a slot: 2x2 twill weave, near-black with a cool sheen, darker at the slot's
    ends (shadow under the titanium)"""
    y, x = np.mgrid[0:h, 0:w]
    tw = ((x // 3 + y // 3) % 2 == 0)
    rgb = np.zeros((h, w, 3)) + [30, 31, 36.0]
    rgb[tw] += [16, 17, 22]
    v = np.linspace(0, 1, h)[:, None]
    u = np.linspace(0, 1, w)[None, :]
    shade = np.clip(np.minimum(np.minimum(v, 1 - v) / 0.06, np.minimum(u, 1 - u) / 0.22), 0.3, 1)
    rgb = rgb * shade[..., None]
    ch = max(1, w // 16)                                    # milled chamfer round the slot: lit on one side, dark on the other
    rgb[:, :ch] = [128, 130, 138]
    rgb[:, -ch:] = [70, 72, 80]
    rgb[:ch, :] = [120, 122, 130]
    rgb[-ch:, :] = [80, 82, 90]
    return rgba(rgb, ID_METAL)


# ------------------------------------------------------------------ maker's mark (ours): a small etched hexagon with an N
_N = ["10001", "11001", "10101", "10011", "10001"]


def makers_mark(img, u0, v0, size_px, frosted=True):
    """etch the Novak mark into a face texture in place: hexagon outline + N, frosted (lighter, low contrast) on dark
    steel or darker on bright steel - subtle, like a real laser-etched maker's mark"""
    h, w = img.shape[:2]
    cx, cy = int(u0 * w), int(v0 * h)
    r = size_px / 2
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = np.abs(xx - cx), np.abs(yy - cy)
    hexd = np.maximum(dx * 0.866 + dy * 0.5, dy)                      # flat-top hexagon distance
    ring = (hexd > r - 1.2) & (hexd <= r)
    mark = ring.copy()
    cell = max(1, int(size_px / 9))
    for j, row in enumerate(_N):
        for i, ch in enumerate(row):
            if ch == "1":
                x0, y0 = cx - int(2.5 * cell) + i * cell, cy - int(2.5 * cell) + j * cell
                mark[max(y0, 0):y0 + cell, max(x0, 0):x0 + cell] = True
    rgb = img[..., :3].astype(float)
    lum = rgb.mean(-1, keepdims=True)
    etched = np.where(lum > 140, rgb * 0.72, rgb + (200 - rgb) * 0.35)
    rgb[mark] = etched[mark]
    img[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return img
