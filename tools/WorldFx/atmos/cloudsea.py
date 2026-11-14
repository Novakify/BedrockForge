"""Cloud Sea atmosphere: block textures (by Novak).

Light: low warm sun, violet sky fill (docs/WORLDFX_ART_BIBLE.md). Ramps lean violet in shade, warm in light.

Every texture is DESIGNED on a 32-unit grid (the structure: stitches, boards, stones, facets) and PAINTED natively at
RES (128 px): shapes are evaluated in continuous design coordinates, so lines stay one or two real pixels thin and
edges stay sharp, and each material gets its own fine detail (yarn fibres, wood grain, grit, pores) at full
resolution. Nothing is upscaled. Each painter returns an RGB(A) uint8 array; build_pack.py names them and adds the
material ids.
"""
from __future__ import annotations

import numpy as np

from tex import normalize, poisson_points, quantize, clean_isolated, ramp, rng, value_noise

S = 32            # design grid units per block
RES = 128         # painted pixels per block
K = RES / S       # pixels per design unit
LW = 0.42         # a fine line, in design units (~1.7 px at 128)
SHADOW_HUE, LIGHT_HUE = 292.0, 62.0
BASE = "faithful"          # hand-painted base art (graded); the painters below stay for later atmospheres / tests
GRADE = {"shadow_amt": 0.032, "light_amt": 0.016, "contrast": 0.08}

# Hive team colours stay instantly readable: vanilla hues, tuned values (no neon, no mud)
DYES = {
    "white": (232, 228, 222), "orange": (232, 120, 34), "magenta": (190, 72, 176), "light_blue": (70, 170, 214),
    "yellow": (240, 196, 52), "lime": (118, 184, 38), "pink": (234, 140, 170), "gray": (72, 74, 82),
    "silver": (148, 146, 142), "cyan": (26, 138, 148), "purple": (122, 48, 172), "blue": (56, 66, 162),
    "brown": (118, 76, 46), "green": (86, 112, 34), "red": (168, 44, 40), "black": (30, 28, 36),
}


def R(base, n=6, **kw):
    """the ramp for a material; at RES the ramp gets about twice the steps (smooth form, still no gradients)"""
    kw.setdefault("shadow_hue", SHADOW_HUE)
    kw.setdefault("light_hue", LIGHT_HUE)
    return ramp(base, n=max(n, int(n * 1.8)), **kw)


def grid():
    """(y, x) in design units at every painted pixel centre"""
    y, x = np.mgrid[0:RES, 0:RES].astype(float)
    return (y + 0.5) / K, (x + 0.5) / K


def vn(cells, seed, octaves=1, persist=0.5):
    return value_noise(RES, cells, seed, octaves, persist)


def rnd(r):
    return r.random((RES, RES))


def vor(pts):
    """tileable Voronoi at RES for points given in design units; distances returned in design units"""
    p = np.asarray(pts, float)
    y, x = grid()
    b1 = np.full((RES, RES), 1e9)
    b2 = np.full((RES, RES), 1e9)
    idx = np.zeros((RES, RES), int)
    for i, (px, py) in enumerate(p):
        dx = np.abs(x - px); dx = np.minimum(dx, S - dx)
        dy = np.abs(y - py); dy = np.minimum(dy, S - dy)
        d = np.sqrt(dx * dx + dy * dy)
        c = d < b1
        b2 = np.where(c, b1, np.minimum(b2, d))
        idx = np.where(c, i, idx)
        b1 = np.where(c, d, b1)
    return b1, b2, idx


def emboss(h, d=1.0):
    """baked light from the top-left: h minus h shifted toward the light by d design units (tileable)"""
    s = max(1, int(round(d * K)))
    return h - np.roll(np.roll(h, s, 0), s, 1)


def Q(f, pal, clean=True):
    img, idx = quantize(np.clip(f, 0, 1), pal)
    if clean:
        img, _ = clean_isolated(idx, pal)
    return img


def _ss(fn, k=3):
    """supersample a shape function f(x, y) in design units -> RES x RES"""
    o = (np.arange(k) + 0.5) / k
    y, x = np.mgrid[0:RES, 0:RES].astype(float)
    acc = np.zeros((RES, RES))
    for oy in o:
        for ox in o:
            acc += fn((x + ox) / K, (y + oy) / K)
    return acc / (k * k)


