"""Cloud Sea atmosphere: block textures (by Novak).

Light: low warm sun, violet sky fill (docs/WORLDFX_ART_BIBLE.md). Ramps lean violet in shade, warm in light.
Each painter returns an RGB(A) uint8 array; build_pack.py names them and adds the material ids.
"""
from __future__ import annotations

import numpy as np

from tex import (normalize, poisson_points, quantize, clean_isolated, ramp, rng, shift_light, value_noise, voronoi)

S = 32  # texture size (2x vanilla: room for real structure, still reads as Minecraft)
SHADOW_HUE, LIGHT_HUE = 292.0, 62.0

# Hive team colours stay instantly readable: vanilla hues, tuned values (no neon, no mud)
DYES = {
    "white": (232, 228, 222), "orange": (232, 120, 34), "magenta": (190, 72, 176), "light_blue": (70, 170, 214),
    "yellow": (240, 196, 52), "lime": (118, 184, 38), "pink": (234, 140, 170), "gray": (72, 74, 82),
    "silver": (148, 146, 142), "cyan": (26, 138, 148), "purple": (122, 48, 172), "blue": (56, 66, 162),
    "brown": (118, 76, 46), "green": (86, 112, 34), "red": (168, 44, 40), "black": (30, 28, 36),
}


def R(base, **kw):
    kw.setdefault("shadow_hue", SHADOW_HUE)
    kw.setdefault("light_hue", LIGHT_HUE)
    return ramp(base, **kw)


def _ss(fn, s=S, k=6):
    """supersample a shape function f(u, v) (pixel coords, floats) -> 0..1, k*k samples per pixel"""
    o = (np.arange(k) + 0.5) / k
    y, x = np.mgrid[0:s, 0:s].astype(float)
    acc = np.zeros((s, s))
    for oy in o:
        for ox in o:
            acc += fn(x + ox, y + oy)
    return acc / (k * k)


