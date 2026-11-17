#!/usr/bin/env python3
"""Stick armor tiers: every armor material gets its own design (not its material's look), ranked like the Hive
BedWars progression - the higher the tier, the more pieces, light and reaction to the game.

  tier 1  leather    RUNNER       team-dyed livery with stitching, soft white rails, a scarf. The base kit.
  tier 2  copper     CIRCUIT      verdigris plates, copper trim, glowing copper circuit traces; slow amber rails
  tier 2  chainmail  PHANTOM      graphite hex weave, silver trim, faint ice glow; one soft pulse
  tier 3  iron       VANGUARD     arctic white ceramic, twin cobalt stripes, cobalt light rails
  tier 3  golden     INFERNO      obsidian with magma veins, ember trim, flickering fire rails, flared shoulders
  tier 4  netherite  ECLIPSE      black chrome + violet Novak Glass, glowing runes, horns and a spined back;
                                  reacts to the game (hurt flash, low-health pulse, sprint)
  tier 4+ diamond    PRISM        flagship: the head is a brilliant-cut Novak Glass diamond round a sapphire core,
                                  midnight sapphire plates cut with glowing facet lines, two floating gems with light
                                  rings; reacts to the game
          turtle     REEF         (helmet only) sea-glass teal

Pieces per tier (stick_armor.build_suit_mix reads TIERS): level 1 = the base set, 2 adds the layered leaves and
first rails, 3 the full suit with every rail, 4 the accents. Only tier 4 uses Renderer Foundation's glass (its panes
bind the vanilla texture RF recognises); every other tier draws its own pane texture (512x256, too big for RF to
look at), so they never turn into glass. Glowing parts: material entity_emissive_alpha, texture alpha 0 = full glow.
"""
import math

import numpy as np
from PIL import Image

# level, motif, glass (RF), reactive, rail speed, accents, palette
TIERS = {
    # every tier = the mix (the original light-rail stick armor + glass faces) in its own livery, one tasteful extra.
    # tier 4 wears the approved mix finish itself (engraved metal + Renderer Foundation Novak Glass)
    "leather":   dict(level=1, name="RUNNER", rf=False, react=False, speed=0.5, acc=("scarf",)),
    "copper":    dict(level=2, name="VERDANT", rf=False, react=False, speed=0.7, acc=("pauldrons",)),
    "chainmail": dict(level=2, name="PHANTOM", rf=False, react=False, speed=0.6, acc=("cowl", "strips")),
    "iron":      dict(level=3, name="VANGUARD", rf=False, react=False, speed=1.0, acc=("crest", "cape", "pauldrons")),
    "golden":    dict(level=3, name="SOLAR", rf=False, react=False, speed=1.25, acc=("halfcape",)),
    "netherite": dict(level=4, name="ECLIPSE", rf=True, react=True, speed=1.15, acc=("collar", "tails"), mix=True),
    "diamond":   dict(level=4, name="PRISM", rf=True, react=True, speed=1.5, acc=("pauldrons", "splitcape"), mix=True),
    "turtle":    dict(level=1, name="REEF", rf=False, react=False, speed=0.0, acc=()),
}
TEXM = 128            # plate atlas (same layout as the metal atlas: plate | visor | rim | recess)
PANE = 8              # pane texture = 8x the 64x32 island layout


def rgb(*c):
    return np.array(c, float)


# livery (iron's recipe in each tier's colours): base, stripe, inlay light, trim, glows?
LIVERY = {
    "leather": (rgb(232, 226, 216), rgb(120, 112, 104), rgb(255, 255, 255), rgb(70, 64, 58), False),
    "copper": (rgb(60, 132, 118), rgb(212, 122, 66), rgb(255, 172, 96), rgb(26, 60, 54), True),
    "golden": (rgb(34, 28, 30), rgb(238, 184, 72), rgb(255, 204, 96), rgb(150, 108, 40), True),
}


