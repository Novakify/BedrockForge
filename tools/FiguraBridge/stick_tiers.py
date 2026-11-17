#!/usr/bin/env python3
"""Stick armor tiers: every armor material gets its own design (not its material's look), ranked like the Hive
BedWars progression - the higher the tier, the more pieces, light and reaction to the game.

  tier 1  leather    RUNNER       team-dyed padded fabric, stitched; no light. The base kit.
  tier 2  copper     CIRCUIT      verdigris plates, copper trim, glowing copper circuit traces; slow amber rails
  tier 2  chainmail  PHANTOM      graphite hex weave, silver trim, faint ice glow; one soft pulse
  tier 3  iron       VANGUARD     arctic white ceramic, twin cobalt stripes, cobalt light rails
  tier 3  golden     INFERNO      obsidian with magma veins, ember trim, flickering fire rails, flared shoulders
  tier 4  netherite  ECLIPSE      black chrome + violet Novak Glass, glowing runes, horns and a spined back;
                                  reacts to the game (hurt flash, low-health pulse, sprint)
  tier 4+ diamond    PRISM        flagship: Novak Glass (Renderer Foundation) panes, iridescent pearl plates, prism
                                  rails that run the spectrum, back fins and a halo; reacts to the game
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
    "leather":   dict(level=1, name="RUNNER", rf=False, react=False, speed=0.0, acc=()),
    "copper":    dict(level=2, name="CIRCUIT", rf=False, react=False, speed=0.45, acc=()),
    "chainmail": dict(level=2, name="PHANTOM", rf=False, react=False, speed=0.6, acc=()),
    "iron":      dict(level=3, name="VANGUARD", rf=False, react=False, speed=1.0, acc=()),
    "golden":    dict(level=3, name="INFERNO", rf=False, react=False, speed=1.35, acc=("flares",)),
    "netherite": dict(level=4, name="ECLIPSE", rf=True, react=True, speed=1.15, acc=("horns", "spikes")),
    "diamond":   dict(level=4, name="PRISM", rf=True, react=True, speed=1.6, acc=("fins", "halo")),
    "turtle":    dict(level=1, name="REEF", rf=False, react=False, speed=0.0, acc=()),
}
TEXM = 128            # plate atlas (same layout as the metal atlas: plate | visor | rim | recess)
PANE = 8              # pane texture = 8x the 64x32 island layout


def rgb(*c):
    return np.array(c, float)


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
    if mat in ("leather", "turtle"):
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
    elif mat == "copper":
        base, dark, hi, trim, gl = rgb(62, 142, 128), rgb(20, 58, 54), rgb(150, 220, 200), rgb(214, 120, 66), rgb(255, 168, 92)
        blot = smooth_noise((S, W), (6, 3), 7)
        col = base[None, None] * (0.80 + 0.30 * blot)[..., None] * (1.06 - 0.16 * v)[..., None]
        col = col * 0.85 + dark * 0.15 * (blot < .35)[..., None]
        P = np.stack([u * 1.0, v * 2.0], -1)
        segs = [((.5, .10), (.5, .70)), ((.5, .70), (.66, .86)), ((.66, .86), (.66, 1.40)),
                ((.5, 1.10), (.34, 1.26)), ((.34, 1.26), (.34, 1.88))]
        d = segs_dist(P, segs) * W
        pads = np.zeros((S, W), bool)
        for (a, b) in segs:
            for p in (a, b):
                pads |= (np.hypot((u - p[0]) * W, (v * 2 - p[1]) * W) < 2.6)
        tr = (d < 0.75) | pads
        col[tr] = gl
        glow[tr] = 1.0
        col[(d >= 1.0) & (d < 2.0) & ~pads] = dark * 0.6
        col[edge < .09] = trim * (0.8 + 2 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark * 0.5
    elif mat == "chainmail":
        base, dark, hi, trim, gl = rgb(46, 50, 60), rgb(16, 18, 24), rgb(176, 188, 210), rgb(204, 212, 228), rgb(205, 232, 255)
        col = base[None, None] * (1.15 - 0.35 * v)[..., None] * (0.9 + 0.12 * np.cos((u - .5) * 3))[..., None]
        hl = hex_lines(xx + .5, yy + .5, 11.0)
        col = np.where((hl < 0.8)[..., None], col * 0.75 + hi * 0.10, col)
        keel = np.abs(u - .5) < 0.6 / W
        col[keel & (v > .06) & (v < .94)] = gl * 0.9
        glow[keel & (v > .06) & (v < .94)] = 0.55
        col[edge < .09] = trim * (0.7 + 2.5 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = dark
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
    elif mat == "golden":
        base, dark, trim, gA, gB = rgb(34, 22, 24), rgb(8, 5, 6), rgb(255, 118, 36), rgb(255, 96, 20), rgb(255, 226, 120)
        gloss = np.exp(-((u - .32) / .08) ** 2) * 0.35 * (1 - v)
        col = base[None, None] * (1.0 + gloss)[..., None] + rgb(60, 40, 40) * gloss[..., None]
        d = segs_dist(np.stack([u, v * 2], -1), cracks(11, n=7, w=1.0, h=2.0)) * W
        core, halo = d < 0.8, (d >= 0.8) & (d < 2.6)
        col[halo] = col[halo] * 0.3 + gA * 0.55 * (1 - (d[halo, None] - .8) / 1.8)
        glow[halo] = 0.5 * (1 - (d[halo] - .8) / 1.8)
        col[core] = gB
        glow[core] = 1.0
        col[edge < .09] = trim * (0.75 + 2.5 * edge[edge < .09, None])
        glow[edge < .04] = 0.6
        col[(edge >= .09) & (edge < .11)] = dark
    elif mat == "diamond":
        hue = 0.50 + 0.30 * v + 0.10 * np.sin(u * 5)
        pearl = hsv(hue, np.full(hue.shape, 0.16), np.full(hue.shape, 0.97)) * 255
        col = pearl * (1.02 - 0.10 * v)[..., None] * (0.92 + 0.10 * np.cos((u - .5) * 3))[..., None]
        fac = ((np.floor((xx + yy * 0.6) / 9) + np.floor((xx - yy * 0.6) / 9)) % 2)   # soft triangular facets
        col *= (0.95 + 0.06 * fac)[..., None]
        cy = rgb(60, 236, 255)
        inl = (edge >= .11) & (edge < .135) & (v > .03) & (v < .97)
        keel = (np.abs(u - .5) < 0.7 / W) & (v > .05) & (v < .95)
        col[inl | keel] = cy
        glow[inl | keel] = 1.0
        for (sx, sy_) in ((.28, .22), (.70, .47), (.33, .72), (.66, .9)):   # four-point stars
            dx, dy = np.abs(xx + .5 - sx * W), np.abs(yy + .5 - sy_ * S)
            star = ((dx < 0.6) & (dy < 2.6)) | ((dy < 0.6) & (dx < 2.6))
            col[star] = rgb(255, 255, 255)
            glow[star] = 1.0
        col[edge < .09] = rgb(255, 255, 255) * (0.78 + 2.2 * edge[edge < .09, None])
        col[(edge >= .09) & (edge < .11)] = rgb(90, 110, 150)
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
    img[:, W + S // 4: W + 3 * S // 8, :3] = pal[1] * 0.85          # rim
    img[:, W + S // 4: W + 3 * S // 8, 3] = 255 if mat in ("leather", "turtle", "chainmail") else 90
    img[:, W + 3 * S // 8:, :3] = pal[0] * 1.4                     # recess
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
        if mat in ("leather", "turtle"):
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
            col = rgb(16, 40, 40) * (1.25 - 0.5 * v)[..., None] + rgb(30, 70, 60) * smooth_noise((H, W), (3, 2), seed)[..., None] * 0.6
            segs = []
            rng = np.random.default_rng(seed)
            for k in range(1 if h <= 4 else 2):
                x0 = rng.uniform(.3, .7)
                yA, yB = rng.uniform(.05, .3), rng.uniform(.6, .95)
                xm = np.clip(x0 + rng.choice([-.25, .25]), .15, .85)
                segs += [((x0, yA), (x0, (yA + yB) / 2)), ((x0, (yA + yB) / 2), (xm, (yA + yB) / 2 + abs(xm - x0) * w / h)),
                         ((xm, (yA + yB) / 2 + abs(xm - x0) * w / h), (xm, yB))]
            P = np.stack([u * W, v * H], -1)
            d = segs_dist(P, [((a[0] * W, a[1] * H), (b[0] * W, b[1] * H)) for a, b in segs])
            pads = np.zeros((H, W), bool)
            for a, b in segs:
                for q in (a, b):
                    pads |= np.hypot(u * W - q[0] * W, v * H - q[1] * H) < 2.6
            tr = (d < 0.8) | pads
            col[tr], glow[tr] = rgb(255, 170, 96), 1.0
            col[e < 3] = rgb(214, 120, 66) * (0.8 + 0.07 * e[e < 3, None])
            if visor:
                col = rgb(8, 20, 20) + rgb(255, 170, 96) * np.exp(-((v - .3 - u * .2) / .04) ** 2)[..., None] * 0.35
                col[e < 3], glow[e < 3] = rgb(255, 170, 96), 1.0
        elif mat == "chainmail":
            col = rgb(26, 30, 38) * (1.0 + 0.9 * (1 - v))[..., None]
            hl = hex_lines(xx + .5, yy + .5, 12.0)
            m = hl < 0.8
            col[m] = col[m] * 0.75 + rgb(205, 232, 255) * 0.16
            glow[m] = 0.15
            cs = (np.abs(u - .5) < 0.7 / W * 2) & (v > .1) & (v < .9)   # one ice seam down the pane
            col[cs], glow[cs] = rgb(205, 232, 255), 0.8
            col[e < 3] = rgb(204, 212, 228) * (0.7 + 0.1 * e[e < 3, None])
            if visor:
                col = rgb(8, 10, 16) + rgb(205, 232, 255) * np.exp(-((v - .28 - u * .2) / .04) ** 2)[..., None] * 0.4
                col[e < 2.5], glow[e < 2.5] = rgb(205, 232, 255), 0.8
        elif mat == "iron":
            col = rgb(236, 240, 246) * (1.03 - 0.12 * v)[..., None] * (0.94 + 0.07 * np.sin(u * math.pi))[..., None]
            stripe = (np.abs(u - .5) > .06) & (np.abs(u - .5) < .17)
            col[stripe] = rgb(36, 98, 235)
            inner = (e >= 2.5) & (e < 3.8)
            col[e < 2.5] = rgb(50, 56, 70)
            col[inner], glow[inner] = rgb(110, 170, 255), 1.0
            if visor:
                col = rgb(10, 14, 26) + rgb(96, 160, 255) * np.exp(-((v - .28 - u * .2) / .04) ** 2)[..., None] * 0.45
                col[e < 2.5] = rgb(236, 240, 246)
                col[inner], glow[inner] = rgb(110, 170, 255), 1.0
        elif mat == "golden":
            col = rgb(20, 10, 12) * (1.4 - 0.6 * v)[..., None]
            d = segs_dist(np.stack([u * W, v * H], -1), [((a[0] * W, a[1] * H), (b[0] * W, b[1] * H))
                                                          for a, b in cracks(seed, n=5, w=1.0, h=1.0)])
            halo = (d >= 1.0) & (d < 4.0)
            col[halo] = col[halo] * 0.4 + rgb(255, 96, 20) * (0.7 * (1 - (d[halo, None] - 1) / 3))
            glow[halo] = 0.6 * (1 - (d[halo] - 1) / 3)
            col[d < 1.0], glow[d < 1.0] = rgb(255, 226, 120), 1.0
            col[e < 3] = rgb(255, 118, 36) * (0.75 + 0.08 * e[e < 3, None])
            glow[e < 1.5] = 0.7
            if visor:
                col = rgb(14, 4, 4) + rgb(255, 120, 30) * np.exp(-((v - .3 - u * .2) / .04) ** 2)[..., None] * 0.5
                col[e < 3], glow[e < 3] = rgb(255, 140, 40), 1.0
        else:
            continue
        if visor:   # the tier's face: a glowing eye line across the visor, each tier its own
            eye = {"leather": None, "turtle": None,
                   "copper": ("lenses", rgb(255, 170, 96)), "chainmail": ("slit", rgb(205, 232, 255)),
                   "iron": ("chevron", rgb(110, 170, 255)), "golden": ("slit2", rgb(255, 170, 60))}[mat]
            if eye:
                kind, c = eye
                if kind == "lenses":
                    for cx in (.32, .68):
                        d = np.hypot((u - cx) * W, (v - .40) * H)
                        ring = (d > 3.0) & (d < 5.0)
                        col[ring], glow[ring] = c, 1.0
                        col[d <= 3.0], glow[d <= 3.0] = c * 0.5, 0.6
                elif kind == "slit":
                    m = (np.abs(v - .40) * H < 1.0) & (np.abs(u - .5) < .34)
                    col[m], glow[m] = c, 1.0
                elif kind == "chevron":
                    m = (np.abs(v - (.34 + 0.35 * np.abs(u - .5))) * H < 1.2) & (np.abs(u - .5) < .36)
                    col[m], glow[m] = c, 1.0
                else:
                    for vy in (.36, .46):
                        m = (np.abs(v - vy) * H < 0.9) & (np.abs(u - .5) < .30 - (vy - .36))
                        col[m], glow[m] = c, 1.0
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
    elif mat == "chainmail":  # dim ice track, one soft wide pulse
        g = 0.30 + 0.9 * np.exp(-(dpk / 0.10) ** 2)
        col = rgb(205, 232, 255)[None] * np.clip(g, 0, 1)[:, None]
    elif mat == "iron":       # cobalt packet with a tail
        tail = np.where(x < 0.25, np.exp(-(x / 0.10)) * 0.5, 0)
        g = 0.55 + 1.2 * np.exp(-(dpk / 0.05) ** 2) + tail
        col = rgb(70, 140, 255)[None] * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 0.9, 0, 1)[:, None] * 0.9
    elif mat == "golden":     # fire: flickering segments, a hot core
        rng = np.random.default_rng(5)
        fl = np.repeat(rng.random(32), H // 32)
        fl = np.convolve(fl, np.ones(5) / 5, "same")
        g = 0.45 + 0.55 * fl + 0.8 * np.exp(-(dpk / 0.05) ** 2)
        col = rgb(255, 92, 18)[None] * np.clip(g, 0, 1)[:, None] + rgb(255, 210, 110)[None] * np.clip(g - .8, 0, 1)[:, None]
    elif mat == "netherite":  # violet track, twin packets
        x2 = (v * 2 - 0.25) % 0.5
        d2 = np.minimum(x2, 0.5 - x2)
        g = 0.35 + 1.1 * np.exp(-(d2 / 0.045) ** 2)
        col = rgb(178, 84, 255)[None] * np.clip(g, 0, 1)[:, None] + rgb(255, 200, 255)[None] * np.clip(g - 1, 0, 1)[:, None] * 2
    elif mat == "diamond":    # prism: the spectrum runs along the rail, a white-hot packet
        hue = (v * 2) % 1.0
        spec = hsv(hue, np.full(H, 0.75), np.full(H, 1.0)) * 255
        g = 0.70 + 1.3 * np.exp(-(dpk / 0.04) ** 2)
        col = spec * np.clip(g, 0, 1)[:, None] + 255 * np.clip(g - 1, 0, 1)[:, None]
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