# ------------------------------------------------------------------ wool: chunky knit (stockinette chevrons), soft and matte
def wool(colour, seed=1, stitch=8.0):
    cw = ch = stitch

    def legs(x, y):
        u, v = np.mod(x, cw), np.mod(y + 2.0, ch)
        out = np.zeros_like(u)
        tw = np.zeros_like(u)
        for side in (-1.0, 1.0):
            uu = u if side < 0 else cw - u
            cx = 1.3 + v * 0.29
            d = (uu - cx) * 0.96
            body = np.clip(1.0 - (np.abs(d) / 2.25) ** 2.6, 0, 1)
            light = 0.80 + 0.20 * np.clip(-d / 2.25, -1, 1)
            tuck = 0.62 + 0.38 * np.sin(np.pi * np.clip(v / ch * 0.92 + 0.06, 0, 1))
            val = body * light * tuck
            # the ply twist: fine diagonal grooves across each leg (what makes it read as yarn up close)
            twist = 0.5 + 0.5 * np.sin((d * 2.2 + v * 1.9) * np.pi)
            tw = np.where(val > out, twist, tw)
            out = np.maximum(out, val)
        return out, tw

    y, x = grid()
    h, tw = legs(x, y)
    r = rng(seed)
    st = r.normal(0, 0.035, (int(S / ch), int(S / cw)))
    h = h + st[(y // ch).astype(int) % st.shape[0], (x // cw).astype(int) % st.shape[1]]
    h = h - (1 - tw) * 0.09 * (h > 0.3)
    f = normalize(h)
    fz = rnd(r)
    f = np.where((fz < 0.02) & (f > 0.4) & (f < 0.8), f + 0.12, f)      # stray fibres catching light
    return Q(np.clip(f, 0, 1) ** 0.75, R(colour, n=6, lo=-0.12, hi=0.05, shadow_shift=0.18))


# ------------------------------------------------------------------ concrete: dense, clean, cast panel
def concrete(colour, seed=2):
    y, x = grid()
    f = 0.48 + (vn(2, seed, 3, 0.6) - 0.5) * 0.14 + (vn(48, seed + 7) - 0.5) * 0.05   # cast clouding + fine aggregate
    f = np.where((y < LW) | (x < LW), 0.70, f)
    f = np.where((y > S - LW) | (x > S - LW), 0.28, f)
    r = rng(seed + 3)
    a = rnd(r)
    inner = (x > 1) & (y > 1) & (x < S - 1) & (y < S - 1)
    pore = (vn(40, seed + 9) < 0.08) & inner                                      # small air pores, a few px each
    f = np.where(pore, f - 0.18, f)
    f = np.where(pore & (np.roll(np.roll(pore, -1, 0), -1, 1) == 0), f + 0.10, f)
    f = np.where((a > 0.9985) & inner, f + 0.2, f)
    return Q(f, R(colour, n=5, lo=-0.09, hi=0.05, shadow_shift=0.18))


# ------------------------------------------------------------------ terracotta: fired clay, sediment lines + speckle
def terracotta(colour, seed=3):
    r = rng(seed)
    y, x = grid()
    yy = (y + (vn(2, seed) - 0.5) * 1.6) % S
    lines = np.sort(r.choice(np.arange(2, S - 1), 5, replace=False)).astype(float)
    band = np.searchsorted(lines, yy)
    tone = r.uniform(-0.06, 0.06, len(lines) + 1)
    tone[-1] = tone[0]
    f = 0.48 + tone[band] + (vn(16, seed + 1) - 0.5) * 0.06 + (vn(64, seed + 2) - 0.5) * 0.05
    for c in lines:
        f = np.where(np.abs(yy - c) < LW * 0.6, f - 0.14, f)
    sp = vn(50, seed + 5)
    f = np.where(sp < 0.05, f - 0.22, f)
    f = np.where(sp > 0.96, f + 0.16, f)
    return Q(f, R(colour, n=6, lo=-0.12, hi=0.06, shadow_shift=0.2))


# ------------------------------------------------------------------ oak planks: four boards, real grain
def planks(base=(176, 132, 82), seed=4):
    boards = 4
    bh = S / boards
    r = rng(seed)
    y, x = grid()
    f = np.zeros((RES, RES))
    for b in range(boards):
        rows = (y >= b * bh) & (y < (b + 1) * bh)
        joint = float(r.integers(6, S - 6))
        off = r.random() * 100
        kx, ky = r.uniform(4, S - 4), b * bh + r.uniform(2, bh - 2)
        dx = np.minimum(np.abs(x - kx), S - np.abs(x - kx))
        dk = np.sqrt((dx / 2.2) ** 2 + (y - ky) ** 2)
        bend = 1.6 * np.exp(-dk / 2.0)
        warp = (vn(2, seed + b, 2) - 0.5) * 2.5
        g = np.sin((y + bend + warp) * 2.2 + off) * 0.5 + 0.5                       # growth rings across the board
        fine = np.sin((y + bend + warp) * 9.0 + off * 3) * 0.5 + 0.5                # fine grain lines between them
        pores = vn(90, seed + 20 + b) < 0.12                                         # open pores, stretched along x
        tone = 0.50 + r.uniform(-0.07, 0.07)
        fb = tone + (g - 0.5) * 0.24 + (fine - 0.5) * 0.06 - pores * 0.06
        knot = np.sin(dk * 6.0) * 0.5 + 0.5
        fb = np.where(dk < 1.2, tone - 0.22 - knot * 0.1, fb)
        fb = np.where((dk >= 1.2) & (dk < 1.9), tone - 0.10, fb)
        top, bot = b * bh, (b + 1) * bh
        fb = np.where(y - top < LW, fb + 0.20, fb)
        fb = np.where(bot - y < LW * 1.2, 0.04, fb)
        jd = (x - joint) % S
        fb = np.where(rows & (jd < LW), 0.06, fb)
        fb = np.where(rows & (jd >= LW) & (jd < LW * 2.2), fb + 0.14, fb)
        nail = (np.abs(x - (joint + 1.6)) % S < 0.45) & (np.abs(y - (top + bh / 2)) < 0.45)   # a nail head by the joint
        fb = np.where(nail, 0.10, fb)
        f = np.where(rows, fb, f)
    return Q(f, R(base, n=7, lo=-0.24, hi=0.10, shadow_shift=0.25))


# ------------------------------------------------------------------ end stone: pale pumice, lavender pores
def end_stone(base=(226, 222, 168), seed=5):
    pts = poisson_points(S, 18, 5.5, seed)
    d1, d2, idx = vor(pts)
    r = rng(seed)
    radius = r.choice([0.7, 1.0, 1.5, 2.1], len(pts), p=[0.35, 0.35, 0.2, 0.1])[idx]
    pit = d1 < radius
    body = 0.62 + (vn(3, seed + 1, 2) - 0.5) * 0.18 + (vn(70, seed + 3) - 0.5) * 0.08
    micro = vn(48, seed + 4) < 0.07                                                 # tiny pinholes everywhere
    f = np.where(pit, 0.10, body)
    lit_wall = pit & (np.roll(np.roll(~pit, int(K), 0), int(K), 1))
    f = np.where(lit_wall, 0.26, f)
    rim = ~pit & np.roll(np.roll(pit, -int(K * 0.6), 0), -int(K * 0.6), 1)
    f = np.where(rim, f + 0.14, f)
    f = np.where(micro & ~pit, f - 0.2, f)
    pal = [(96, 92, 92), (116, 112, 106), (134, 130, 118), (156, 152, 128), (178, 174, 140), (194, 190, 150),
           (206, 202, 158), (216, 212, 165), (226, 222, 172), (234, 230, 182), (242, 238, 194)]
    return Q(f, pal)


# ------------------------------------------------------------------ obsidian: black glass, conchoidal facets
def obsidian(seed=6):
    pts = poisson_points(S, 6, 11.0, seed)
    d1, d2, idx = vor(pts)
    r = rng(seed)
    y, x = grid()
    n = len(pts)
    ang = r.uniform(0, 2 * np.pi, n)
    ca, sa = np.cos(ang)[idx], np.sin(ang)[idx]
    cx = np.array([p[0] for p in pts])[idx]; cy = np.array([p[1] for p in pts])[idx]
    ddx = (x - cx + S / 2) % S - S / 2; ddy = (y - cy + S / 2) % S - S / 2
    grad = (ddx * ca + ddy * sa) / 12.0
    ripple = np.sin(np.sqrt(ddx ** 2 + ddy ** 2) * 2.4) * 0.5 + 0.5               # conchoidal ripple rings
    f = r.uniform(0.18, 0.36, n)[idx] + grad * 0.14 + (ripple - 0.5) * 0.06
    f = np.where((d2 - d1) < 0.35, f - 0.10, f)
    facing = r.random(n) < 0.5
    for i in range(n):
        if not facing[i]:
            continue
        px, py = pts[i]
        L = r.uniform(2.0, 4.0)
        t = ((x - (px - 2)) + (y - (py - 2))) / 2
        dist = np.abs((x - (px - 2)) - (y - (py - 2))) / 1.414
        streak = (dist < 0.22) & (t > 0) & (t < L)
        f = np.where(streak, 0.80 + 0.15 * np.sin(np.pi * np.clip(t / L, 0, 1)), f)
    sp = vn(60, seed + 2) > 0.985
    f = np.where(sp, 0.62, f)
    pal = [(10, 8, 16), (14, 10, 22), (19, 13, 31), (24, 16, 38), (30, 20, 47), (38, 26, 60), (48, 32, 76),
           (64, 42, 100), (90, 66, 140), (130, 104, 184), (196, 178, 236)]
    return Q(f, pal, clean=False)


# ------------------------------------------------------------------ stone family
def stone(base=(128, 126, 128), seed=7):
    pts = poisson_points(S, 6, 11.0, seed)
    d1, d2, idx = vor(pts)
    r = rng(seed)
    n = len(pts)
    f = r.uniform(0.49, 0.56, n)[idx] + (vn(4, seed + 1, 2) - 0.5) * 0.20 + (vn(64, seed + 5, 2) - 0.5) * 0.10
    y, x = grid()
    crack = np.zeros((RES, RES), bool)
    for _ in range(3):
        cx0, cy0 = r.uniform(0, S), r.uniform(0, S)
        L = r.uniform(3, 6)
        t = (y - cy0) % S
        crack |= (t < L) & (np.abs(((x - cx0) % S) - t * 0.45 - 0.2 * np.sin(t * 2)) < 0.22)
    f = np.where(crack, 0.08, f)
    f = np.where(np.roll(np.roll(crack, int(K * 0.5), 0), int(K * 0.5), 1) & ~crack, f + 0.12, f)
    fl = vn(70, seed + 8)
    f = np.where(fl > 0.97, f + 0.16, f)
    f = np.where(fl < 0.03, f - 0.14, f)
    return Q(f, R(base, n=6, lo=-0.24, hi=0.09, shadow_shift=0.4))


def cobblestone(base=(126, 122, 120), seed=8):
    pts = poisson_points(S, 11, 8.2, seed)
    d1, d2, idx = vor(pts)
    r = rng(seed)
    gap = d2 - d1
    dome = np.clip(gap / 4.0, 0, 1) ** 0.6
    tone = r.uniform(-0.12, 0.12, len(pts))[idx]
    h = dome * 0.85 + tone + (vn(6, seed + 2) - 0.5) * 0.18 + (vn(56, seed + 3, 2) - 0.5) * 0.10
    lit = h + emboss(h, 1.0) * 1.2
    f = normalize(lit)
    f = np.where(gap < 0.8, 0.02, f)
    f = np.where((gap >= 0.8) & (gap < 1.2), f * 0.7, f)
    return Q(f, R(base, n=7, lo=-0.30, hi=0.10, shadow_shift=0.5))


def stone_bricks(base=(124, 122, 124), seed=9):
    y, x = grid()
    bh, bw = 8.0, 16.0
    row = (y // bh).astype(int)
    xs = (x + (row % 2) * (bw / 2)) % S
    u, v = xs % bw, y % bh
    h = 0.55 + (vn(4, seed, 3) - 0.5) * 0.30 + (vn(60, seed + 1, 2) - 0.5) * 0.10
    r = rng(seed)
    bt = r.uniform(-0.08, 0.08, (int(S // bh), int(S // bw) + 1))
    h += bt[row % bt.shape[0], (xs // bw).astype(int) % bt.shape[1]]
    chip = vn(24, seed + 4) < 0.06
    h = np.where(chip, h - 0.12, h)
    h = np.where((v < LW * 1.6) | (u < LW * 1.6), h + 0.20, h)
    h = np.where((v > bh - LW * 2) | (u > bw - LW * 2), 0.02, h)
    return Q(h, R(base, n=6, lo=-0.28, hi=0.10, shadow_shift=0.45))


# ------------------------------------------------------------------ glass: a clean pane, light on the frame
def glass(tint=None, seed=10):
    y, x = grid()
    frame = (x < 1) | (y < 1) | (x > S - 1) | (y > S - 1)
    lit_frame = frame & ((x < 1) | (y < 1))
    inner = ((x < 1.6) | (y < 1.6)) & ~frame
    d = (x - y)
    streak = ((np.abs(d - 6) < 0.35) & (x > 3) & (x < 13)) | ((np.abs(d - 9.5) < 0.22) & (x > 8) & (x < 14))
    a = np.zeros((RES, RES))
    rgb = np.zeros((RES, RES, 3))
    if tint is None:
        rgb[:] = [214, 226, 236]
        a[frame] = 205
        a[inner] = 70
        a[streak] = 110
        rgb[lit_frame] = [248, 246, 240]
        rgb[streak] = [250, 250, 248]
    else:
        pal = ramp(tint, n=4, lo=-0.12, hi=0.10, shadow_hue=SHADOW_HUE, light_hue=LIGHT_HUE)
        rgb[:] = pal[2]
        a[:] = 92
        a[frame] = 228
        rgb[frame] = pal[1]
        rgb[lit_frame] = pal[3]
        a[inner] = 140
        rgb[inner] = pal[3]
        rgb[streak] = pal[3]
        a[streak] = 160
    return rgb.astype(np.uint8), a


# ------------------------------------------------------------------ sand / sandstone
def sand(base=(222, 206, 160), seed=11):
    y, x = grid()
    rip = np.sin((x * 0.35 + y + (vn(2, seed, 2) - 0.5) * 6) / S * 2 * np.pi * 3) * 0.5 + 0.5
    g = vn(90, seed + 2)
    f = 0.48 + (rip - 0.5) * 0.12 + (vn(16, seed + 1) - 0.5) * 0.12
    f = np.where(g > 0.88, f + 0.20, f)                                              # bright quartz grains
    f = np.where(g < 0.07, f - 0.20, f)
    return Q(f, R(base, n=5, lo=-0.12, hi=0.05, shadow_shift=0.15))


def sandstone(base=(218, 202, 150), seed=12):
    y, x = grid()
    bands = np.sin((y + (vn(2, seed, 2) - 0.5) * 3) / S * 2 * np.pi * 4) * 0.5 + 0.5
    f = 0.45 + (bands - 0.5) * 0.30 + (vn(6, seed + 1) - 0.5) * 0.16 + (vn(80, seed + 3) - 0.5) * 0.08
    f = np.where((y % 8) > 8 - LW * 1.4, f - 0.30, f)
    f = np.where((y % 8) < LW, f + 0.15, f)
    return Q(f, R(base, n=6, lo=-0.20, hi=0.06, shadow_shift=0.35))


# ------------------------------------------------------------------ soil and plants
def dirt(base=(134, 96, 66), seed=13):
    pts = poisson_points(S, 20, 5.2, seed)
    d1, d2, idx = vor(pts)
    r = rng(seed)
    clod = np.clip((d2 - d1) / 2.2, 0, 1) ** 0.45
    h = clod * 0.35 + r.uniform(-0.1, 0.1, len(pts))[idx] + (vn(64, seed + 2, 2) - 0.5) * 0.12
    lit = h + emboss(h, 1.0) * 0.8
    f = 0.25 + normalize(lit) * 0.55
    g = vn(60, seed + 5)
    f = np.where(g > 0.95, 0.92, f)                                                   # pale pebbles
    f = np.where(g < 0.04, 0.03, f)                                                   # root holes
    return Q(f, R(base, n=6, lo=-0.22, hi=0.10, shadow_shift=0.3))


def _blades(seed, count=360):
    """grayscale grass for biome tint: long thin blades (1-2 px wide at 128), lit tips, dark roots, a few leaning"""
    r = rng(seed)
    f = 0.40 + (vn(4, seed, 2) - 0.5) * 0.14 + (vn(64, seed + 1) - 0.5) * 0.06
    y, x = grid()
    for _ in range(count):
        bx, by = r.uniform(0, S), r.uniform(0, S)
        L = r.uniform(2.0, 4.5)
        lean = r.uniform(-0.35, 0.35)
        t = (by - y) % S
        on = (t < L) & (np.abs(((x - bx - lean * t + S / 2) % S) - S / 2) < 0.20 + 0.1 * (1 - t / L))
        f = np.where(on, 0.36 + 0.5 * (t / L), f)
    return np.clip(f, 0, 1)


def grass_top(seed=14):
    pal = [(int(v), int(v), int(v)) for v in np.linspace(92, 200, 10)]
    img, idx = quantize(_blades(seed), pal)
    return img


def grass_side(seed=15):
    d = dirt(seed=seed + 1)
    g = grass_top(seed + 2)
    r = rng(seed)
    y, x = grid()
    edge = 5 + vn(6, seed)[0] * 4
    drip = vn(10, seed + 3)[0]
    depth = edge + np.where(drip > 0.7, (drip - 0.7) * 10, 0)
    col = depth[(x * K).astype(int).clip(0, RES - 1)]
    mask = y < col
    img = np.where(mask[..., None], g, d)
    under = (y >= col) & (y < col + 1.5)
    img = np.where(under[..., None], (img * 0.78).astype(np.uint8), img)
    alpha = np.where(mask, 255, 0)
    return img, alpha


def leaves(seed=16):
    r = rng(seed)
    f = np.full((RES, RES), -1.0)
    y, x = grid()
    n = 170
    for k in range(n):
        depth = k / n
        cx, cy = r.random() * S, r.random() * S
        ang = r.uniform(0, np.pi)
        a_, b_ = r.uniform(1.6, 2.4), r.uniform(0.9, 1.3)
        dx = (x - cx + S / 2) % S - S / 2
        dy = (y - cy + S / 2) % S - S / 2
        u = dx * np.cos(ang) + dy * np.sin(ang)
        v = -dx * np.sin(ang) + dy * np.cos(ang)
        e = (u / a_) ** 2 + (v / b_) ** 2
        inside = e < 1.0
        shade = 0.25 + depth * 0.45 + np.where(v < -0.25, 0.14, 0.0) - np.where(v > 0.45, 0.10, 0.0)
        vein = inside & (np.abs(v) < 0.12) & (np.abs(u) < a_ * 0.75)
        edge = inside & (e > 0.72)
        f = np.where(inside, shade, f)
        f = np.where(edge, shade - 0.07, f)
        f = np.where(vein, shade + 0.06, f)
    hole = f < 0
    f = np.where(hole, 0.0, f)
    pal = [(int(v), int(v), int(v)) for v in np.linspace(46, 190, 11)]
    img, idx = quantize(np.clip(f, 0, 1), pal)
    alpha = np.where(hole, 0, 255)
    return img, alpha


def log_side(bark=(104, 80, 52), seed=17, birch=False):
    r = rng(seed)
    y, x = grid()
    if birch:
        f = 0.80 + (vn(4, seed, 2) - 0.5) * 0.08 + (vn(70, seed + 1) - 0.5) * 0.05
        for _ in range(10):
            cx, cy, L = r.uniform(0, S), r.uniform(0, S), r.uniform(2, 6)
            t = (x - cx) % S
            on = (t < L) & (np.abs(y - cy) < 0.32)
            f = np.where(on, 0.10 + 0.25 * (np.abs(t - L / 2) > L / 2 - 0.6), f)
        papery = vn(30, seed + 4) > 0.92                                              # peeling curls
        f = np.where(papery, f + 0.10, f)
        pal = R((214, 210, 196), n=6, lo=-0.55, hi=0.06, shadow_shift=0.25)
    else:
        warp = (vn(3, seed, 2) - 0.5) * 3.0
        cols = np.sort(r.choice(np.arange(S), 6, replace=False)).astype(float)
        xx = (x + warp) % S
        d = np.min(np.stack([np.minimum(np.abs(xx - c), S - np.abs(xx - c)) for c in cols]), 0)
        fis = d < 0.55
        plate = np.clip(d / 3.0, 0, 1)
        ridges = np.sin((y + warp * 2) * 3.0) * 0.5 + 0.5
        f = 0.35 + plate * 0.35 + (vn(6, seed + 3) - 0.5) * 0.12 + (ridges - 0.5) * 0.06 + (vn(80, seed + 6) - 0.5) * 0.08
        f = np.where(fis, 0.04, f)
        lip = (d >= 0.55) & (d < 0.95) & (((xx - 0.7) % S) > 0)
        f = np.where(lip, f + 0.16, f)
        pal = R(bark, n=6, lo=-0.24, hi=0.10, shadow_shift=0.3)
    return Q(f, pal)


def log_top(wood=(176, 140, 90), bark=(104, 80, 52), seed=18):
    y, x = grid()
    cx, cy = S / 2 + 0.7, S / 2 - 0.4
    rr = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) + (vn(3, seed, 2) - 0.5) * 1.6
    rings = np.sin(rr * 1.35) * 0.5 + 0.5
    fine = np.sin(rr * 5.2) * 0.5 + 0.5
    rays = np.sin(np.arctan2(y - cy, x - cx) * 23) > 0.96                            # medullary rays
    f = 0.45 + rings * 0.22 + (fine - 0.5) * 0.05 - rr / S * 0.12 - rays * 0.05
    checks = (np.abs(np.sin(np.arctan2(y - cy, x - cx) * 3 + 1.1)) < 0.03) & (rr > 3) & (rr < 11)   # drying cracks
    f = np.where(checks, 0.08, f)
    f = np.where(rr < 1.2, 0.20, f)
    bark_band = (x < 1.6) | (y < 1.6) | (x > S - 1.6) | (y > S - 1.6)
    img = Q(f, R(wood, n=6, lo=-0.20, hi=0.08, shadow_shift=0.3))
    bk = np.array(R(bark, n=4, lo=-0.2, hi=0.04))
    bf = (vn(24, seed + 2) * (len(bk) - 0.01)).astype(int)
    return np.where(bark_band[..., None], bk[bf], img)


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
    "wood": ramp((168, 128, 80), n=6, lo=-0.24, hi=0.10, shadow_hue=SHADOW_HUE, light_hue=LIGHT_HUE),
    "leather": ramp((92, 58, 40), n=5, lo=-0.12, hi=0.08, shadow_hue=SHADOW_HUE, light_hue=LIGHT_HUE),
    "cord": ramp((176, 150, 104), n=5, lo=-0.18, hi=0.06, shadow_hue=SHADOW_HUE, light_hue=LIGHT_HUE),
    "flint": [(46, 44, 52), (70, 68, 76), (96, 94, 100), (122, 120, 124), (150, 148, 150), (196, 194, 192)],
    "crystal": [(20, 92, 112), (34, 150, 166), (64, 200, 206), (118, 228, 226), (186, 246, 240), (240, 255, 252)],
    "gem": [(80, 10, 30), (140, 24, 52), (196, 48, 76), (232, 104, 122), (252, 190, 196)],
}


def equipment(pack):
    import equip
    import particles
    equip.export(pack, EQUIP_PAL)
    particles.export(pack)
    _faithful_armor(pack)


def _faithful_armor(pack):
    """Faithful 32x armor, graded, with the World FX material id in alpha (metal / crystal) for the actor shader;
    leather keeps its dye mask in alpha."""
    from pathlib import Path
    from PIL import Image
    import grade
    src = Path("/tmp/fb/textures/models/armor")
    if not src.exists():
        return
    dst = pack / "textures" / "models" / "armor"
    dst.mkdir(parents=True, exist_ok=True)
    ids = {"iron": 12, "gold": 12, "netherite": 12, "chain": 12, "diamond": 13}
    for f in sorted(src.glob("*")):
        if f.suffix.lower() not in (".png", ".tga") or f.stem.endswith(("_mers", "_normal")):
            continue
        if f.stem.startswith("chain"):   # Faithful's chain is a solid checker (no holes): Glass Armor can't tell it from leather
            continue                     # by shape or colour, so the game keeps vanilla chain (seen in game 2026-11-11)
        im = np.array(Image.open(f).convert("RGBA"))
        rgb = grade.grade(im[..., :3], **GRADE)
        al = im[..., 3].copy()
        mid = ids.get(f.stem.split("_")[0], 0)
        if mid and not f.stem.startswith("leather"):
            al = np.where(al > 0, 255 - 4 * mid, 0).astype(np.uint8)
        Image.fromarray(np.dstack([rgb, al]), "RGBA").save(dst / f.name)


# ------------------------------------------------------------------ mineral blocks (Bed Wars generators sit on these)
def metal_block(pal, seed=40, kind="iron"):
    y, x = grid()
    streak = vn(64, seed)[:1].repeat(RES, 0)
    f = 0.48 + (streak - 0.5) * 0.22 + (vn(8, seed + 3) - 0.5) * 0.06
    if kind == "iron":
        u, v = x % 16, y % 16
        f = np.where((u < LW * 1.4) | (v < LW * 1.4), f + 0.26, f)
        f = np.where((u > 16 - LW * 1.6) | (v > 16 - LW * 1.6), 0.10, f)
        for cx, cy in ((2.5, 2.5), (13.5, 2.5), (2.5, 13.5), (13.5, 13.5)):
            d = np.sqrt((u - cx) ** 2 + (v - cy) ** 2)
            f = np.where(d < 0.75, 0.62 - (u - cx + v - cy) * 0.3, f)                 # domed rivet, lit top-left
            f = np.where((d >= 0.75) & (d < 1.0) & (u - cx + v - cy > 0), 0.15, f)
    else:
        row = (y // 8).astype(int)
        xs = (x + (row % 2) * 8) % S
        u, v = xs % 16, y % 8
        f = 0.50 + (vn(16, seed + 1) - 0.5) * 0.10 + (vn(80, seed + 2) - 0.5) * 0.05
        f = np.where((v < LW * 1.4) | (u < LW * 1.4), 0.88, f)
        f = np.where((v >= LW * 1.4) & (v < 1.4), f + 0.12, f)
        f = np.where((v > 8 - LW * 1.6) | (u > 16 - LW * 1.6), 0.12, f)
        f = np.where((v > 6.3) & (v <= 8 - LW * 1.6), f - 0.12, f)
        stamp = (np.abs(u - 8) < 2.2) & (np.abs(v - 4) < 1.1) & ~((np.abs(u - 8) < 1.6) & (np.abs(v - 4) < 0.5))
        f = np.where(stamp, f - 0.10, f)                                              # foundry stamp on each ingot
    return Q(f, ramp(pal[2], n=11, lo=-0.38, hi=0.22, shadow_hue=SHADOW_HUE, light_hue=LIGHT_HUE, shadow_shift=0.1) if False else _expand(pal), clean=False)


def _expand(pal, n=11):
    """interpolate a short hand palette into n steps (OK space would be nicer; these ramps are already smooth)"""
    p = np.array(pal, float)
    t = np.linspace(0, len(p) - 1, n)
    i = np.floor(t).astype(int).clip(0, len(p) - 2)
    w = (t - i)[:, None]
    return [tuple(int(v) for v in c) for c in (p[i] * (1 - w) + p[i + 1] * w)]


def gem_block(pal, seed=41):
    y, x = grid()
    u, v = x % 16 - 8.0, y % 16 - 8.0
    au, av = np.abs(u), np.abs(v)
    oct_ = np.maximum(au, av) + 0.45 * np.minimum(au, av)
    table = oct_ < 3.6
    ang = np.arctan2(v, u)
    sector = ((ang + np.pi) / (np.pi / 4)).astype(int) % 8
    lightness = np.array([0.42, 0.30, 0.38, 0.62, 0.84, 0.74, 0.56, 0.48])
    f = np.where(~table, lightness[sector], 0.66)
    f = f + np.where(~table, (oct_ - 3.6) / 4.4 * 0.08, 0)                         # facets darken toward the girdle
    f = np.where(table & (u + v < -1.5), 0.80, f)
    edge = np.abs(au - av) < 0.18
    f = np.where(edge & ~table, f + 0.12, f)
    tedge = np.abs(oct_ - 3.6) < 0.16
    f = np.where(tedge, f + 0.14, f)
    f = np.where(oct_ > 8.6, 0.06, f)
    f = np.where((oct_ > 7.9) & (oct_ <= 8.6), 0.22, f)
    gl = np.sqrt((u + 2.5) ** 2 + (v + 2.5) ** 2)
    f = np.where(gl < 0.7, 0.99, f)
    f = np.where((np.abs(u + 2.5) < 0.12) & (np.abs(v + 2.5) < 1.8), 0.95, f)       # a four-point star glint
    f = np.where((np.abs(v + 2.5) < 0.12) & (np.abs(u + 2.5) < 1.8), 0.95, f)
    return Q(f, _expand(pal), clean=False)


def ore(gem_pal, seed=42):
    base = stone(seed=seed)
    r = rng(seed)
    y, x = grid()
    out = base.copy()
    pal = np.array(_expand(gem_pal, 7), np.uint8)
    for _ in range(4):
        cx, cy = r.uniform(3, S - 4), r.uniform(3, S - 4)
        for _k in range(int(r.integers(3, 6))):
            px, py = cx + r.uniform(-1.4, 1.4), cy + r.uniform(-1.4, 1.4)
            s = r.uniform(0.6, 1.1)
            u, v = x - px, y - py
            gem = (np.abs(u) + np.abs(v)) < s
            lit = gem & (u + v < -s * 0.2)
            out[gem] = pal[3]
            out[lit] = pal[5]
            out[gem & (u + v > s * 0.5)] = pal[1]
            shadow = ~gem & ((np.abs(u - 0.4) + np.abs(v - 0.4)) < s) & (u + v > 0)
            out[shadow] = (out[shadow] * 0.55).astype(np.uint8)
    return out


MINERALS = {"iron": [(70, 76, 92), (110, 118, 132), (150, 158, 170), (190, 196, 204), (226, 228, 230), (248, 246, 240)],
            "gold": [(124, 70, 24), (176, 116, 32), (214, 160, 50), (236, 196, 80), (248, 226, 136), (255, 246, 200)],
            "diamond": [(16, 80, 100), (30, 136, 152), (60, 190, 198), (120, 226, 224), (196, 248, 242), (244, 255, 253)],
            "emerald": [(10, 70, 36), (20, 120, 58), (34, 170, 84), (80, 210, 120), (160, 240, 176), (232, 255, 236)]}


# ------------------------------------------------------------------ more building blocks
def quartz(seed=50):
    y, x = grid()
    w = vn(3, seed, 3)
    vein = np.abs(np.sin((x * 0.6 + y + w * 14) / S * 2 * np.pi * 1.5))
    hair = np.abs(np.sin((x * -0.4 + y * 0.9 + vn(5, seed + 4, 2) * 10) / S * 2 * np.pi * 3.0))
    f = 0.62 + (vn(4, seed + 1) - 0.5) * 0.06 + (vn(90, seed + 2) - 0.5) * 0.03
    f = np.where(vein < 0.025, 0.38, f)
    f = np.where((vein >= 0.025) & (vein < 0.06), 0.50, f)
    f = np.where(hair < 0.012, 0.52, f)
    f = np.where((y < LW) | (x < LW), 0.86, f)
    f = np.where((y > S - LW) | (x > S - LW), 0.30, f)
    return Q(f, R((232, 226, 216), n=6, lo=-0.16, hi=0.04, shadow_shift=0.4))


def bricks(base=(150, 74, 58), mortar=(170, 160, 150), seed=51, bw=16, bh=8):
    y, x = grid()
    row = (y // bh).astype(int)
    xs = (x + (row % 2) * (bw / 2)) % S
    u, v = xs % bw, y % bh
    r = rng(seed)
    tone = r.uniform(-0.1, 0.1, (S // bh, S // bw + 1))
    f = 0.52 + tone[row % tone.shape[0], (xs // bw).astype(int) % tone.shape[1]] + (vn(6, seed) - 0.5) * 0.14
    f += (vn(70, seed + 3) - 0.5) * 0.10                                            # fired-clay grit
    f = np.where((v > 1) & (v < 1.0 + LW * 1.6) & (u > 1), f + 0.16, f)
    f = np.where((v > bh - 1.6), f - 0.12, f)
    img = Q(f, R(base, n=6, lo=-0.20, hi=0.08, shadow_shift=0.3))
    mp = np.array(R(mortar, n=3, lo=-0.12, hi=0.0, shadow_shift=0.4), np.uint8)
    joint = (v < 1.0) | (u < 1.0)
    grain = (vn(60, seed + 4) * (len(mp) - 0.01)).astype(int)
    return np.where(joint[..., None], mp[grain], img)


def clay(seed=52):
    pts = poisson_points(S, 12, 7.0, seed)
    d1, d2, idx = vor(pts)
    h = np.clip(d1 / 5.0, 0, 1) + (vn(60, seed + 1) - 0.5) * 0.08
    lit = h + emboss(h, 1.0) * 1.5
    f = 0.45 + (normalize(lit) - 0.5) * 0.30
    return Q(f, R((160, 166, 180), n=5, lo=-0.12, hi=0.06, shadow_shift=0.2))


def snow(seed=53):
    h = vn(4, seed, 3) + (vn(40, seed + 2) - 0.5) * 0.1
    lit = h + emboss(h, 1.0) * 2.0
    f = 0.60 + (normalize(lit) - 0.5) * 0.30
    f = np.where(vn(80, seed + 3) > 0.97, 0.98, f)                                   # ice crystals catching light
    pal = [(176, 184, 214), (192, 198, 224), (204, 210, 232), (216, 220, 238), (226, 230, 244), (234, 236, 247),
           (240, 242, 250), (247, 248, 253), (252, 252, 255)]
    return Q(f, pal)


def purpur(seed=54):
    y, x = grid()
    u, v = x % 16, y % 16
    f = 0.50 + (vn(4, seed) - 0.5) * 0.16 + (vn(70, seed + 1) - 0.5) * 0.08
    f = np.where((u < LW * 1.5) | (v < LW * 1.5), 0.82, f)
    f = np.where((u > 16 - LW * 1.6) | (v > 16 - LW * 1.6), 0.10, f)
    f = np.where(((u >= LW * 1.5) & (u < 1.3)) | ((v >= LW * 1.5) & (v < 1.3)), f + 0.08, f)
    return Q(f, R((170, 122, 170), n=6, lo=-0.22, hi=0.08, shadow_shift=0.15))


def tnt(seed=60):
    y, x = grid()
    stick = x % 8
    f = 0.50 + np.where(stick < 0.6, -0.30, 0.0) + np.where((stick >= 0.6) & (stick < 1.6), 0.16, 0.0)
    f += np.where(stick > 6.8, -0.12, 0.0) + (np.cos((stick - 4) / 4 * np.pi / 2) - 0.7) * 0.15   # round sticks
    f += (vn(12, seed) - 0.5) * 0.08 + (vn(70, seed + 4) - 0.5) * 0.05
    red = R((182, 46, 38), n=6, lo=-0.22, hi=0.08, shadow_shift=0.25)
    img = Q(f, red)
    band = (y >= 11) & (y <= 21)
    cream = R((226, 214, 186), n=4, lo=-0.12, hi=0.04)
    bf = 0.5 + np.where(y < 11.5, 0.4, 0) + np.where(y > 20.5, -0.4, 0) + (vn(30, seed + 1) - 0.5) * 0.2
    cimg = Q(bf, cream, clean=False)
    img = np.where(band[..., None], cimg, img)
    # stencilled letters T N T
    lx = x - 7.0
    ly = y - 13.0
    T1 = ((ly < 1.2) & (lx > 0) & (lx < 5)) | ((np.abs(lx - 2.5) < 0.7) & (ly < 6))
    N = ((np.abs(lx - 7) < 0.7) | (np.abs(lx - 11) < 0.7) | (np.abs((lx - 7) - (ly * 0.66)) < 0.75)) & (lx > 6.3) & (lx < 11.7)
    T2 = ((ly < 1.2) & (lx > 13) & (lx < 18)) | ((np.abs(lx - 15.5) < 0.7) & (ly < 6))
    letters = (T1 | N | T2) & (ly >= 0) & (ly < 6)
    img = np.where(letters[..., None], np.array([46, 34, 40], np.uint8), img)
    yy, xx = y, x
    cu, cv = (xx % 8) - 4, (yy % 8) - 4
    d = np.sqrt(cu ** 2 + cv ** 2)
    ft = np.where(d < 3.4, 0.62 - d * 0.06 + (np.sin(d * 4) * 0.03), 0.12)
    ft = np.where((d < 3.4) & (cu + cv < -2), 0.85, ft)
    top = Q(ft, red, clean=False)
    fuse = (np.abs(xx - 16) < 0.9) & (np.abs(yy - 16) < 0.9)
    top = np.where(fuse[..., None], np.array([60, 52, 50], np.uint8), top)
    bottom = Q(ft * 0.8, red, clean=False)
    return img, top, bottom


def ladder(seed=61):
    y, x = grid()
    rail = ((x >= 3) & (x <= 5.5)) | ((x >= 26.5) & (x <= 29))
    rung = ((y % 7) >= 2) & ((y % 7) <= 3.6) & (x > 5.5) & (x < 26.5)
    wood = R((168, 128, 80), n=5, lo=-0.22, hi=0.08)
    grain = (vn(60, seed) - 0.5) * 0.1
    f = 0.5 + grain + np.where(rail & ((x < 3.5) | ((x >= 26.5) & (x < 27))), 0.25, 0) + np.where(rail & ((x > 5) | (x > 28.5)), -0.25, 0)
    f = np.where(rung & ((y % 7) < 2.5), 0.80, np.where(rung & ((y % 7) > 3.1), 0.20, np.where(rung, 0.45 + grain, f)))
    img = Q(f, wood, clean=False)
    alpha = np.where(rail | rung, 255, 0)
    return img, alpha