def smooth_noise(shape, cells, seed):
    """soft value noise (a few broad blobs - never pixel noise)"""
    rng = np.random.default_rng(seed)
    g = rng.random((cells[0] + 2, cells[1] + 2))
    H, W = shape
    y = np.linspace(0, cells[0], H)[:, None] * np.ones((1, W))
    x = np.linspace(0, cells[1], W)[None, :] * np.ones((H, 1))
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int)
    fy, fx = y - y0, x - x0
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx
    b = g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def hsv(h, s, v):
    h = np.asarray(h, float) % 1.0
    k = (np.stack([h * 6 + 5, h * 6 + 3, h * 6 + 1], -1)) % 6
    return v[..., None] * (1 - s[..., None] * np.clip(np.minimum(k, 4 - k), 0, 1)) if np.ndim(v) else \
        v * (1 - s * np.clip(np.minimum(k, 4 - k), 0, 1))


def segs_dist(P, segs):
    """distance from points P (..., 2) to a list of segments ((x0, y0), (x1, y1))"""
    d = np.full(P.shape[:-1], 1e9)
    for a, b in segs:
        a, b = np.array(a, float), np.array(b, float)
        ab = b - a
        t = np.clip(((P - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
        d = np.minimum(d, np.linalg.norm(P - (a + t[..., None] * ab), axis=-1))
    return d


def cracks(seed, n=5, w=1.0, h=1.0):
    """branching magma cracks in a w x h box (segments)"""
    rng = np.random.default_rng(seed)
    segs = []
    for i in range(n):
        p = np.array([rng.uniform(0.15, 0.85) * w, rng.uniform(0.05, 0.95) * h])
        ang = rng.uniform(0, math.pi)
        for _ in range(rng.integers(3, 6)):
            ang += rng.normal(0, 0.7)
            q = p + np.array([math.cos(ang), math.sin(ang)]) * rng.uniform(0.08, 0.18) * h
            q = np.clip(q, [0.04 * w, 0.02 * h], [0.96 * w, 0.98 * h])
            segs.append((tuple(p), tuple(q)))
            if rng.random() < 0.35:   # a branch
                b = q + np.array([math.cos(ang + 1.2), math.sin(ang + 1.2)]) * 0.07 * h
                segs.append((tuple(q), tuple(np.clip(b, 0, [w, h]))))
            p = q
    return segs


def hex_lines(x, y, s):
    """distance to a hex lattice's edges (pointy-top hexes of size s), in px"""
    q = np.stack([x, y], -1) / s
    best = np.full(x.shape, 1e9)
    # three line families of a hex tiling
    for ang in (0, 60, 120):
        a = math.radians(ang)
        n = np.array([math.cos(a), math.sin(a)])
        t = q @ n
        best = np.minimum(best, np.abs(t - np.round(t)) * s)
    return best


# ====================================================================== plates (the "metal" atlas layer)
def plate_tex(mat):
    """RGBA TEXM x TEXM: plate region (u across a plate, v along it) | visor | rim | recess. alpha 0 = glows."""
    S = TEXM
    img = np.zeros((S, S, 4))
    img[..., 3] = 255
    W = S // 2
    yy, xx = np.mgrid[0:S, 0:W]
    u, v = (xx + .5) / W, (yy + .5) / S
    edge = np.minimum(u, 1 - u)
    glow = np.zeros((S, W))          # 0..1 emissive
    if mat == "turtle":
        if mat == "leather":   # neutral; the item's dye colours it
            base, dark, hi, trim = rgb(222, 216, 206), rgb(92, 86, 80), rgb(252, 250, 246), rgb(176, 170, 160)
        else:
            base, dark, hi, trim = rgb(70, 160, 140), rgb(20, 60, 56), rgb(170, 235, 215), rgb(222, 206, 160)
        cell = np.floor(v * 3)                      # three quilted cells
        cv = v * 3 - cell
        pad = 0.86 + 0.16 * np.sin(cv * math.pi) * np.cos((u - .5) * math.pi * 0.9)   # each cell padded
        weave = 1 + 0.025 * (((xx // 2) + (yy // 2)) % 2)
        col = base * (pad * weave)[..., None]
        seam = np.abs(cv - 0) < (1.5 / S * 3)
        col[seam & (edge > .1)] = dark * 0.9
        stitch = (np.abs(edge - .15) < .012) & (yy % 6 < 3)
        col[stitch] = hi
        col[(edge < .1)] = (trim * (0.75 + 2.2 * edge[edge < .1, None]))
    elif mat == "copper":   # PCB: two mirrored traces along the plate, necking in at the middle, pads at the ends
        base, dark, trim, gl = rgb(58, 128, 116), rgb(18, 52, 48), rgb(206, 116, 66), rgb(255, 172, 96)
        blot = smooth_noise((S, W), (4, 2), 7)
        col = base[None, None] * (0.90 + 0.14 * blot)[..., None] * (1.06 - 0.16 * v)[..., None]
        segs = []
        for sd in (-1, 1):
            xo, xi = .5 + sd * .24, .5 + sd * .13
            segs += [((xo, .16), (xo, .40)), ((xo, .40), (xi, .48)), ((xi, .48), (xi, .56)), ((xi, .56), (xo, .64)),
                     ((xo, .64), (xo, .86))]
        P = np.stack([u * W, v * S], -1)
        d = segs_dist(P, [((a_[0] * W, a_[1] * S), (b_[0] * W, b_[1] * S)) for a_, b_ in segs])
        tr = d < 0.9
        col[(d >= 0.9) & (d < 2.2)] = col[(d >= 0.9) & (d < 2.2)] * 0.55
        col[tr], glow[tr] = gl, 1.0
        for sd in (-1, 1):
            for vy in (.16, .86):
                dd = np.hypot(xx + .5 - (.5 + sd * .24) * W, yy + .5 - vy * S)
                ring = (dd > 1.6) & (dd < 3.2)
                col[ring], glow[ring] = gl, 1.0
                col[dd <= 1.6] = dark
        col[edge < .09] = trim * (0.8 + 2 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark * 0.6
    elif mat == "chainmail":   # lamellar: stacked graphite bands, an ice seam down the keel
        base, dark, hi, trim, gl = rgb(48, 52, 62), rgb(14, 16, 22), rgb(150, 162, 186), rgb(196, 206, 224), rgb(205, 232, 255)
        band = np.floor(v * 10)
        col = base[None, None] * (1.12 - 0.30 * v)[..., None] * (0.92 + 0.10 * np.cos((u - .5) * 3))[..., None]
        col *= np.where(band % 2 == 0, 1.0, 0.92)[..., None]
        bv = v * 10 - band
        col[(bv < 1.0 / S * 10 * 1.0) & (edge > .1)] = dark
        col[((bv >= 1.0 / S * 10) & (bv < 2.0 / S * 10)) & (edge > .1)] = hi * 0.55
        keel = (np.abs(xx + .5 - W / 2) < 0.9) & (v > .06) & (v < .94)
        col[keel], glow[keel] = gl, 0.75
        col[edge < .09] = trim * (0.7 + 2.5 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark
    elif mat in LIVERY:
        base, cob, gl, trim, glows = LIVERY[mat]
        dark, hi = trim * 0.6, np.minimum(base * 1.25 + 30, 255)
        col = base[None, None] * (1.04 - 0.12 * v)[..., None] * (0.93 + 0.08 * np.cos((u - .5) * 3))[..., None]
        if mat == "golden":   # obsidian gloss
            col += rgb(90, 80, 84) * (np.exp(-((u - .30) / .06) ** 2) * (1 - 0.6 * v) * 0.5)[..., None]
        stripe = (np.abs(u - .5) > .035) & (np.abs(u - .5) < .10)
        col[stripe] = cob
        for v0 in (0.30, 0.56, 0.80):
            r = np.round((v0 - 0.22 * np.abs(u - .5)) * S).astype(int)
            col[(yy == r) & (edge > .14)] = trim
            col[(yy == r + 1) & (edge > .14)] = hi
        inl = (edge >= .11) & (edge < .135) & (v > .03) & (v < .97)
        if glows:
            col[inl], glow[inl] = gl, 1.0
        else:   # stitching
            col[inl & (yy % 6 < 3)] = gl
        col[edge < .09] = trim * (0.9 + 2 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark * 0.6
    elif mat == "iron":
        base, dark, hi, trim, cob, gl = rgb(226, 231, 238), rgb(58, 64, 80), rgb(255, 255, 255), rgb(50, 56, 70), rgb(36, 98, 235), rgb(96, 160, 255)
        col = base[None, None] * (1.04 - 0.12 * v)[..., None] * (0.93 + 0.08 * np.cos((u - .5) * 3))[..., None]
        stripe = ((np.abs(u - .5) > .035) & (np.abs(u - .5) < .10))
        col[stripe] = cob
        for v0 in (0.30, 0.56, 0.80):   # graphite chevrons cut through the stripes
            r = np.round((v0 - 0.22 * np.abs(u - .5)) * S).astype(int)
            col[(yy == r) & (edge > .14)] = trim
            col[(yy == r + 1) & (edge > .14)] = hi
        inl = (edge >= .11) & (edge < .135) & (v > .03) & (v < .97)
        col[inl] = gl
        glow[inl] = 1.0
        col[edge < .09] = trim * (0.9 + 2 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark * 0.6
    elif mat == "golden":   # obsidian, one molten vein down the keel with tapering branches (designed, symmetric)
        base, dark, trim, gA, gB = rgb(32, 22, 24), rgb(8, 5, 6), rgb(232, 104, 34), rgb(255, 92, 18), rgb(255, 228, 140)
        gloss = np.exp(-((u - .30) / .07) ** 2) * 0.30 * (1 - 0.6 * v)
        col = base[None, None] * (1.0 + gloss)[..., None] + rgb(70, 48, 44) * gloss[..., None]
        P = np.stack([(u - .5) * W, v * S], -1)
        core = np.zeros((S, W)) + 1e9
        def taper(seg, w0, w1):
            (x0, y0), (x1, y1) = seg
            a_, b_ = np.array([x0 * W, y0 * S]), np.array([x1 * W, y1 * S])
            ab = b_ - a_
            t = np.clip(((P - a_) @ ab) / (ab @ ab), 0, 1)
            dist = np.linalg.norm(P - (a_ + t[..., None] * ab), axis=-1)
            return dist - (w0 + (w1 - w0) * t)        # <0 inside the vein
        veins = [taper(((0, .06), (0, .94)), 0.4, 0.4)]
        for vy, ln in ((.24, .30), (.48, .36), (.72, .28)):
            for sd in (-1, 1):
                veins.append(taper(((0, vy), (sd * .30, vy + ln * .55)), 1.3, 0.1))
        for q in veins:
            core = np.minimum(core, q)
        core[...] = np.minimum(core, taper(((0, .06), (0, .94)), 1.4, 1.4))
        halo = np.exp(-np.maximum(core, 0) / 2.2)
        m = edge > .11
        col[m] = col[m] * (1 - 0.85 * halo[m, None]) + gA * (0.85 * halo[m, None])
        glow[m] = np.maximum(glow[m], 0.75 * halo[m])
        hot = (core < 0) & m
        col[hot], glow[hot] = gB, 1.0
        col[edge < .09] = trim * (0.75 + 2.5 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark
    elif mat == "diamond":   # midnight sapphire, cut like a gem: glowing facet lines fan from the keel
        base = rgb(14, 26, 54)
        col = base[None, None] * (1.25 - 0.45 * v)[..., None] * (0.85 + 0.25 * np.cos((u - .5) * 3))[..., None]
        col += rgb(40, 90, 150) * (np.exp(-((u - .30) / .06) ** 2) * (1 - v) * 0.6)[..., None]   # gloss
        cy = rgb(90, 236, 255)
        P = np.stack([(u - .5) * W, v * S], -1)
        segs = [((0, .04 * S), (0, .96 * S))]
        for vy in np.linspace(.10, .90, 6):
            for sd in (-1, 1):
                segs.append(((0, vy * S), (sd * .36 * W, (vy + .07) * S)))
        d = segs_dist(P, segs)
        m = edge > .13
        halo = np.exp(-d / 1.6) * m
        col += cy * (0.35 * halo)[..., None]
        line = (d < 0.55) & m
        col[line], glow[line] = cy, 1.0
        glow[m & ~line] = np.maximum(glow[m & ~line], 0.3 * halo[m & ~line])
        inl = (edge >= .115) & (edge < .135) & (v > .03) & (v < .97)
        col[inl], glow[inl] = cy * 0.9, 1.0
        col[edge < .09] = rgb(170, 200, 235) * (0.70 + 2.6 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = rgb(6, 10, 22)
    elif mat == "netherite":
        base, dark, hi, vi = rgb(22, 20, 28), rgb(5, 4, 8), rgb(150, 132, 182), rgb(178, 84, 255)
        chrome = np.exp(-((u - .30) / .05) ** 2) * 0.9 + np.exp(-((u - .74) / .10) ** 2) * 0.25
        col = base[None, None] * (1 - 0.3 * v)[..., None] + hi * (chrome * (1 - 0.6 * v))[..., None] * 0.55
        inl = (edge >= .11) & (edge < .135) & (v > .03) & (v < .97)
        col[inl] = vi
        glow[inl] = 1.0
        for k in range(6):   # runes: short glowing bars down the keel
            v0 = 0.12 + 0.14 * k
            r = (np.abs(v - v0) < 1.2 / S) & (np.abs(u - .5) < (0.10 if k % 2 else 0.05))
            col[r] = vi
            glow[r] = 1.0
        keel = (np.abs(u - .5) < 0.5 / W) & (v > .05) & (v < .95)
        col[keel] = vi * 0.7
        glow[keel] = 0.6
        col[edge < .09] = hi * (0.45 + 3 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark
    img[:, :W, :3] = col
    img[:, :W, 3] = 255 * (1 - glow)
    # visor | rim | recess
    pal = {"leather": (rgb(30, 28, 26), rgb(240, 236, 228)), "turtle": (rgb(10, 34, 32), rgb(150, 240, 210)),
           "copper": (rgb(10, 22, 22), rgb(255, 168, 92)), "chainmail": (rgb(10, 12, 18), rgb(205, 232, 255)),
           "iron": (rgb(10, 14, 26), rgb(96, 160, 255)), "golden": (rgb(14, 6, 6), rgb(255, 140, 40)),
           "diamond": (rgb(8, 16, 28), rgb(60, 236, 255)), "netherite": (rgb(8, 4, 14), rgb(178, 84, 255))}[mat]
    x2 = slice(W, W + S // 4)
    uu = (np.mgrid[0:S, 0:S // 4][1] + .5) / (S // 4)
    vv = (np.mgrid[0:S, 0:S // 4][0] + .5) / S
    streak = np.exp(-((vv - 0.22 - uu * 0.18) / 0.04) ** 2) * 0.6
    img[:, x2, :3] = pal[0] + pal[1] * streak[..., None] * 0.5
    img[:, W + S // 4: W + 3 * S // 8, :3] = pal[1] * 0.85          # rim (also the tiers' glowing accents)
    img[:, W + S // 4: W + 3 * S // 8, 3] = 255 if mat in ("leather", "turtle", "chainmail") else 90
    if mat == "golden":   # flames: a white-gold core, orange flanks, deep red at the very edge; all glowing
        rw = S // 8
        ru = (np.arange(rw) + .5) / rw
        cdist = np.abs(ru - .5) * 2
        rv = (np.arange(S) + .5) / S
        fl = np.clip(1 - cdist[None, :] - 0.35 * rv[:, None], 0, 1)
        fc = rgb(150, 20, 6)[None, None] * (1 - fl[..., None]) + rgb(255, 120, 24) * np.clip(fl * 2, 0, 1)[..., None] \
            + rgb(0, 110, 110) * np.clip(fl * 2 - 1, 0, 1)[..., None]
        img[:, W + S // 4: W + 3 * S // 8, :3] = np.clip(fc, 0, 255)
        img[:, W + S // 4: W + 3 * S // 8, 3] = 0
    # recess region = the tier's cloth (hood, scarf, cowl, capes, crest): u across, v along
    xr = slice(W + 3 * S // 8, S)
    cw = S - (W + 3 * S // 8)
    cu = (np.mgrid[0:S, 0:cw][1] + .5) / cw
    cv = (np.mgrid[0:S, 0:cw][0] + .5) / S
    ce = np.minimum(cu, 1 - cu)
    cloth = {"leather": (rgb(150, 144, 136), rgb(236, 230, 220)), "turtle": (rgb(50, 130, 110), rgb(222, 206, 160)),
             "copper": (rgb(176, 98, 56), rgb(255, 172, 96)), "chainmail": (rgb(30, 33, 42), rgb(150, 162, 186)),
             "iron": (rgb(30, 78, 196), rgb(236, 240, 246)), "golden": (rgb(118, 18, 22), rgb(250, 190, 80)),
             "diamond": (rgb(20, 70, 140), rgb(120, 236, 255)), "netherite": (rgb(26, 20, 34), rgb(178, 84, 255))}[mat]
    fold = 0.82 + 0.18 * np.cos(cu * math.pi * 3) ** 2          # soft folds along the cloth
    cc = cloth[0] * (fold * (1.05 - 0.2 * cv))[..., None]
    trim = ce < 0.14
    cc[trim] = cloth[1]
    img[:, xr, :3] = cc
    img[:, xr, 3] = 255
    if mat in ("golden", "netherite", "diamond", "copper"):   # their cloth trim glows
        img[:, xr, 3] = np.where(trim, 40, 255)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")


# ====================================================================== panes (tiers without Renderer Foundation glass)
def pane_tex(mat, layer, islands, island_layer):
    """RGBA 512x256: every clean island of this layer painted as a framed panel in the tier's design"""
    S = PANE
    img = np.zeros((32 * S, 64 * S, 4))
    img[..., 3] = 255
    img[..., :3] = 40
    for name, (x, y, w, h) in islands.items():
        if island_layer[name] != layer:
            continue
        W, H = w * S, h * S
        yy, xx = np.mgrid[0:H, 0:W]
        u, v = (xx + .5) / W, (yy + .5) / H
        e = np.minimum(np.minimum(xx + .5, W - xx - .5), np.minimum(yy + .5, H - yy - .5))   # px to the panel edge
        glow = np.zeros((H, W))
        visor = name == "crown"
        seed = sum(map(ord, name)) + len(mat)
        if mat == "turtle":
            base = rgb(226, 220, 210) if mat == "leather" else rgb(80, 178, 156)
            q = ((xx + yy) % 14 < 1) | ((xx - yy) % 14 < 1)        # quilted diamonds
            col = base * (0.80 + 0.22 * np.sin(v * math.pi) * np.sin(u * math.pi))[..., None]
            col[q & (e > 4)] = base * 0.62
            stitch = (np.abs(e - 3.5) < 0.6) & ((xx + yy) % 6 < 3)
            col[stitch] = rgb(255, 255, 255) if mat == "leather" else rgb(240, 225, 170)
            col[e < 2.5] = base * 0.45
            if visor:
                col = rgb(26, 24, 22) * (1 + 0.8 * np.exp(-((v - .25 - u * .2) / .05) ** 2))[..., None]
                col[e < 2.2] = rgb(240, 236, 228)
        elif mat == "copper":
            col = rgb(16, 40, 40) * (1.25 - 0.45 * v)[..., None]
            P = np.stack([u * W, v * H], -1)
            segs = []
            for sd in (-1, 1):
                xo, xi = .5 + sd * .22, .5 + sd * .10
                segs += [((xo, .14), (xo, .42)), ((xo, .42), (xi, .50)), ((xi, .50), (xi, .86))]
            d = segs_dist(P, [((a_[0] * W, a_[1] * H), (b_[0] * W, b_[1] * H)) for a_, b_ in segs])
            col[(d >= 1.0) & (d < 2.4)] *= 0.5
            col[d < 1.0], glow[d < 1.0] = rgb(255, 172, 96), 1.0
            for sd in (-1, 1):
                dd = np.hypot(u * W - (.5 + sd * .22) * W, v * H - .14 * H)
                col[(dd > 1.8) & (dd < 3.3)], glow[(dd > 1.8) & (dd < 3.3)] = rgb(255, 172, 96), 1.0
            col[e < 3] = rgb(206, 116, 66) * (0.8 + 0.07 * e[e < 3, None])
        elif mat == "chainmail":
            col = rgb(28, 32, 40) * (1.0 + 0.8 * (1 - v))[..., None]
            bv = (v * 8) % 1.0
            col[(bv < 1.2 / H * 8) & (e > 3)] = rgb(12, 14, 18)
            col[((bv >= 1.2 / H * 8) & (bv < 2.4 / H * 8)) & (e > 3)] = rgb(96, 106, 126)
            cs = (np.abs(xx + .5 - W / 2) < 1.0) & (v > .08) & (v < .92)
            col[cs], glow[cs] = rgb(205, 232, 255), 0.8
            col[e < 3] = rgb(196, 206, 224) * (0.7 + 0.1 * e[e < 3, None])
        elif mat in LIVERY:
            base, cob, gl, trim, glows = LIVERY[mat]
            col = base * (1.03 - 0.12 * v)[..., None] * (0.94 + 0.07 * np.sin(u * math.pi))[..., None]
            stripe = np.abs(u - .5) * W < 2.2
            col[stripe & (e > 4)] = cob
            inner = (e >= 2.5) & (e < 3.6)
            col[e < 2.5] = trim
            if glows:
                col[inner], glow[inner] = gl, 1.0
            else:
                col[inner & ((xx + yy) % 6 < 3)] = gl
        elif mat == "iron":
            col = rgb(236, 240, 246) * (1.03 - 0.12 * v)[..., None] * (0.94 + 0.07 * np.sin(u * math.pi))[..., None]
            stripe = np.abs(u - .5) * W < 2.2
            col[stripe & (e > 4)] = rgb(36, 98, 235)
            inner = (e >= 2.5) & (e < 3.6)
            col[e < 2.5] = rgb(50, 56, 70)
            col[inner], glow[inner] = rgb(110, 170, 255), 1.0
        elif mat == "golden":
            col = rgb(22, 12, 14) * (1.4 - 0.6 * v)[..., None]
            P = np.stack([(u - .5) * W, v * H], -1)
            def taper(a_, b_, w0, w1):
                a_, b_ = np.array(a_, float), np.array(b_, float)
                ab = b_ - a_
                t = np.clip(((P - a_) @ ab) / (ab @ ab), 0, 1)
                return np.linalg.norm(P - (a_ + t[..., None] * ab), axis=-1) - (w0 + (w1 - w0) * t)
            core = taper((0, .08 * H), (0, .92 * H), 1.0, 1.0)
            for vy in (.3, .62):
                for sd in (-1, 1):
                    core = np.minimum(core, taper((0, vy * H), (sd * .36 * W, (vy + .2) * H), 1.6, 0.2))
            halo = np.exp(-np.maximum(core, 0) / 3.0)
            m = e > 3
            col[m] = col[m] * (1 - 0.85 * halo[m, None]) + rgb(255, 92, 18) * (0.85 * halo[m, None])
            glow[m] = 0.75 * halo[m]
            hot = (core < 0) & m
            col[hot], glow[hot] = rgb(255, 228, 140), 1.0
            col[e < 3] = rgb(232, 104, 34) * (0.75 + 0.08 * e[e < 3, None])
            glow[e < 1.5] = 0.7
        else:
            continue
        if visor:   # smoked visor glass: darker at the brow, one soft reflection, a fine rim in the tier's light
            tint, rimc, rim_glow = {"leather": (rgb(24, 22, 20), rgb(236, 230, 220), 0.0),
                                    "turtle": (rgb(8, 30, 28), rgb(150, 240, 210), 0.0),
                                    "copper": (rgb(8, 22, 22), rgb(255, 172, 96), 1.0),
                                    "chainmail": (rgb(8, 10, 16), rgb(205, 232, 255), 0.8),
                                    "iron": (rgb(8, 14, 30), rgb(110, 170, 255), 1.0),
                                    "golden": (rgb(16, 6, 6), rgb(255, 140, 48), 1.0)}[mat]
            refl = np.exp(-((v - .18 - .45 * u) / .16) ** 2) * (1 - v) * 0.22
            col = tint * (0.7 + 0.9 * v)[..., None] + rgb(200, 215, 235) * refl[..., None]
            glow = np.zeros((H, W))
            r = e < 2.2
            col[r], glow[r] = rimc, rim_glow
        sl = (slice(y * S, y * S + H), slice(x * S, x * S + W))
        img[sl[0], sl[1], :3] = col
        img[sl[0], sl[1], 3] = 255 * (1 - glow)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")


# ====================================================================== light rails (strip 8 x 256, period 0.5 of v)
def rail_tex(mat):
    H, W = 256, 8
    img = np.zeros((H, W, 4))
    v = (np.arange(H) + .5) / H
    x = (v - 0.25) % 0.5                      # 0 = the packet, growing along its trail
    dpk = np.minimum(x, 0.5 - x)
    if mat == "copper":     # amber dashes, one lit at a time
        dash = ((v * 16) % 1.0) < 0.55
        g = np.where(dash, 0.45, 0.12) + 1.0 * np.exp(-(dpk / 0.06) ** 2) * dash
        col = rgb(255, 160, 80)[None] * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 1, 0, 1)[:, None] * 0.5
    elif mat == "leather":    # soft white track (the dye can't reach the rails: neutral)
        g = 0.35 + 0.8 * np.exp(-(dpk / 0.08) ** 2)
        col = rgb(255, 244, 228)[None] * np.clip(g, 0, 1)[:, None]
    elif mat == "chainmail":  # dim ice track, one soft wide pulse
        g = 0.30 + 0.9 * np.exp(-(dpk / 0.10) ** 2)
        col = rgb(205, 232, 255)[None] * np.clip(g, 0, 1)[:, None]
    elif mat == "iron":       # cobalt packet with a tail
        tail = np.where(x < 0.25, np.exp(-(x / 0.10)) * 0.5, 0)
        g = 0.55 + 1.2 * np.exp(-(dpk / 0.05) ** 2) + tail
        col = rgb(70, 140, 255)[None] * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 0.9, 0, 1)[:, None] * 0.9
    elif mat == "golden":     # warm gold packet with a tail
        tail = np.where(x < 0.25, np.exp(-(x / 0.10)) * 0.5, 0)
        g = 0.55 + 1.2 * np.exp(-(dpk / 0.05) ** 2) + tail
        col = rgb(255, 196, 80)[None] * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 0.9, 0, 1)[:, None] * 0.9
    elif mat == "netherite":  # violet track, twin packets
        x2 = (v * 2 - 0.25) % 0.5
        d2 = np.minimum(x2, 0.5 - x2)
        g = 0.35 + 1.1 * np.exp(-(d2 / 0.045) ** 2)
        col = rgb(178, 84, 255)[None] * np.clip(g, 0, 1)[:, None] + rgb(255, 200, 255)[None] * np.clip(g - 1, 0, 1)[:, None] * 2
    elif mat == "diamond":    # ice-white light; the prism shows only as a faint spectral fringe in the packet's tail
        tail = np.where(x < 0.22, np.exp(-(x / 0.08)), 0)
        g = 0.62 + 1.4 * np.exp(-(dpk / 0.035) ** 2)
        fringe = hsv(0.45 + x * 3.0, np.full(H, 0.45), np.full(H, 1.0)) * 255
        col = rgb(150, 236, 255)[None] * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 1, 0, 1)[:, None]
        col = col * (1 - 0.5 * tail[:, None]) + fringe * (0.5 * tail[:, None])
    else:
        col = np.full((H, 3), 200.0)
    img[:, :, :3] = np.clip(col, 0, 255)[:, None, :]
    img[:, :, 3] = 0                          # all glow
    return Image.fromarray(img.astype(np.uint8), "RGBA")


# ====================================================================== reactions (tier 4): RC overlay colour
def react_overlay(var, mat):
    """hurt: a flash in the tier's light that fades over the hurt time; below 3 hearts: a slow red pulse"""
    flash = {"diamond": (0.85, 0.97, 1.0), "netherite": (0.86, 0.55, 1.0)}[mat]
    a_hurt = "math.clamp((q.hurt_time ?? 0) / 10, 0, 1) * 0.85"
    a_low = "(q.health < 6 ? (0.5 + 0.5 * math.sin(q.life_time * 240)) * 0.55 : 0)"
    hurt = f"(q.hurt_time ?? 0) > 0"
    return {"r": f"{hurt} ? {flash[0]} : 1.0", "g": f"{hurt} ? {flash[1]} : 0.15", "b": f"{hurt} ? {flash[2]} : 0.2",
            "a": f"math.max({a_hurt}, {a_low})"}
