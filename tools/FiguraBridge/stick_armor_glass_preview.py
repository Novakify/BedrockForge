#!/usr/bin/env python3
"""Stick armor x Novak Glass: an offline frame of what Renderer Foundation's glass armor pass draws on the stick.

The plates are rasterised at in-game screen scale over a real in-game screenshot, then shaded per pixel with a numpy
port of PSGlassArmor (worldfx/rf shaders/glass_armor.hlsl, the non-Cloud-Sea path) at the live settings:
fusedK 0, filmK 1, reflect 0.45, swirl 0.9, ripple 0.09, lens 0.8, no LyricsForge (edgeLv 1), Novak on (pad4 1),
pad3 = match progress (0 start "aero" .. 1 endgame "neon"), uiScale = res.y / 760.

Everything the shader reads from the texture side comes from the same data RF compiles in (masks, faces, roles of
Mojang's armor textures), so outline placement, facets and bevels are exactly where RF puts them. Approximations
(orthographic camera, flat-lit stick, scene copy = the screenshot + stick, previous frame = the same) are noted in
the README. Data: tools/FiguraBridge/glass_data.json (from RF's glass_armor_data.hlsli).

usage: stick_armor_glass_preview.py STICK_GEO RP_DIR BACKGROUND.jpg out.png [materials=diamond,...] [pad3=0,1]
       [scale=8] [views=0,35,180] [zoom=2] [anchor=0.5,0.86]
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).parent))
from stick_armor_preview import pose, tris_of   # noqa: E402

DATA = json.loads((Path(__file__).parent / "glass_data.json").read_text())
ORDER = ["leather", "chain", "iron", "copper", "gold", "diamond", "netherite"]
MAT = {"turtle": "leather", "leather": "leather", "chainmail": "chain", "iron": "iron", "copper": "copper", "golden": "gold",
       "diamond": "diamond", "netherite": "netherite"}
LOOK = {"leather": ("#8b5a2b", "#ff9a3c"), "chain": ("#9aa3ad", "#c9d8ea"), "iron": ("#dfe6f0", "#eef4ff"),
        "copper": ("#e0784a", "#ff7a3d"), "gold": ("#ffc83a", "#ffd23a"), "diamond": ("#4ff3ff", "#38f2ff"),
        "netherite": ("#4a3560", "#b14cff")}
LEATHER_DYE = np.array([0xA0, 0x65, 0x40]) / 255.0   # undyed leather (the game's default ChangeColor)
SLOT_LAYER = {"head": 1, "chest": 1, "legs": 2, "feet": 1}
SWIRL, RIPPLE, LENS, FILMK, REFLECT = 0.9, 0.09, 0.8, 1.0, 0.45


def lin(h):
    def c(x):
        x /= 255.0
        return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
    return np.array([c(int(h[i:i + 2], 16)) for i in (1, 3, 5)])


MASK = np.array([np.array(DATA["masks"][f"{m}_{l}"], bool) for m in ORDER for l in (1, 2)])   # [14,32,64]
ROLE = np.array([np.array(DATA["roles"][f"{m}_{l}"], int) for m in ORDER for l in (1, 2)])
FACES = {1: np.array(DATA["faces"]["1"]), 2: np.array(DATA["faces"]["2"])}
FIDX = {}
for L, F in FACES.items():
    a = np.full((32, 64), -1, int)
    for i, (x, y, w, h) in reversed(list(enumerate(F))):
        a[y:y + h, x:x + w] = i
    FIDX[L] = a


def shape_of(variant):   # GA_SHAPE: chain is drawn on iron's shape
    return variant + 2 if variant // 2 == 1 else variant


def painted(shape, bx, by):
    ok = (bx >= 0) & (by >= 0) & (bx < 64) & (by < 32)
    out = np.zeros(bx.shape, bool)
    out[ok] = MASK[shape[ok], by[ok], bx[ok]]
    return out


def face_of(layer, bx, by):
    ok = (bx >= 0) & (by >= 0) & (bx < 64) & (by < 32)
    out = np.full(bx.shape, -1, int)
    for L in (1, 2):
        k = ok & (layer == L)
        out[k] = FIDX[L][by[k], bx[k]]
    return out


def d_in(t, shape, layer):
    """distance (texels) to the nearest edge of the painted area / of this cube face, and the face index"""
    b = np.floor(t).astype(int)
    fi = face_of(layer, b[:, 0], b[:, 1])
    d = np.ones(len(t))
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            nx, ny = b[:, 0] + dx, b[:, 1] + dy
            same = painted(shape, nx, ny) & (face_of(layer, nx, ny) == fi)
            q = np.abs(t - np.c_[nx + .5, ny + .5]) - .5
            dist = np.linalg.norm(np.maximum(q, 0), axis=1)
            d = np.where(same, d, np.minimum(d, dist))
    return d, fi


def ga_hash(q):
    return np.modf(np.sin(q[..., 0] * 127.1 + q[..., 1] * 311.7) * 43758.5453)[0] % 1.0


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def voronoi(cg):
    ci, cf = np.floor(cg), cg - np.floor(cg)
    d1, d2 = np.full(len(cg), 9.0), np.full(len(cg), 9.0)
    cid = ci.copy()
    for yy in (-1, 0, 1):
        for xx in (-1, 0, 1):
            o = np.array([xx, yy], float)
            hh = np.c_[ga_hash(ci + o), ga_hash(ci + o + 17.3)]
            r = o + 0.15 + 0.7 * hh - cf
            dd = (r * r).sum(1)
            near = dd < d1
            d2 = np.where(near, d1, np.minimum(d2, dd))
            cid = np.where(near[:, None], ci + o, cid)
            d1 = np.where(near, dd, d1)
    return np.sqrt(d2) - np.sqrt(d1), d1, cid


def ga_hue(h):
    return np.clip(np.abs(((h[:, None] + np.array([0, .333, .667])) % 1.0) * 6 - 3) - 1, 0, 1)


# ---------------------------------------------------------------------------------------------- raster
def raster(items, az, W, H, scale, anchor, z0=None):
    """items: (tris world, uvs, kind, payload). kind 'body' payload rgb; kind 'armor' payload (variant, layer).
    Returns z, kind, colour, t, J (dt/dpx, dt/dpy), variant, layer, dist-to-camera buffers"""
    a = math.radians(az)
    view = np.array([math.sin(a), 0, -math.cos(a)])
    right = np.array([math.cos(a), 0, math.sin(a)])
    L = np.array([-0.4, 0.75, -0.55]) / np.linalg.norm([-0.4, 0.75, -0.55])
    z = np.full((H, W), -1e9) if z0 is None else z0.copy()
    kind = np.zeros((H, W), int)
    col = np.zeros((H, W, 3))
    T = np.zeros((H, W, 2))
    J = np.zeros((H, W, 4))
    var = np.zeros((H, W), int)
    lay = np.zeros((H, W), int)
    ax, ay = anchor[0] * W, anchor[1] * H
    for P, UV, k, pay in items:
        q = np.c_[P @ right * scale + ax, ay - P[:, 1] * scale, P @ view]
        x0, x1 = max(0, int(q[:, 0].min())), min(W - 1, int(q[:, 0].max()) + 1)
        y0, y1 = max(0, int(q[:, 1].min())), min(H - 1, int(q[:, 1].max()) + 1)
        if x0 > x1 or y0 > y1:
            continue
        den = (q[1, 1] - q[2, 1]) * (q[0, 0] - q[2, 0]) + (q[2, 0] - q[1, 0]) * (q[0, 1] - q[2, 1])
        if abs(den) < 1e-9:
            continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        l0 = ((q[1, 1] - q[2, 1]) * (X - q[2, 0]) + (q[2, 0] - q[1, 0]) * (Y - q[2, 1])) / den
        l1 = ((q[2, 1] - q[0, 1]) * (X - q[2, 0]) + (q[0, 0] - q[2, 0]) * (Y - q[2, 1])) / den
        l2 = 1 - l0 - l1
        zz = l0 * q[0, 2] + l1 * q[1, 2] + l2 * q[2, 2]
        old = z[y0:y1 + 1, x0:x1 + 1]
        m = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6) & (zz > old)
        if k == "armor":
            tex = np.array([64.0, 32.0])
            tt = (l0[..., None] * UV[0] + l1[..., None] * UV[1] + l2[..., None] * UV[2]) * tex
            v, layer = pay
            b = np.floor(tt).astype(int)
            m &= painted(np.full(b.shape[:2], shape_of(v)), b[..., 0], b[..., 1])   # discard (vanilla shape)
            # screen -> texel Jacobian of this (planar) triangle
            M = np.array([[q[1, 0] - q[0, 0], q[2, 0] - q[0, 0]], [q[1, 1] - q[0, 1], q[2, 1] - q[0, 1]]])
            Tm = np.array([(UV[1] - UV[0]) * tex, (UV[2] - UV[0]) * tex]).T   # columns: dt per barycentric
            try:
                Jt = Tm @ np.linalg.inv(M)   # dt = Jt @ dpx
            except np.linalg.LinAlgError:
                continue
        if k == "tex":   # metal atlas (lit) / light rail strip (unlit, emissive)
            tex, unlit, NN = pay
            th_, tw_ = tex.shape[:2]
            U = l0 * UV[0, 0] + l1 * UV[1, 0] + l2 * UV[2, 0]
            Vv = l0 * UV[0, 1] + l1 * UV[1, 1] + l2 * UV[2, 1]
            tc = tex[np.clip((Vv * th_).astype(int), 0, th_ - 1), np.clip((U * tw_).astype(int), 0, tw_ - 1)]
            if tex.shape[2] == 4 and unlit is False:
                m &= tc[..., 3] >= 0.5
        if not m.any():
            continue
        old[m] = zz[m]
        sl = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        kind[sl][m] = 1 if k == "body" else 2
        if k == "tex":
            nn = l0[..., None] * NN[0] + l1[..., None] * NN[1] + l2[..., None] * NN[2]   # smooth: per-pixel normal
            nn /= np.linalg.norm(nn, axis=-1, keepdims=True) + 1e-12
            nv = nn @ view
            nn = np.where((nv < 0)[..., None], -nn, nn)
            sh = np.ones(nv.shape) if unlit is True else 0.50 + 0.42 * np.maximum(0, nn @ L) + 0.22 * (1 - np.abs(nv)) ** 2
            if unlit == "mask":   # entity_emissive_alpha: alpha 0 = full glow, 1 = lit
                sh = sh * tc[..., 3] + (1 - tc[..., 3])
            kind[sl][m] = 1
            col[sl][m] = np.clip(tc[..., :3][m] * sh[m][:, None], 0, 1)
        elif k == "body":
            n = np.cross(P[1] - P[0], P[2] - P[0])
            n /= np.linalg.norm(n) + 1e-12
            if n @ view < 0:
                n = -n
            shade = 0.72 + 0.28 * max(0, n @ L)
            col[sl][m] = np.array(pay) * shade
        else:
            T[sl][m] = tt[m]
            J[sl][m] = Jt.reshape(4)          # [dtx/dx, dtx/dy, dty/dx, dty/dy]
            var[sl][m] = pay[0]
            lay[sl][m] = pay[1]
    return z, kind, col, T, J, var, lay


# ---------------------------------------------------------------------------------------------- shade
def shade(frame, scene_dist, z, kind, T, J, var, lay, pad3, cam_d, time=0.0):
    H, W = kind.shape
    res = np.array([W, H], float)
    S = frame.shape[0] / 760.0
    ys, xs = np.nonzero(kind == 2)
    if not len(xs):
        return frame
    t = T[ys, xs]
    Jx = np.c_[J[ys, xs, 0], J[ys, xs, 2]]   # ddx(t)
    Jy = np.c_[J[ys, xs, 1], J[ys, xs, 3]]   # ddy(t)
    v = var[ys, xs]
    shp = np.where(v // 2 == 1, v + 2, v)
    L = lay[ys, xs]
    b = np.floor(t).astype(int)
    dIn, fi = d_in(t, shp, L)
    dX, _ = d_in(t + Jx, shp, L)
    dY, _ = d_in(t + Jy, shp, L)
    fr = np.where((L == 1)[:, None], FACES[1][np.clip(fi, 0, 29)], FACES[2][np.clip(fi, 0, 11)])
    fs, p = fr[:, 2:].astype(float), t - fr[:, :2]

    dj = Jx[:, 0] * Jy[:, 1] - Jy[:, 0] * Jx[:, 1]
    ppt = 1 / np.sqrt(np.maximum(np.abs(dj), 1e-8))
    d = -dIn * ppt
    gr = np.c_[-(dX - dIn), -(dY - dIn)]
    n = gr / np.maximum(np.linalg.norm(gr, axis=1), 1e-6)[:, None]
    halfPx = np.minimum(fs[:, 0], fs[:, 1]) * 0.5 * ppt
    edgeLv = np.ones(len(t))

    mat = np.array([ORDER[i // 2] for i in v])
    baseEdge = np.array([lin(LOOK[m][1]) for m in mat])
    baseTint = np.array([lin(LOOK[m][0]) for m in mat])
    dyeK = np.zeros(len(t))
    leather = mat == "leather"
    if leather.any():
        dye = LEATHER_DYE
        dv = dye / dye.max()
        dv = np.clip(np.dot(dv, [.299, .587, .114]) + (dv - np.dot(dv, [.299, .587, .114])) * 1.3, 0, 1)
        baseEdge[leather], baseTint[leather], dyeK[leather] = dv, dye, 1
    ngK = smoothstep(0.30, 0.95, pad3)
    edgeC = baseEdge.copy()
    aeroE = ((np.array([0.90, 0.97, 1.00]) * 0.65 + edgeC * 0.35)) * 0.80
    neonE = np.clip(edgeC * 1.35, 0, 1) * (1.45 + 0.30 * math.sin(time * 2.2))
    edgeC = aeroE * (1 - ngK) + neonE * ngK

    bevel = np.minimum(22 * S, halfPx * 0.85)
    e = (1 - np.clip(-d / np.maximum(bevel, 1), 0, 1)) ** 2.2
    rel = p - fs * 0.5
    rr = np.linalg.norm(rel, axis=1) / np.maximum(np.minimum(fs[:, 0], fs[:, 1]) * 0.5, 1e-3)
    ph = np.modf(np.sin(fr[:, 0] * 12.9898 + fr[:, 1] * 78.233) * 43758.5)[0] % 1.0
    ang = SWIRL * smoothstep(1.15, 0.0, rr) * np.where(ph > 0.5, 1, -1)
    cs, sn = np.cos(ang), np.sin(ang)
    fo = np.c_[cs * rel[:, 0] - sn * rel[:, 1], sn * rel[:, 0] + cs * rel[:, 1]] - rel
    fo += RIPPLE * np.c_[np.sin(p[:, 1] * 0.9 + ph * 6), np.cos(p[:, 0] * 0.9 + ph * 4)]
    sdj = np.where(np.abs(dj) > 1e-8, dj, 1)
    off = np.c_[Jy[:, 1] * fo[:, 0] - Jy[:, 0] * fo[:, 1], -Jx[:, 1] * fo[:, 0] + Jx[:, 0] * fo[:, 1]] / sdj[:, None]
    off = np.clip(off, -42 * S, 42 * S)
    off += -n * 16 * LENS * S * e[:, None]

    cg = p / 2.4 + np.c_[fi * 3.7, fi * 1.3]
    cgap, d1, cid = voronoi(cg)
    cgX, _, _ = voronoi(cg + Jx / 2.4)
    cgY, _, _ = voronoi(cg + Jy / 2.4)
    fw = np.abs(cgX - cgap) + np.abs(cgY - cgap)
    cutLn = 1 - smoothstep(0, np.maximum(fw * 1.3, 0.035), cgap)
    cellPx = 1 / np.maximum(np.maximum(np.linalg.norm(Jx, axis=1), np.linalg.norm(Jy, axis=1)) / 2.4, 1e-4)
    facetK = smoothstep(5, 12, cellPx)
    cutLn *= facetK
    fh1, fh2 = ga_hash(cid + 5.1), ga_hash(cid + 9.7)
    tilt = np.c_[fh1, fh2] * 2 - 1
    off += tilt * 13 * S * (1 - 0.5 * e)[:, None]

    px = np.c_[xs + .5, ys + .5]
    # your own body right under the glass stays clean: only bend what is further behind
    selfd = cam_d - z[ys, xs]

    def sd(q):
        qi = np.clip(np.round(q - .5).astype(int), [0, 0], [W - 1, H - 1])
        return scene_dist[qi[:, 1], qi[:, 0]]
    ratio = np.minimum(sd(px), sd(px + off)) / selfd
    off *= smoothstep(1.03, 1.18, ratio)[:, None]

    def at(q, ch=None):
        qi = np.clip(q, [0, 0], [W - 1.001, H - 1.001])
        x0, y0 = np.floor(qi[:, 0] - .5).astype(int).clip(0, W - 2), np.floor(qi[:, 1] - .5).astype(int).clip(0, H - 2)
        fx, fy = (qi[:, 0] - .5 - x0).clip(0, 1)[:, None], (qi[:, 1] - .5 - y0).clip(0, 1)[:, None]
        s = (frame[y0, x0] * (1 - fx) + frame[y0, x0 + 1] * fx) * (1 - fy) + \
            (frame[y0 + 1, x0] * (1 - fx) + frame[y0 + 1, x0 + 1] * fx) * fy
        return s if ch is None else s[:, ch]
    c = np.c_[at(px + off, 0), at(px + off * 1.04, 1), at(px + off * 1.08, 2)]
    tintc = np.array([0.0065, 0.0032, 0.018]) * (1 - (0.35 + 0.25 * dyeK))[:, None] + baseTint * (0.35 + 0.25 * dyeK)[:, None]
    k = (0.10 + 0.06 * dyeK)[:, None]
    c = c * (1 - k) + tintc * k

    hueC = baseEdge / np.maximum(baseEdge.max(1), 1e-3)[:, None]
    Lc = np.array([-0.45, -0.89]) / np.linalg.norm([-0.45, -0.89])
    fl = tilt @ Lc
    c *= (1 + ((0.86 + 0.26 * (0.5 + 0.5 * fl)) - 1) * (0.35 + 0.65 * facetK))[:, None]
    c += hueC * (0.05 * (0.5 + 0.5 * fl))[:, None]
    ngL = 1 + max(ngK, 0)
    c += (0.55 + 0.45 * hueC) * (cutLn * (0.16 + 0.14 * (0.5 + 0.5 * fl)) * ngL)[:, None]
    spk = np.clip(np.sin(fh1 * 60 + (px[:, 0] + px[:, 1]) / res[0] * 7 + time * 0.5), 0, 1) ** 40
    c += (0.7 + 0.3 * hueC) * (spk * smoothstep(0.30, 0.0, np.sqrt(d1)) * 0.55 * ngL)[:, None]

    gm = v // 2
    film = (e * 1.6 + px[:, 0] / res[0] * 0.9 - px[:, 1] / res[1] * 0.6 + time * 0.035 + fi * 0.11) % 1.0
    role = ROLE[v, np.clip(b[:, 1], 0, 31), np.clip(b[:, 0], 0, 63)]
    c += ga_hue(film) * ((0.03 + 0.14 * e) * FILMK * np.where((gm == 1) | (role == 0), 0.35, 1.0))[:, None]

    rp = px - off * 1.6 + n * (24 * S)
    refl = (at(rp) + at(rp + np.array([3, 0]) * S) + at(rp + np.array([0, 3]) * S)) / 3
    lumR = refl @ [.299, .587, .114]
    w = REFLECT * (0.10 + 0.55 * e) * smoothstep(0.25, 0.85, lumR + 0.25)
    c = 1 - (1 - c) * (1 - refl * w[:, None])

    rim = 1 - smoothstep(0, 1.6, np.abs(d + 0.9))
    lit = 0.45 + 0.55 * np.maximum(n @ Lc, 0)
    c += 0.9 * (rim * lit * 0.75 + e * 0.08)[:, None]
    sat = baseEdge.max(1) - baseEdge.min(1)
    bv = np.clip(1 + d / np.maximum(bevel, 1), 0, 1)
    c += edgeC * (0.24 * edgeLv * (0.45 + 0.55 * np.clip(sat * 1.5, 0, 1)) * bv ** 3)[:, None]
    c += edgeC * 0.015
    aa = np.maximum(np.abs(dX - dIn) + np.abs(dY - dIn), 1e-4) * 1.1
    lw = 0.19 + 0.06 * max(ngK, 0)
    ln = 1 - smoothstep(lw - aa, lw + aa, dIn)
    if ngK >= 0:
        c += edgeC * (0.16 * ngK * bv ** 2)[:, None]
        hueV = hueC
        c = c * (1 - 0.6 * ngK) + c * (1 + (hueV - 1) * 0.35) * 0.6 * ngK + hueV * (0.07 + 0.05 * e)[:, None] * ngK
        edgeLv = np.maximum(edgeLv, ngK)
    c = c * (1 - (ln * np.clip(edgeLv, 0, 1))[:, None]) + edgeC * 1.25 * (ln * np.clip(edgeLv, 0, 1))[:, None]
    out = frame.copy()
    out[ys, xs] = np.clip(c, 0, 1)
    return out


# ---------------------------------------------------------------------------------------------- main
def tris_n(pm):
    """triangles of a poly_mesh with their per-corner normals"""
    pos, uv, nr = np.array(pm["positions"], float), np.array(pm["uvs"], float), np.array(pm["normals"], float)
    out = []
    for poly in pm["polys"]:
        tri = [[0, 1, 2]] + ([[0, 2, 3]] if poly[2] != poly[3] else [])
        for t in tri:
            c = [poly[i] for i in t]
            out.append((pos[[x[0] for x in c]], uv[[x[2] for x in c]], nr[[x[1] for x in c]]))
    return out


def rot_n(N, bone, az):   # normals turn with the pose (no translation)
    return pose(N + np.array([0, 100.0, 0]), bone, az) - pose(np.array([[0, 100.0, 0]] * len(N)), bone, az)


def load_items(geo_path, rp, mat, az, slots, phase=0.3):
    g = json.loads(Path(geo_path).read_text())["minecraft:geometry"][0]
    items = []
    for bn in g["bones"]:
        if "poly_mesh" in bn and bn["name"] in ("p17", "p22", "p30", "p32", "p37", "p39", "p44", "p46", "p48", "p52",
                                                 "p54", "p56"):
            for P, UV in tris_of(bn["poly_mesh"]):
                items.append((pose(P, bn["name"], az), UV, "body", (0.42, 0.80, 0.16)))
    A = json.loads((Path(rp) / "models/entity/fb_armor_stick.geo.json").read_text())["minecraft:geometry"]
    vi = ORDER.index(MAT[mat])
    td = Path(rp) / "textures/figura/fb_armor"
    tiers = any(geo["description"]["identifier"].count(".") == 4 for geo in A)   # stick_tiers: one suit per material
    if tiers:
        sys.path.insert(0, str(Path(__file__).parent))
        import stick_tiers
        rf = stick_tiers.TIERS[mat]["rf"]

    def load(name, dye=False):
        t = np.asarray(Image.open(td / name).convert("RGBA"), float) / 255
        return t * np.r_[LEATHER_DYE, 1.0] if dye and mat == "leather" else t
    for geo in A:
        parts = geo["description"]["identifier"].split(".")
        if tiers and parts[-1] != mat:
            continue
        ident = parts[-2] if tiers else parts[-1]
        slot, _, part = ident.partition("_")
        if slot not in slots:
            continue
        layer = SLOT_LAYER[slot]
        if part in ("metal", "fx") or (tiers and not rf):
            if tiers:
                tex = load({"metal": f"t_{mat}.png", "fx": f"t_{mat}_fx.png", "": f"t_{mat}_pane{layer}.png"}[part],
                           dye=part != "fx")
                emissive = "mask"
            else:
                tex = load(f"{mat}.png" if part == "metal" else f"{mat}_fx.png", dye=part == "metal")
                emissive = part == "fx"
            if part == "fx":   # uv_anim: v offset (v.fbaph * 0.5)
                tex = np.roll(tex, -int(round(phase * 0.5 * tex.shape[0])), axis=0)
            for bn in geo["bones"]:
                if "poly_mesh" in bn:
                    for P, UV, N in tris_n(bn["poly_mesh"]):
                        items.append((pose(P, bn["name"], az), UV, "tex", (tex, emissive, rot_n(N, bn["name"], az))))
            continue
        for bn in geo["bones"]:
            if "poly_mesh" in bn:
                for P, UV in tris_of(bn["poly_mesh"]):
                    items.append((pose(P, bn["name"], az), UV, "armor", (vi * 2 + layer - 1, layer)))
    return items


def frame_for(geo, rp, bg, mat, az, pad3, scale, anchor, slots, cam_d=64.0):
    H, W = bg.shape[:2]
    items = load_items(geo, rp, mat, az, slots)
    # the scene copy RF refracts: the world and the stick, drawn before the armor (opaque pass order)
    zb, kb, cb, *_ = raster([i for i in items if i[2] != "armor"], az, W, H, scale, anchor)
    z, kind, col, T, J, var, lay = raster([i for i in items if i[2] == "armor"], az, W, H, scale, anchor, z0=zb)
    frame = bg.copy()
    frame[kb == 1] = cb[kb == 1]
    dist = np.full((H, W), 320.0)                       # the world behind: blocks well behind the player
    dist[kb == 1] = cam_d - zb[kb == 1]
    # scene copy for the glass = world + body + armor plates drawn before (approximation: world + body)
    return shade(frame, dist, z, kind, T, J, var, lay, pad3, cam_d)


def main():
    geo, rp, bgp, png = sys.argv[1:5]
    o = dict(a.split("=") for a in sys.argv[5:])
    mats = o.get("materials", "diamond").split(",")
    pads = [float(x) for x in o.get("pad3", "0,1").split(",")]
    scale = float(o.get("scale", "8"))
    views = [float(x) for x in o.get("views", "0,35,180").split(",")]
    zoom = int(o.get("zoom", "2"))
    anchor = [float(x) for x in o.get("anchor", "0.5,0.86").split(",")]
    slots = o.get("slots", "head,chest,legs,feet").split(",")
    crop = [int(x) for x in o.get("crop", "300,620").split(",")]   # w,h around the stick (screen px)
    bg = np.asarray(Image.open(bgp).convert("RGB").resize((1920, 1080), Image.LANCZOS), float) / 255.0
    tiles = []
    for mat in mats:
        for pad3 in pads:
            for az in views:
                f = frame_for(geo, rp, bg, mat, az, pad3, scale, anchor, slots)
                cx, cy = int(anchor[0] * 1920), int(anchor[1] * 1080)
                im = Image.fromarray((f * 255).astype(np.uint8)).crop(
                    (cx - crop[0] // 2, cy - crop[1] + 40, cx + crop[0] // 2, cy + 40))
                im = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST if zoom > 1 else Image.LANCZOS)
                tiles.append((f"{mat} {'start' if pad3 < .5 else 'endgame'} {int(az)}°", im))
    cols = len(views) * len(pads)
    tw, th = tiles[0][1].size
    sheet = Image.new("RGB", (tw * cols, (th + 30) * len(mats)), (12, 12, 16))
    dr = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    for i, (lab, im) in enumerate(tiles):
        x, y = (i % cols) * tw, (i // cols) * (th + 30)
        sheet.paste(im, (x, y + 30))
        dr.text((x + 8, y + 6), lab, font=font, fill=(230, 230, 240))
    sheet.save(png)


if __name__ == "__main__":
    main()
