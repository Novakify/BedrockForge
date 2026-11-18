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
  tier 4+ diamond    PRISM        flagship: Novak Glass over liquid chrome (512px), a beam that splits into a spectrum,
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
    "netherite": dict(level=4, name="ECLIPSE", rf=True, react=True, speed=1.15, acc=("collar", "tails")),
    "diamond":   dict(level=4, name="PRISM", rf=True, react=True, speed=1.5, acc=("pauldrons", "splitcape"), allglass=True),
    "turtle":    dict(level=1, name="REEF", rf=False, react=False, speed=0.0, acc=()),
}
TEXM = 128            # plate atlas (same layout as the metal atlas: plate | visor | rim | recess)
PANE = 8              # pane texture = 8x the 64x32 island layout


def rgb(*c):
    return np.array(c, float)


# livery (iron's recipe in each tier's colours): base, stripe, inlay light, trim, glows?
LIVERY = {
    "leather": (rgb(232, 226, 216), rgb(120, 112, 104), rgb(255, 255, 255), rgb(70, 64, 58), False),
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
    if mat == "diamond":
        return chrome_tex()
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
    elif mat == "copper":   # CIRCUIT (ref: black suit, green Y-lines + node circles): a keel trace forking into a Y
        base, trim, gl = rgb(30, 44, 42), rgb(190, 112, 64), rgb(255, 170, 90)
        col = base[None, None] * (1.10 - 0.25 * v)[..., None] * (0.90 + 0.12 * np.cos((u - .5) * 3))[..., None]
        P = np.stack([(u - .5) * W, v * S], -1)
        segs = [((0, .30 * S), (0, .92 * S)), ((0, .30 * S), (-.30 * W, .10 * S)), ((0, .30 * S), (.30 * W, .10 * S))]
        d = segs_dist(P, segs)
        col[(d < 2.4) & (d >= .9)] *= 0.55
        line = d < .9
        col[line], glow[line] = gl, 1.0
        for (x0, y0) in ((0, .30), (-.30, .10), (.30, .10), (0, .92)):
            dd = np.hypot(P[..., 0] - x0 * W, P[..., 1] - y0 * S)
            ring = (dd > 1.7) & (dd < 3.1)
            col[ring], glow[ring] = gl, 1.0
            col[dd <= 1.7] = base * 0.5
        col[edge < .09] = trim * (0.75 + 2.2 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = base * 0.4
    elif mat == "golden":   # SOLAR (ref: black suit, orange lines flowing over the form): two swept lines + a branch
        base, trim, gl, hot = rgb(22, 18, 22), rgb(150, 104, 36), rgb(255, 150, 40), rgb(255, 222, 140)
        col = base[None, None] * (1.15 - 0.35 * v)[..., None]
        col += rgb(80, 70, 80) * (np.exp(-((u - .28) / .06) ** 2) * (1 - 0.6 * v) * 0.55)[..., None]   # gloss
        P = np.stack([u * W, v * S], -1)
        def flow(off, amp, ph, v0=0.04, v1=0.96):
            pts = [((.5 + off + amp * math.sin(t * math.pi * 1.3 + ph)) * W, t * S) for t in np.linspace(v0, v1, 24)]
            return segs_dist(P, list(zip(pts, pts[1:])))
        d = np.minimum(flow(-0.12, 0.16, 0.0), flow(0.06, 0.16, 0.0, 0.18, 0.96))
        d = np.minimum(d, segs_dist(P, [((.5 * W + .16 * W * math.sin(.55 * math.pi * 1.3 - 0.0) - .06 * W, .55 * S),
                                          (.84 * W, .70 * S))]))
        m = edge > .12
        halo = np.exp(-d / 2.0) * m
        col = col * (1 - 0.6 * halo[..., None]) + gl * (0.6 * halo)[..., None]
        glow = np.maximum(glow, 0.55 * halo)
        line = (d < .8) & m
        col[line], glow[line] = hot, 1.0
        col[edge < .09] = trim * (0.75 + 2.4 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = base * 0.3
    elif mat == "netherite":   # ECLIPSE (ref: hex-tiled armor, red trim, glowing hex core)
        base, trim, gl = rgb(40, 40, 48), rgb(70, 22, 30), rgb(255, 46, 86)
        hs = 7.0
        X_, Y_ = xx + .5, yy + .5
        def hexsd(px, py):   # flat-top hexagon "radius" of a local point
            return np.maximum(np.abs(py), np.maximum(np.abs(.866 * px + .5 * py), np.abs(.866 * px - .5 * py)))
        qx = (2 / 3 * X_) / hs
        qy = (-X_ / 3 + math.sqrt(3) / 3 * Y_) / hs
        rz_ = -qx - qy
        rx, ry, rz = np.round(qx), np.round(qy), np.round(rz_)
        dx_, dy_, dz_ = np.abs(rx - qx), np.abs(ry - qy), np.abs(rz - rz_)
        a1 = (dx_ > dy_) & (dx_ > dz_)
        rx = np.where(a1, -ry - rz, rx)
        a2 = ~a1 & (dy_ > dz_)
        ry = np.where(a2, -rx - rz, ry)
        cx, cy = hs * 1.5 * rx, hs * math.sqrt(3) * (ry + rx / 2)
        de = hs * .866 - hexsd(X_ - cx, Y_ - cy)              # px to this tile's edge
        h = np.modf(np.abs(np.sin(rx * 12.9898 + ry * 78.233)) * 43758.5)[0]
        col = base[None, None] * (0.78 + 0.45 * h)[..., None] * (1.12 - 0.28 * v)[..., None]
        col[de < 1.6] *= 1.35                                 # tile bevel
        col[de < 0.8] = rgb(12, 10, 16)                       # seams
        lit = (h < 0.14) & (v > .30) & (de < 1.1) & (de > 0.1)
        col[lit], glow[lit] = gl, 1.0                          # a few tiles outlined in crimson, a lit spine
        dc = hexsd(X_ - W / 2, Y_ - .17 * S)                  # the core: a white hex ring
        core = np.abs(dc - 6.0) < 1.0
        col[core], glow[core] = rgb(255, 238, 244), 1.0
        col[dc < 5.0] = rgb(60, 14, 26)
        glow[dc < 5.0] = 0.5
        inl = (edge >= .11) & (edge < .135) & (v > .03) & (v < .97)
        col[inl], glow[inl] = gl, 1.0
        col[edge < .09] = rgb(110, 108, 120) * (0.55 + 2.6 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = trim * 0.5
    elif mat == "diamond":   # PRISM (refs: liquid glass + a prism): liquid navy chrome, white light that splits into
        base = rgb(12, 20, 44)                               # a spectrum at its end
        col = base[None, None] * (1.25 - 0.4 * v)[..., None]
        for ph, a, w_ in ((0.0, 0.18, 0.045), (1.9, 0.12, 0.03)):   # liquid highlights: soft white bands that flow
            cx = .5 + a * np.sin(v * math.pi * 2.2 + ph) - 0.2
            band = np.exp(-((u - cx) / w_) ** 2) * (0.55 + 0.45 * np.sin(v * math.pi * 3 + ph))
            col += rgb(170, 200, 255) * (band * 0.75)[..., None]
        col += rgb(60, 90, 160) * (np.exp(-((u - .80) / .08) ** 2) * 0.35)[..., None]   # cool rim reflection
        P = np.stack([(u - .5) * W, v * S], -1)
        d = segs_dist(P, [((0, .04 * S), (0, .60 * S))])     # the beam down the keel
        m = edge > .12
        beam = (d < 1.0) & m
        col = col + rgb(200, 230, 255) * (np.exp(-d / 2.2) * 0.45 * m)[..., None]
        col[beam], glow[beam] = rgb(255, 255, 255), 1.0
        for k in range(6):                                   # ...and its dispersion
            t = k / 5
            tip = ((t - .5) * .62 * W, .97 * S)
            dk = segs_dist(P, [((0, .60 * S), tip)])
            c = hsv(0.0 + 0.78 * t, 0.85, 1.0) * 255
            ln = (dk < 0.75) & m & (P[..., 1] > .60 * S)
            col[ln], glow[ln] = c, 1.0
        inl = (edge >= .115) & (edge < .135) & (v > .03) & (v < .97)
        col[inl], glow[inl] = rgb(235, 245, 255), 1.0
        col[edge < .09] = rgb(190, 210, 245) * (0.65 + 2.8 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = rgb(4, 8, 18)
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
             "copper": (rgb(30, 44, 42), rgb(255, 170, 90)), "chainmail": (rgb(30, 33, 42), rgb(150, 162, 186)),
             "iron": (rgb(30, 78, 196), rgb(236, 240, 246)), "golden": (rgb(118, 18, 22), rgb(250, 190, 80)),
             "diamond": (rgb(14, 22, 48), rgb(240, 248, 255)), "netherite": (rgb(26, 24, 30), rgb(255, 46, 86))}[mat]
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
            col = rgb(24, 36, 34) * (1.25 - 0.45 * v)[..., None]
            P = np.stack([u * W, v * H], -1)
            segs = [((.5, .40), (.5, .88)), ((.5, .40), (.24, .14)), ((.5, .40), (.76, .14))]
            d = segs_dist(P, [((a_[0] * W, a_[1] * H), (b_[0] * W, b_[1] * H)) for a_, b_ in segs])
            col[(d >= 1.0) & (d < 2.4)] *= 0.5
            col[d < 1.0], glow[d < 1.0] = rgb(255, 172, 96), 1.0
            for (x0, y0) in ((.5, .40), (.24, .14), (.76, .14), (.5, .88)):
                dd = np.hypot(u * W - x0 * W, v * H - y0 * H)
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
            col = rgb(20, 16, 20) * (1.3 - 0.5 * v)[..., None]
            P = np.stack([u * W, v * H], -1)
            def flow(off, amp, v0, v1):
                pts = [((.5 + off + amp * math.sin(t * math.pi * 1.3)) * W, t * H) for t in np.linspace(v0, v1, 20)]
                return segs_dist(P, list(zip(pts, pts[1:])))
            d = np.minimum(flow(-.14, .18, .06, .94), flow(.10, .18, .25, .94))
            m = e > 3
            halo = np.exp(-d / 2.6) * m
            col = col * (1 - 0.6 * halo[..., None]) + rgb(255, 150, 40) * (0.6 * halo)[..., None]
            glow = np.maximum(glow, 0.55 * halo)
            hot = (d < 1.0) & m
            col[hot], glow[hot] = rgb(255, 222, 140), 1.0
            col[e < 3] = rgb(150, 104, 36) * (0.75 + 0.08 * e[e < 3, None])
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
        col = rgb(255, 46, 86)[None] * np.clip(g, 0, 1)[:, None] + rgb(255, 210, 220)[None] * np.clip(g - 1, 0, 1)[:, None] * 2
    elif mat == "diamond":    # a white beam; behind the packet it splits into the spectrum (the prism)
        tail = np.where(x < 0.30, 1 - x / 0.30, 0)
        g = 0.70 + 1.4 * np.exp(-(dpk / 0.035) ** 2)
        spec = hsv(np.clip(x / 0.30, 0, 1) * 0.78, np.full(H, 0.85), np.full(H, 1.0)) * 255
        col = rgb(240, 248, 255)[None] * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 1, 0, 1)[:, None]
        col = col * (1 - 0.75 * tail[:, None]) + spec * (0.75 * tail[:, None])
    else:
        col = np.full((H, 3), 200.0)
    img[:, :, :3] = np.clip(col, 0, 255)[:, None, :]
    img[:, :, 3] = 0                          # all glow
    return Image.fromarray(img.astype(np.uint8), "RGBA")


# ====================================================================== reactions (tier 4): RC overlay colour
def react_overlay(var, mat):
    """hurt: a flash in the tier's light that fades over the hurt time; below 3 hearts: a slow red pulse"""
    flash = {"diamond": (1.0, 1.0, 1.0), "netherite": (1.0, 0.25, 0.4)}[mat]
    a_hurt = "math.clamp((q.hurt_time ?? 0) / 10, 0, 1) * 0.95"
    a_low = "(q.health < 6 ? (0.5 + 0.5 * math.sin(q.life_time * 240)) * 0.55 : 0)"
    hurt = f"(q.hurt_time ?? 0) > 0"
    return {"r": f"{hurt} ? {flash[0]} : 1.0", "g": f"{hurt} ? {flash[1]} : 0.15", "b": f"{hurt} ? {flash[2]} : 0.2",
            "a": f"math.max({a_hurt}, {a_low})"}


# ====================================================================== PRISM: high-res liquid chrome (512 x 512)
def _ramp(x, stops):
    """piecewise-linear colour ramp; x in 0..1 (wraps), stops [(pos, rgb)]"""
    x = np.asarray(x) % 1.0
    out = np.zeros(x.shape + (3,))
    pos = [p for p, _ in stops] + [stops[0][0] + 1]
    cols = [np.array(c, float) for _, c in stops] + [np.array(stops[0][1], float)]
    for i in range(len(stops)):
        a, b = pos[i], pos[i + 1]
        m = (x >= a) & (x < b) if i else ((x < b) | (x >= pos[-2] + 0 if False else x < b))
        m = (x >= a) & (x < b)
        t = ((x - a) / (b - a))[m]
        t = t * t * (3 - 2 * t)
        out[m] = cols[i] * (1 - t[:, None]) + cols[i + 1] * t[:, None]
    lo = x < pos[0]
    out[lo] = cols[0]
    return out


# chrome reflects a sky: deep indigo below, a dark horizon line, a molten amber band, a hot white sky streak, violet
CHROME = [(0.00, (14, 10, 40)), (0.20, (34, 22, 96)), (0.34, (92, 58, 196)), (0.41, (210, 200, 255)),
          (0.43, (255, 255, 255)), (0.46, (150, 128, 245)), (0.58, (48, 30, 120)), (0.70, (8, 6, 22)),
          (0.76, (200, 100, 40)), (0.79, (255, 196, 120)), (0.83, (110, 46, 40)), (0.92, (20, 12, 44))]


def chrome_tex(S=512):
    img = np.zeros((S, S, 4))
    img[..., 3] = 255
    W = S // 2
    yy, xx = np.mgrid[0:S, 0:W]
    u, v = (xx + .5) / W, (yy + .5) / S
    edge = np.minimum(u, 1 - u)
    k = W / 64.0                                            # px per "design px" (lines stay crisp at any size)
    glow = np.zeros((S, W))
    # the reflection flows: a liquid warp of u along the plate, the bands bend round the plate's curve
    um = np.abs(u - .5) * 2                                  # mirror-symmetric: no seam where the helmet's wrap meets
    f = 0.12 + 0.62 * um + 0.08 * v + 0.04 * np.sin(v * math.pi * 2.2 + um * 2.0) + 0.35 * um ** 2
    col = _ramp(f, CHROME)
    fres = np.clip(1 - edge / 0.22, 0, 1) ** 2               # rim: brighter toward the plate's edges (fresnel)
    col = col * (1 - 0.35 * fres[..., None]) + np.array([200, 190, 255]) * (0.35 * fres)[..., None]
    cx = .62 + 0.03 * np.sin(v * math.pi * 2.0 + 0.9)       # a hot specular thread each side, riding the liquid
    sp = (np.exp(-((um - cx) / .018) ** 2)) * (0.6 + 0.4 * np.sin(v * math.pi))
    col = col + 255 * (0.8 * sp)[..., None]
    P = np.stack([(u - .5) * W, v * S], -1)
    m = edge > .12
    d = segs_dist(P, [((0, .04 * S), (0, .58 * S))])         # the beam...
    col += np.array([220, 236, 255]) * (np.exp(-d / (2.0 * k)) * 0.35 * m)[..., None]
    beam = (d < 0.6 * k) & m
    col[beam], glow[beam] = (255, 255, 255), 1.0
    for i in range(7):                                       # ...split by the prism
        t = i / 6
        tip = ((t - .5) * .66 * W, .97 * S)
        dk = segs_dist(P, [((0, .58 * S), tip)])
        c = hsv(0.0 + 0.80 * t, 0.70, 1.0) * 255
        sel = m & (P[..., 1] > .58 * S)
        col = col + c * (np.exp(-dk / (1.6 * k)) * 0.22 * sel)[..., None]
        ln = (dk < 0.45 * k) & sel
        col[ln], glow[ln] = c, 1.0
    inl = (edge >= .110) & (edge < .118) & (v > .02) & (v < .98)   # a fine white light just inside the bevel
    col[inl], glow[inl] = (250, 252, 255), 1.0
    bev = edge < .09                                         # polished bevel: bright chrome
    col[bev] = _ramp(f[bev] + 0.5, CHROME) * 0.6 + 255 * 0.4 * (edge[bev, None] / .09)
    col[(edge >= .09) & (edge < .11)] = (6, 4, 16)
    img[:, :W, :3] = np.clip(col, 0, 255)
    img[:, :W, 3] = 255 * (1 - glow)
    # visor | rim | recess (cape cloth: the same liquid chrome, quieter, with a white-lit hem)
    q = S // 4
    vv, uu = np.mgrid[0:S, 0:q]
    vv, uu = (vv + .5) / S, (uu + .5) / q
    img[:, W:W + q, :3] = _ramp(0.70 * uu + 0.25 * vv + 0.10, CHROME) * 0.35
    img[:, W + q:W + q + S // 8, :3] = (240, 246, 255)
    img[:, W + q:W + q + S // 8, 3] = 40
    cw = S - (W + q + S // 8)
    vv, uu = np.mgrid[0:S, 0:cw]
    vv, uu = (vv + .5) / S, (uu + .5) / cw
    cl = _ramp(0.45 * uu + 0.35 * vv + 0.08 * np.sin(vv * 9 + uu * 3), CHROME) * 0.75
    ce = np.minimum(uu, 1 - uu)
    hem = ce < 0.07
    cl[hem] = (245, 248, 255)
    img[:, W + q + S // 8:, :3] = cl
    img[:, W + q + S // 8:, 3] = np.where(hem, 40, 255)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")


# ====================================================================== match moments (Hive Bed Wars / SkyWars)
# Bedrock Forge (MatchFx) writes v.fbev_bed / v.fbev_kill / v.fbev_resp = seconds since you broke a bed, got a kill /
# elimination, respawned. Each tier answers each moment with its own particles (in its design language) and a flash of
# its light over the armor; the rails race for a moment.
EVENTS = ("bed", "kill", "resp")
FX_CELLS = ("dot", "streak", "shard", "ring", "hex", "ember", "star", "pixel", "chevron")   # 16 px cells, 4 x 3 atlas
ACCENT = {"leather": (1.0, 0.96, 0.9), "copper": (1.0, 0.64, 0.30), "chainmail": (0.78, 0.9, 1.0),
          "iron": (0.38, 0.62, 1.0), "golden": (1.0, 0.72, 0.25), "netherite": (1.0, 0.18, 0.34),
          "diamond": (0.92, 0.97, 1.0), "turtle": (0.5, 0.95, 0.8)}


def fx_atlas():
    """64 x 48: soft dot, spark streak, shard, ring, hex outline, ember, 4-point star, pixel, chevron (white, alpha)"""
    C = 16
    img = np.zeros((48, 64, 4))
    yy, xx = np.mgrid[0:C, 0:C]
    x, y = (xx + .5) / C * 2 - 1, (yy + .5) / C * 2 - 1
    r = np.hypot(x, y)
    hexr = np.maximum(np.abs(y), np.maximum(np.abs(.866 * x + .5 * y), np.abs(.866 * x - .5 * y)))
    cells = {
        "dot": np.clip(1 - r, 0, 1) ** 1.6,
        "streak": np.clip(1 - np.abs(x) * 5, 0, 1) * np.clip(1 - np.abs(y) ** 3, 0, 1),
        "shard": np.clip(1 - (np.abs(x) * 1.5 + np.abs(y)), 0, 1) ** 0.5,
        "ring": np.clip(1 - np.abs(r - .72) * 7, 0, 1),
        "hex": np.clip(1 - np.abs(hexr - .72) * 7, 0, 1),
        "ember": np.clip(1 - r * 1.4, 0, 1) ** 0.7,
        "star": np.clip(np.maximum(1 - np.abs(x) * 7 - np.abs(y) * .9, 1 - np.abs(y) * 7 - np.abs(x) * .9), 0, 1)
                + np.clip(1 - r * 3, 0, 1),
        "pixel": ((np.abs(x) < .5) & (np.abs(y) < .5)).astype(float),
        "chevron": np.clip(1 - np.abs(y + .3 - np.abs(x) * .9) * 6, 0, 1) * (np.abs(x) < .85),
    }
    for i, n in enumerate(FX_CELLS):
        cx, cy = (i % 4) * C, (i // 4) * C
        img[cy:cy + C, cx:cx + C, :3] = 255
        img[cy:cy + C, cx:cx + C, 3] = np.clip(cells[n], 0, 1) * 255
    return Image.fromarray(img.astype(np.uint8), "RGBA")


# per tier, per moment: (cell, count, shape, speed, life, gravity, size, colour mode, extra)
#   shape: "sphere", "disc" (ring on the ground, outwards), "up" (a column upwards), "in" (converging on you)
#   colour: "acc" (the tier's light), "white", "spectrum" (a random hue each), "dark" (smoke)
FX = {
    "leather": {"bed": ("dot", 26, "sphere", 3.2, 1.2, -3, .30, "white"), "kill": ("pixel", 22, "sphere", 3.8, 1.0, -6, .14, "white"),
                "resp": ("dot", 34, "disc", 3.0, 1.0, 0, .26, "white")},
    "copper": {"bed": ("streak", 40, "sphere", 8.0, .8, -9, .34, "acc"), "kill": ("streak", 30, "up", 9.0, .8, 0, .42, "acc"),
               "resp": ("dot", 34, "up", 2.6, 1.5, 0, .20, "acc")},
    "chainmail": {"bed": ("shard", 26, "sphere", 4.0, 1.3, -7, .28, "acc"), "kill": ("dot", 30, "up", 1.8, 1.8, 0.5, .38, "acc"),
                  "resp": ("ring", 1, "point", 0, .9, 0, 3.4, "acc")},
    "iron": {"bed": ("ring", 1, "point", 0, .8, 0, 4.2, "acc"), "kill": ("chevron", 18, "up", 8.0, .9, 0, .40, "acc"),
             "resp": ("hex", 34, "shell", 0.5, 1.1, 0, .40, "acc")},
    "golden": {"bed": ("ember", 50, "sphere", 6.5, 1.4, -8, .24, "acc"), "kill": ("ember", 44, "spiral", 3.5, 1.5, 1.5, .24, "acc"),
               "resp": ("ring", 1, "point", 0, 1.0, 0, 4.0, "acc")},
    "netherite": {"bed": ("hex", 34, "sphere", 6.5, 1.1, -6, .34, "acc"), "kill": ("hex", 36, "spiral", 3.0, 1.5, 1.0, .30, "acc"),
                  "resp": ("hex", 40, "in", 3.6, .9, 0, .32, "acc")},
    "diamond": {"bed": ("shard", 54, "sphere", 8.0, 1.3, -7, .34, "spectrum"), "kill": ("streak", 44, "up", 12.0, 1.1, 0, .70, "spectrum"),
                "resp": ("shard", 54, "in", 5.0, .9, 0, .32, "spectrum")},
}
# and a flash at the body for every moment: (cell, size, life) - the punch that makes the moment read mid-fight
FLASH = {"leather": ("dot", 1.4, .22), "copper": ("star", 2.2, .25), "chainmail": ("dot", 1.8, .30), "iron": ("star", 2.4, .25),
         "golden": ("ember", 2.4, .30), "netherite": ("hex", 2.0, .30), "diamond": ("star", 3.2, .32), "turtle": ("dot", 1.4, .22)}
FX["turtle"] = FX["leather"]


def fx_particle(ident, mat, ev):
    cell, n, shape, speed, life, grav, size, cmode = FX[mat][ev]
    i = FX_CELLS.index(cell)
    uv = [(i % 4) * 16, (i // 4) * 16]
    a = ACCENT[mat]
    comps = {"minecraft:emitter_rate_instant": {"num_particles": n},
             "minecraft:emitter_lifetime_once": {"active_time": 0.05},
             "minecraft:particle_lifetime_expression": {"max_lifetime": f"{life} * (0.75 + 0.5 * v.particle_random_2)"},
             "minecraft:particle_initial_speed": f"{speed} * (0.6 + 0.6 * v.particle_random_3)",
             "minecraft:particle_motion_dynamic": {"linear_acceleration": [0, grav, 0], "linear_drag_coefficient": 0.6}}
    if shape == "sphere":
        comps["minecraft:emitter_shape_sphere"] = {"offset": [0, 1.0, 0], "radius": 0.35, "direction": "outwards"}
    elif shape == "disc":
        comps["minecraft:emitter_shape_disc"] = {"offset": [0, 0.1, 0], "radius": 0.4, "plane_normal": "y",
                                                 "surface_only": True, "direction": "outwards"}
    elif shape == "up":
        comps["minecraft:emitter_shape_disc"] = {"offset": [0, 0.2, 0], "radius": 0.55, "plane_normal": "y",
                                                 "direction": [0, 1, 0]}
    elif shape == "shell":
        comps["minecraft:emitter_shape_sphere"] = {"offset": [0, 1.0, 0], "radius": 1.0, "surface_only": True,
                                                   "direction": "outwards"}
    elif shape == "in":
        comps["minecraft:emitter_shape_sphere"] = {"offset": [0, 1.0, 0], "radius": 1.6, "surface_only": True,
                                                   "direction": "inwards"}
    elif shape == "spiral":
        comps["minecraft:emitter_shape_disc"] = {"offset": [0, 0.2, 0], "radius": 0.7, "plane_normal": "y",
                                                 "surface_only": True, "direction": [0, 1, 0]}
        comps["minecraft:particle_motion_dynamic"]["linear_acceleration"] = [
            "math.cos(v.particle_age * 400 + v.particle_random_1 * 360) * 6", grav,
            "math.sin(v.particle_age * 400 + v.particle_random_1 * 360) * 6"]
    else:   # point: one big ring on the ground, growing
        comps["minecraft:emitter_shape_point"] = {"offset": [0, 0.08, 0]}
    grow = shape == "point"
    sz = f"{size} * (0.2 + 1.2 * v.particle_age / v.particle_lifetime)" if grow else \
        f"{size} * (1 - 0.6 * v.particle_age / v.particle_lifetime) * (0.7 + 0.6 * v.particle_random_4)"
    face = "direction_y" if cell in ("streak",) else ("emitter_transform_xz" if grow else "rotate_xyz")
    bb = {"size": [sz if cell != "streak" else f"{size} * 0.35", sz], "facing_camera_mode": face,
          "uv": {"texture_width": 64, "texture_height": 48, "uv": uv, "uv_size": [16, 16]}}
    if face == "direction_y":
        bb["direction"] = {"mode": "derive_from_velocity"}
    comps["minecraft:particle_appearance_billboard"] = bb
    fade = "math.clamp(2 - 2 * v.particle_age / v.particle_lifetime, 0, 1)"
    if cmode == "spectrum":
        h = "v.particle_random_1 * 300"
        col = [f"0.62 + 0.38 * math.cos({h})", f"0.62 + 0.38 * math.cos({h} - 120)", f"0.62 + 0.38 * math.cos({h} - 240)", fade]
    elif cmode == "white":
        col = [1.0, 1.0, 1.0, f"{fade} * 0.85"]
    else:
        col = [a[0], a[1], a[2], fade]
    comps["minecraft:particle_appearance_tinting"] = {"color": col}
    return {"format_version": "1.10.0", "particle_effect": {
        "description": {"identifier": ident, "basic_render_parameters": {
            "material": "particles_blend" if mat in ("leather", "turtle") else "particles_add",
            "texture": "textures/figura/fb_armor/fx_particles"}},
        "components": comps}}


def fx_flash(ident, mat):
    cell, size, life = FLASH[mat]
    i = FX_CELLS.index(cell)
    a = ACCENT[mat]
    return {"format_version": "1.10.0", "particle_effect": {
        "description": {"identifier": ident, "basic_render_parameters": {
            "material": "particles_add", "texture": "textures/figura/fb_armor/fx_particles"}},
        "components": {
            "minecraft:emitter_rate_instant": {"num_particles": 1},
            "minecraft:emitter_lifetime_once": {"active_time": 0.05},
            "minecraft:emitter_shape_point": {"offset": [0, 1.05, 0]},
            "minecraft:particle_lifetime_expression": {"max_lifetime": life},
            "minecraft:particle_appearance_billboard": {
                "size": [f"{size} * (0.5 + 0.8 * v.particle_age / v.particle_lifetime)",
                         f"{size} * (0.5 + 0.8 * v.particle_age / v.particle_lifetime)"],
                "facing_camera_mode": "rotate_xyz",
                "uv": {"texture_width": 64, "texture_height": 48, "uv": [(i % 4) * 16, (i // 4) * 16], "uv_size": [16, 16]}},
            "minecraft:particle_appearance_tinting": {"color": [a[0], a[1], a[2], "1 - v.particle_age / v.particle_lifetime"]}}}}