# ------------------------------------------------------------------ wool: chunky knit (stockinette chevrons), soft and matte
def wool(colour, seed=1):
    cw, ch = 8.0, 8.0                                # chunky sweater knit: a "V" 8 px wide, 8 px tall (4x4 per block)

    def stitch(x, y):
        u, v = np.mod(x, cw), np.mod(y + 2.0, ch)      # +2: the V's of the next row tuck under this one
        # left leg "\": centre line from (1.3, 0) to (3.6, 8); right leg "/" mirrored about x = 4
        out = np.zeros_like(u)
        for side in (-1.0, 1.0):
            uu = u if side < 0 else cw - u
            cx = 1.3 + v * 0.29
            d = (uu - cx) * 0.96                         # ~perpendicular distance to the slanted leg
            body = np.clip(1.0 - (np.abs(d) / 2.25) ** 2.6, 0, 1)
            light = 0.80 + 0.20 * np.clip(-d / 2.25, -1, 1)   # the outer-upper side of each leg catches light
            tuck = 0.62 + 0.38 * np.sin(np.pi * np.clip(v / ch * 0.92 + 0.06, 0, 1))
            out = np.maximum(out, body * light * tuck)
        return out

    h = _ss(stitch)
    r = rng(seed)
    st = r.normal(0, 0.035, (int(S / ch), int(S / cw)))
    yy, xx = np.mgrid[0:S, 0:S]
    h += st[(yy // int(ch)) % st.shape[0], (xx // int(cw)) % st.shape[1]]
    lit = h
    f = normalize(lit)
    fuzz = (r.random((S, S)) < 0.012) & (f > 0.45) & (f < 0.75)
    f = np.where(fuzz, f + 0.16, f)
    pal = R(colour, n=5, lo=-0.12, hi=0.05, shadow_shift=0.18)
    img, idx = quantize(np.clip(f, 0, 1) ** 0.75, pal)
    img, _ = clean_isolated(idx, pal)
    return img


# ------------------------------------------------------------------ concrete: dense, clean, cast panel
def concrete(colour, seed=2):
    m = value_noise(S, 2, seed, octaves=3, persist=0.6)
    f = 0.48 + (m - 0.5) * 0.16                       # cast surface: barely-there clouding, never a stain
    y, x = np.mgrid[0:S, 0:S]
    f = np.where((y == 0) | (x == 0), 0.70, f)        # a soft chamfer: one shade up on the lit edges
    f = np.where((y == S - 1) | (x == S - 1), 0.28, f)  # one shade down on the far ones
    r = rng(seed + 3)
    agg = r.random((S, S))
    inner = (x > 0) & (y > 0) & (x < S - 1) & (y < S - 1)
    f = np.where((agg < 0.012) & inner, f - 0.20, f)  # rare air pores
    f = np.where((agg > 0.992) & inner, f + 0.20, f)  # and a fleck of aggregate
    pal = R(colour, n=5, lo=-0.09, hi=0.05, shadow_shift=0.18)
    img, _ = quantize(np.clip(f, 0, 1), pal)
    return img


# ------------------------------------------------------------------ terracotta: fired clay, strata + speckle
def terracotta(colour, seed=3):
    y, x = np.mgrid[0:S, 0:S].astype(float)
    warp = (value_noise(S, 2, seed, octaves=2) - 0.5) * 5.0
    band = np.sin((y + warp) / S * 2 * np.pi * 3.0) * 0.5 + 0.5
    f = 0.42 + band * 0.20 + (value_noise(S, 6, seed + 1) - 0.5) * 0.18
    r = rng(seed + 2)
    sp = r.random((S, S))
    f = np.where(sp < 0.018, f + 0.22, f)
    f = np.where(sp > 0.985, f - 0.22, f)
    pal = R(colour, n=6, lo=-0.12, hi=0.07, shadow_shift=0.2)
    img, idx = quantize(np.clip(f, 0, 1), pal)
    return img


# ------------------------------------------------------------------ oak planks: four boards, real grain
def planks(base=(176, 132, 82), seed=4):
    boards = 4
    bh = S // boards
    r = rng(seed)
    y, x = np.mgrid[0:S, 0:S].astype(float)
    f = np.zeros((S, S))
    for b in range(boards):
        rows = (y >= b * bh) & (y < (b + 1) * bh)
        joint = r.integers(6, S - 6)
        off = r.random() * 100
        # grain: long lines along x, gently waving, with one knot per board that bends them
        kx, ky = r.uniform(4, S - 4), b * bh + r.uniform(2, bh - 2)
        dx = np.minimum(np.abs(x - kx), S - np.abs(x - kx))
        dk = np.sqrt((dx / 2.2) ** 2 + (y - ky) ** 2)
        bend = 1.6 * np.exp(-dk / 2.0)
        g = np.sin((y + bend + (value_noise(S, 2, seed + b, 2) - 0.5) * 2.5) * 2.2 + off) * 0.5 + 0.5
        tone = 0.50 + r.uniform(-0.07, 0.07)
        fb = tone + (g - 0.5) * 0.24
        fb = np.where(dk < 1.1, tone - 0.30, fb)                                # the knot itself
        fb = np.where((dk >= 1.1) & (dk < 1.8), tone - 0.12, fb)
        # board edges: lit top lip, dark seam at the bottom, end joint
        fb = np.where(y == b * bh, fb + 0.20, fb)
        fb = np.where(y == (b + 1) * bh - 1, 0.04, fb)
        fb = np.where((x == joint) & rows, 0.06, fb)
        fb = np.where((x == (joint + 1) % S) & rows, fb + 0.14, fb)
        f = np.where(rows, fb, f)
    pal = R(base, n=7, lo=-0.24, hi=0.10, shadow_shift=0.25)
    img, idx = quantize(np.clip(f, 0, 1), pal)
    img, _ = clean_isolated(idx, pal)
    return img


# ------------------------------------------------------------------ end stone: pale pumice, lavender pores
def end_stone(base=(226, 222, 168), seed=5):
    """pale pumice: crisp pores of a few sizes, each with a lavender inner shadow on its lit side and a pale lip on
    the far side (light from the top-left), on a calm, slightly lumpy body"""
    pts = poisson_points(S, 18, 5.5, seed)
    d1, d2, idx = voronoi(S, pts)
    r = rng(seed)
    radius = r.choice([0.7, 1.0, 1.5, 2.1], len(pts), p=[0.35, 0.35, 0.2, 0.1])[idx]
    pit = d1 < radius
    body = 0.62 + (value_noise(S, 3, seed + 1, 2) - 0.5) * 0.18
    h = np.where(pit, 0.0, 1.0)
    sh = shift_light(h, 1, 1)                           # >0: lip facing the light; <0: shadowed inner wall
    f = body.copy()
    f = np.where(pit, 0.10, f)
    f = np.where(pit & (np.roll(np.roll(~pit, 1, 0), 1, 1)), 0.26, f)    # far wall of the pore, lit
    f = np.where(~pit & (sh > 0.5), f + 0.16, f)
    # hand-set ramp: pores sink to a cool olive-lavender, never pink
    pal = [(96, 92, 92), (134, 130, 118), (178, 174, 140), (206, 202, 158), (226, 222, 172), (240, 236, 192)]
    img, idx2 = quantize(np.clip(f, 0, 1), pal)
    img, _ = clean_isolated(idx2, pal)
    return img


# ------------------------------------------------------------------ obsidian: black glass, conchoidal facets
def obsidian(seed=6):
    """volcanic glass: a few broad planes in near-black violet, edges between them a touch darker; light only as a
    handful of short glint streaks on the planes that face it, plus rare deep-violet specks"""
    pts = poisson_points(S, 6, 11.0, seed)
    d1, d2, idx = voronoi(S, pts)
    r = rng(seed)
    y, x = np.mgrid[0:S, 0:S].astype(float)
    n = len(pts)
    ang = r.uniform(0, 2 * np.pi, n)
    ca, sa = np.cos(ang)[idx], np.sin(ang)[idx]
    cx = np.array([p[0] for p in pts])[idx]; cy = np.array([p[1] for p in pts])[idx]
    ddx = (x + 0.5 - cx + S / 2) % S - S / 2; ddy = (y + 0.5 - cy + S / 2) % S - S / 2   # wrap-aware: no tile seams
    grad = (ddx * ca + ddy * sa) / 12.0
    f = r.uniform(0.18, 0.40, n)[idx] + grad * 0.14
    f = np.where((d2 - d1) < 0.8, f - 0.10, f)
    facing = r.random(n) < 0.5
    for i in range(n):                                    # glint streaks: 2-4 px diagonals, only on lit planes
        if not facing[i]:
            continue
        px, py = pts[i]
        L = int(r.integers(2, 5))
        for k in range(L):
            xx, yy = int(px - 2 + k) % S, int(py - 2 + k) % S
            f[yy, xx] = 0.95 if k in (1, 2) else 0.78
    sp = r.random((S, S)) < 0.012
    f = np.where(sp, 0.66, f)
    pal = [(12, 9, 19), (19, 13, 31), (28, 19, 45), (40, 27, 64), (64, 42, 100), (118, 92, 168), (190, 170, 232)]
    img, idx2 = quantize(np.clip(f, 0, 1), pal)
    return img


# ------------------------------------------------------------------ stone family
def stone(base=(128, 126, 128), seed=7):
    """weathered smooth stone: a few large soft plates in close tones, some of their edges open into thin cracks
    with a lit lip; calm surface, rare flecks"""
    pts = poisson_points(S, 6, 11.0, seed)
    d1, d2, idx = voronoi(S, pts)
    r = rng(seed)
    n = len(pts)
    f = r.uniform(0.49, 0.56, n)[idx] + (value_noise(S, 4, seed + 1, 2) - 0.5) * 0.20
    gap = d2 - d1
    j = np.minimum(idx, np.roll(idx, 1, 1))
    open_edge = r.random(n * n).reshape(n, n) < 0.30
    nb = np.where(gap < 2.0, 1, 0)
    # which neighbour: approximate by the second-nearest site - recompute cheaply
    crack = np.zeros((S, S), bool)
    for _ in range(3):
        cx0, cy0 = int(r.integers(0, S)), int(r.integers(0, S))
        for k in range(int(r.integers(3, 6))):
            crack[(cy0 + k) % S, (cx0 + k // 2) % S] = True
    lip = np.roll(np.roll(crack, 1, 0), 1, 1) & ~crack
    f = np.where(crack, 0.08, f)
    f = np.where(lip, f + 0.14, f)
    fl = r.random((S, S))
    f = np.where((fl < 0.015) & ~crack, f + 0.18, f)
    f = np.where((fl > 0.988) & ~crack, f - 0.16, f)
    pal = R(base, n=6, lo=-0.24, hi=0.09, shadow_shift=0.4)
    img, idx2 = quantize(np.clip(f, 0, 1), pal)
    img, _ = clean_isolated(idx2, pal)
    return img


def cobblestone(base=(126, 122, 120), seed=8):
    pts = poisson_points(S, 11, 8.2, seed)
    d1, d2, idx = voronoi(S, pts)
    r = rng(seed)
    gap = d2 - d1
    dome = np.clip(gap / 4.0, 0, 1) ** 0.6
    tone = r.uniform(-0.12, 0.12, len(pts))[idx]
    h = dome * 0.85 + tone + (value_noise(S, 6, seed + 2) - 0.5) * 0.18
    lit = h + shift_light(h, 1, 1) * 1.2
    f = normalize(lit)
    f = np.where(gap < 0.9, 0.02, f)                                          # mortar: deep violet shade
    pal = R(base, n=7, lo=-0.30, hi=0.10, shadow_shift=0.5)
    img, idx2 = quantize(f, pal)
    img, _ = clean_isolated(idx2, pal)
    return img


def stone_bricks(base=(124, 122, 124), seed=9):
    y, x = np.mgrid[0:S, 0:S]
    bh, bw = 8, 16
    row = y // bh
    xs = (x + (row % 2) * (bw // 2)) % S
    u, v = xs % bw, y % bh
    h = 0.55 + (value_noise(S, 4, seed, 3) - 0.5) * 0.35
    r = rng(seed)
    bt = r.uniform(-0.08, 0.08, (S // bh, S // bw + 1))
    h += bt[row % bt.shape[0], (xs // bw) % bt.shape[1]]
    h = np.where((v == 0) | (u == 0), h + 0.22, h)
    h = np.where((v == bh - 1) | (u == bw - 1), 0.02, h)
    pal = R(base, n=6, lo=-0.28, hi=0.10, shadow_shift=0.45)
    img, idx = quantize(np.clip(h, 0, 1), pal)
    return img


# ------------------------------------------------------------------ glass: a clean pane, light on the frame
def glass(tint=None, seed=10):
    y, x = np.mgrid[0:S, 0:S]
    a = np.zeros((S, S))
    rgb = np.zeros((S, S, 3))
    frame = (x == 0) | (y == 0) | (x == S - 1) | (y == S - 1)
    inner = (x == 1) | (y == 1)
    if tint is None:
        base = np.array([214, 226, 236])
        rgb[:] = base
        a[:] = 0
        a[frame] = 200
        a[inner & ~frame] = 70
        streak = (np.abs((x - y) - 6) < 1) & (x > 3) & (x < 13)
        streak2 = (np.abs((x - y) - 10) < 1) & (x > 8) & (x < 14)
        a[streak | streak2] = 95
        rgb[frame & ((x == 0) | (y == 0))] = [246, 244, 240]
    else:
        pal = R(tint, n=4, lo=-0.12, hi=0.10)
        rgb[:] = pal[2]
        a[:] = 92
        a[frame] = 225
        rgb[frame] = pal[1]
        rgb[frame & ((x == 0) | (y == 0))] = pal[3]
        a[inner & ~frame] = 140
        rgb[inner & ~frame] = pal[3]
        streak = (np.abs((x - y) - 6) < 1) & (x > 3) & (x < 13)
        rgb[streak] = pal[3]
        a[streak] = 150
    return rgb.astype(np.uint8), a


# ------------------------------------------------------------------ sand / sandstone
def sand(base=(222, 206, 160), seed=11):
    """fine grains: a calm body, single bright quartz grains and a few dark ones, faint ripples"""
    y, x = np.mgrid[0:S, 0:S].astype(float)
    rip = np.sin((x * 0.35 + y + (value_noise(S, 2, seed, 2) - 0.5) * 6) / S * 2 * np.pi * 3) * 0.5 + 0.5
    r = rng(seed)
    g = r.random((S, S))
    f = 0.48 + (rip - 0.5) * 0.12 + (value_noise(S, 16, seed + 1) - 0.5) * 0.16
    f = np.where(g < 0.07, f + 0.22, f)
    f = np.where(g > 0.95, f - 0.22, f)
    pal = R(base, n=5, lo=-0.12, hi=0.05, shadow_shift=0.15)
    img, idx = quantize(np.clip(f, 0, 1), pal)
    return img


def sandstone(base=(218, 202, 150), seed=12):
    y, x = np.mgrid[0:S, 0:S].astype(float)
    bands = np.sin((y + (value_noise(S, 2, seed, 2) - 0.5) * 3) / S * 2 * np.pi * 4) * 0.5 + 0.5
    f = 0.45 + (bands - 0.5) * 0.35 + (value_noise(S, 6, seed + 1) - 0.5) * 0.2
    f = np.where(y % 8 == 7, f - 0.30, f)
    f = np.where(y % 8 == 0, f + 0.15, f)
    pal = R(base, n=6, lo=-0.20, hi=0.06, shadow_shift=0.35)
    img, idx = quantize(np.clip(f, 0, 1), pal)
    return img


# ------------------------------------------------------------------ soil and plants
def dirt(base=(134, 96, 66), seed=13):
    """loam: soft clods (rounded, lit on top) with small pale pebbles and dark root-holes"""
    pts = poisson_points(S, 20, 5.2, seed)
    d1, d2, idx = voronoi(S, pts)
    r = rng(seed)
    clod = np.clip((d2 - d1) / 2.2, 0, 1) ** 0.45
    h = clod * 0.35 + r.uniform(-0.1, 0.1, len(pts))[idx]
    lit = h + shift_light(h, 0, 1) * 0.8
    f = 0.25 + normalize(lit) * 0.55
    g = r.random((S, S))
    f = np.where(g < 0.025, 0.95, f)
    f = np.where(g > 0.985, 0.02, f)
    pal = R(base, n=6, lo=-0.22, hi=0.10, shadow_shift=0.3)
    img, idx2 = quantize(np.clip(f, 0, 1), pal)
    img, _ = clean_isolated(idx2, pal)
    return img


def _blades(seed, density=0.9):
    """grayscale blade field for tinted grass: short upright strokes, lit tips, dark roots"""
    r = rng(seed)
    f = np.full((S, S), 0.42) + (value_noise(S, 4, seed, 2) - 0.5) * 0.18
    n = int(S * S * density / 6)
    for _ in range(n):
        x, y = r.integers(0, S), r.integers(0, S)
        L = int(r.integers(2, 5))
        lean = 1 if r.random() < 0.3 else 0
        for k in range(L):
            t = k / max(L - 1, 1)
            f[(y - k) % S, (x + (lean if k == L - 1 else 0)) % S] = 0.40 + 0.40 * t
        f[(y + 1) % S, x] = min(f[(y + 1) % S, x], 0.26)
    return np.clip(f, 0, 1)


def grass_top(seed=14):
    f = _blades(seed)
    pal = [(96, 96, 96), (118, 118, 118), (138, 138, 138), (156, 156, 156), (174, 174, 174), (194, 194, 194)]
    img, idx = quantize(f, pal)
    return img


def grass_side(seed=15):
    """dirt with a grass lip: tint mask in alpha (255 = grass, tinted by the biome; 0 = dirt), like vanilla"""
    d = dirt(seed=seed + 1)
    g = grass_top(seed + 2)
    r = rng(seed)
    y, x = np.mgrid[0:S, 0:S]
    edge = 5 + (value_noise(S, 6, seed, 1)[0] * 4).astype(int)            # ragged lip, 5-9 px deep
    drip = r.random(S) < 0.25
    depth = edge + np.where(drip, r.integers(1, 4, S), 0)
    mask = y < depth[x]
    img = np.where(mask[..., None], g, d)
    # the dirt right under the lip sits in the grass's shadow
    under = (y >= depth[x]) & (y < depth[x] + 2)
    img = np.where(under[..., None], (img * 0.78).astype(np.uint8), img)
    alpha = np.where(mask, 255, 0)
    return img, alpha


def leaves(seed=16):
    """leaf clusters: rounded clumps of 3-5 px leaves, lit tops, dark interior, real holes (alpha) at the gaps"""
    pts = poisson_points(S, 22, 5.2, seed)
    d1, d2, idx = voronoi(S, pts)
    r = rng(seed)
    leaf = np.clip(1 - d1 / 3.2, 0, 1)
    h = leaf + r.uniform(-0.15, 0.15, len(pts))[idx]
    lit = h + shift_light(h, 0, 1) * 0.9
    f = normalize(lit)
    hole = (d1 > 3.3) & ((d2 - d1) < 1.2)
    pal = [(46, 46, 46), (74, 74, 74), (102, 102, 102), (130, 130, 130), (160, 160, 160), (190, 190, 190)]
    img, idx2 = quantize(f, pal)
    alpha = np.where(hole, 0, 255)
    return img, alpha


def log_side(bark=(104, 80, 52), seed=17, birch=False):
    """bark: vertical plates split by deep fissures (lit lip on one side), plate tone varies"""
    r = rng(seed)
    y, x = np.mgrid[0:S, 0:S].astype(float)
    if birch:
        f = np.full((S, S), 0.80) + (value_noise(S, 4, seed, 2) - 0.5) * 0.10
        for _ in range(9):                                   # dark lenticels: short horizontal dashes
            cx, cy, L = r.integers(0, S), r.integers(0, S), r.integers(2, 6)
            for k in range(L):
                f[cy, (cx + k) % S] = 0.08 if k not in (0, L - 1) else 0.30
        pal = R((214, 210, 196), n=6, lo=-0.55, hi=0.06, shadow_shift=0.25)
    else:
        warp = (value_noise(S, 3, seed, 2) - 0.5) * 3.0
        cols = np.sort(r.choice(np.arange(S), 6, replace=False)).astype(float)
        xx = (x + warp) % S
        d = np.min(np.stack([np.minimum(np.abs(xx - c), S - np.abs(xx - c)) for c in cols]), 0)
        fis = d < 0.75
        plate = np.clip(d / 3.0, 0, 1)
        f = 0.35 + plate * 0.35 + (value_noise(S, 6, seed + 3) - 0.5) * 0.15
        f = np.where(fis, 0.04, f)
        lip = np.roll(fis, -1, 1) & ~fis
        f = np.where(lip, f + 0.22, f)
        pal = R(bark, n=6, lo=-0.24, hi=0.10, shadow_shift=0.3)
    img, idx = quantize(np.clip(f, 0, 1), pal)
    return img


def log_top(wood=(176, 140, 90), bark=(104, 80, 52), seed=18):
    y, x = np.mgrid[0:S, 0:S].astype(float) + 0.5
    cx, cy = S / 2 + 0.7, S / 2 - 0.4
    rr = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) + (value_noise(S, 3, seed, 2) - 0.5) * 1.6
    rings = (np.sin(rr * 1.35) * 0.5 + 0.5)
    f = 0.45 + rings * 0.25 - rr / S * 0.12
    f = np.where(rr < 1.3, 0.20, f)
    bark_band = (x < 2) | (y < 2) | (x > S - 2) | (y > S - 2)
    pal = R(wood, n=6, lo=-0.20, hi=0.08, shadow_shift=0.3)
    img, idx = quantize(np.clip(f, 0, 1), pal)
    bk = np.array(R(bark, n=4, lo=-0.2, hi=0.04))
    bf = ((value_noise(S, 6, seed + 2) * 3.99).astype(int))
    img = np.where(bark_band[..., None], bk[bf], img)
    return img


PLANKS = {"oak": (176, 132, 82), "spruce": (120, 86, 54), "birch": (206, 186, 130), "jungle": (170, 118, 84),
          "acacia": (178, 98, 56), "big_oak": (82, 58, 36)}
LOGS = {"oak": ((104, 80, 52), (176, 140, 90)), "spruce": ((64, 46, 30), (120, 88, 56)),
        "birch": ((214, 210, 196), (200, 180, 126)), "jungle": ((92, 72, 36), (172, 128, 86)),
        "acacia": ((104, 96, 88), (176, 96, 56)), "big_oak": ((58, 42, 26), (90, 66, 40))}


# ------------------------------------------------------------------ equipment palettes (dark -> light)
EQUIP_PAL = {
    "steel": [(48, 54, 72), (78, 88, 108), (114, 124, 142), (152, 162, 176), (194, 202, 212), (238, 240, 240)],
    "silver": [(62, 66, 84), (104, 110, 130), (150, 156, 172), (192, 198, 210), (226, 230, 236), (252, 250, 246)],
    "gold": [(112, 62, 22), (164, 104, 30), (206, 150, 44), (232, 190, 74), (246, 222, 128), (255, 244, 196)],
    "netherite": [(26, 22, 28), (40, 34, 42), (56, 48, 58), (74, 64, 76), (100, 88, 100), (150, 132, 140)],
    "wood": R((168, 128, 80), n=6, lo=-0.24, hi=0.10),
    "leather": R((92, 58, 40), n=5, lo=-0.12, hi=0.08),
    "cord": R((176, 150, 104), n=5, lo=-0.18, hi=0.06),
    "flint": [(46, 44, 52), (70, 68, 76), (96, 94, 100), (122, 120, 124), (150, 148, 150), (196, 194, 192)],
    "crystal": [(20, 92, 112), (34, 150, 166), (64, 200, 206), (118, 228, 226), (186, 246, 240), (240, 255, 252)],
    "gem": [(80, 10, 30), (140, 24, 52), (196, 48, 76), (232, 104, 122), (252, 190, 196)],
}


def equipment(pack):
    import equip
    equip.export(pack, EQUIP_PAL)
