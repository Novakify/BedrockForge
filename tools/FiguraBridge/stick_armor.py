#!/usr/bin/env python3
"""Sculpted, animated armor for the FiguraBridge stick figure (v2: poly_mesh, approved "light rail" design).

Vanilla armor draws on the vanilla player bones, which an avatar hides (attachables/fb_armor), so the stick wears armor
of its own: tapered, layered plates that hug each rod, on geometries that copy the stick's exact bone hierarchy (same
names, parents, pivots, rotations - no new bones). The player entity draws them with its own render controllers, so the
Figura drive animation that moves the stick moves the armor the same way.

Per equipment slot two geometries:
  geometry.fb_armor.stick.<slot>     the plates (static metal atlas, Material.fbam = entity_alphatest)
  geometry.fb_armor.stick.<slot>_fx  raised light rails (energy strip, Material.fbamfx = figura_uv_emissive with
                                     USE_UV_ANIM); the render controller's uv_anim scrolls ONLY these, so the metal
                                     texture never moves. Rails sit LIFT px above their plate - never coplanar.
A light packet travels chest V -> shoulders -> down the forearms every 2.4 s (1.2 s sprinting, eased), legs/greaves
0.3 s later, helmet cheek rails slightly ahead. Equipping a piece lights its rails in sequence (chest, legs, feet,
visor over ~0.55 s). While a helmet is worn the avatar's outline disc (p6) is hidden; it returns when unequipped.

Material per slot from what the player wears (query.is_item_name_any), index order kept: leather, chainmail, iron,
golden, diamond, netherite, copper, turtle. Only on the avatar's player (v.fbnoarmor), only while the stick is the
installed avatar (v.fbstickarmor is written into its player.entity.json only).

Glass style (STYLE = "glass", the default since 2026-11-16): the plates wear Renderer Foundation's Novak Glass /
Cloud Sea armor instead of the metal atlas - see the "glass style" notes below. One geometry per slot, no light rails;
build_suit_glass() is the suit. Judge it with stick_armor_glass_preview.py (an offline port of RF's glass shader).

usage: stick_armor.py STICK_GEO [PLAYER_ENTITY [FIGURA_RC]] OUT_DIR
  STICK_GEO      figura_stick.geo.json (installed) or stick_skeleton.json
  PLAYER_ENTITY  optional: patched idempotently into OUT_DIR/entity/player.entity.json
  FIGURA_RC      optional: figura_player.render_controllers.json, p6 visibility patched (idempotent)
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

SLOTS = ("head", "chest", "legs", "feet")
# name, body, dark (recess), highlight (bevel), light accent
MATERIALS = [
    ("leather", (104, 66, 40), (46, 28, 18), (168, 118, 74), (255, 214, 150)),
    ("chainmail", (82, 86, 96), (30, 32, 38), (170, 176, 190), (226, 238, 255)),
    ("iron", (150, 156, 168), (52, 58, 70), (236, 240, 248), (205, 246, 255)),
    ("golden", (220, 168, 46), (112, 72, 12), (255, 234, 140), (255, 246, 205)),
    ("diamond", (18, 84, 160), (6, 26, 62), (96, 206, 255), (170, 245, 255)),
    ("netherite", (64, 54, 78), (22, 18, 30), (162, 116, 214), (228, 180, 255)),
    ("copper", (188, 104, 66), (100, 50, 30), (240, 164, 114), (255, 222, 182)),
    ("turtle", (62, 146, 68), (26, 74, 32), (130, 210, 110), (204, 255, 170)),
]
ITEMS = {
    "head": ["leather_helmet", "chainmail_helmet", "iron_helmet", "golden_helmet", "diamond_helmet", "netherite_helmet",
             "copper_helmet", "turtle_helmet"],
    "chest": ["leather_chestplate", "chainmail_chestplate", "iron_chestplate", "golden_chestplate", "diamond_chestplate",
              "netherite_chestplate", "copper_chestplate"],
    "legs": ["leather_leggings", "chainmail_leggings", "iron_leggings", "golden_leggings", "diamond_leggings",
             "netherite_leggings", "copper_leggings"],
    "feet": ["leather_boots", "chainmail_boots", "iron_boots", "golden_boots", "diamond_boots", "netherite_boots",
             "copper_boots"],
}
SLOT_NAME = {s: f"slot.armor.{s}" for s in SLOTS}
VAR = {"head": "fbah", "chest": "fbac", "legs": "fbal", "feet": "fbaf"}
# equip startup: seconds after equipping before that slot's rails light (chest, legs, feet, then visor)
START = {"chest": 0.0, "legs": 0.18, "feet": 0.36, "head": 0.55}
PERIOD, SPRINT_PERIOD = 2.4, 1.2
TEX = 64                    # metal atlas size
FXW, FXH = 4, 128           # energy strip size
LIFT = 0.06                 # rail height above its plate (px)

# atlas regions (normalized u0, u1)
REG = {"metal": (0.0, 0.5), "visor": (0.5, 0.75), "rim": (0.75, 0.875), "recess": (0.875, 1.0)}

# ---------------------------------------------------------------------- glass style (default)
# The plates bind the game's own vanilla armor texture (64x32 layout). Renderer Foundation's glass armor pass recognises
# the draw by that texture and draws its Novak Glass / Cloud Sea crystal glass instead - the same pass as the regular
# glass armor. RF does NOT read the texture's colours for the glass: it reads only WHERE the vanilla texture is painted
# and which cube face each texel belongs to, and draws the neon outline, the rim light and the lens bevel along the
# edges of each painted face area (glass_armor.hlsl: painted(), faceOf(), dIn).
#
# So the look of every plate is decided by its UVs (audit 2026-11-16, offline port of the shader):
#   * a plate must cover ONE "clean island" edge to edge - a rectangle that is painted in every material and whose
#     border is a real edge in the shader (unpainted or another face outside it). Then the whole silhouette of the
#     plate gets the outline, rim and bevel, like each cube face of the regular glass armor.
#   * a plate mapped to the inside of a face gets no outline at all (v1: plates sampled half a texel inside sub-rects
#     of faces: no outline anywhere, the glass read as a faint smear), and holes/edges in the middle of the art put
#     outline fragments at random places across the plate.
#   * about one texel per model px (vanilla's density): the outline width and facet size are in texels.
STYLE = "tiers"   # "tiers" (default): a design per material (stick_tiers.py); "mix" (default): metal structure + glass faces + light rails; "glass": all glass; "metal": original
# vanilla file stem per material (index order of MATERIALS); layer 1 = helmet/chestplate/boots, 2 = leggings
VANILLA = {"leather": "cloth", "chainmail": "chain", "iron": "iron", "golden": "gold", "diamond": "diamond",
           "netherite": "netherite", "copper": "copper", "turtle": "turtle"}
LAYER = {"head": 1, "chest": 1, "legs": 2, "feet": 1}
# clean islands (x, y, w, h texels), checked against RF's masks + faces for leather, iron, copper, gold, diamond and
# netherite (chainmail is drawn on iron's shape) by test_stick_glass.py
ISLAND = {
    "crown": (8, 0, 8, 8),        # layer 1: helmet top face
    "side_r": (16, 20, 4, 9),     # layer 1: chestplate right side
    "side_l": (28, 20, 4, 9),     # layer 1: chestplate left side
    "arm_top": (44, 16, 4, 4),    # layer 1: sleeve top
    "leg_end": (8, 16, 4, 4),     # layer 1: boot sole
    "boot": (4, 26, 4, 6),        # layer 1: boot front, lower half
    "back_low": (32, 27, 8, 5),   # layer 2: leggings belt back, lower part
    "knee": (4, 16, 4, 4),        # layer 2: leg top
    "leg_a": (0, 20, 4, 9), "leg_b": (4, 20, 4, 9), "leg_c": (8, 20, 4, 9), "leg_d": (12, 20, 4, 9),   # layer 2 legs
}
ISLAND_LAYER = {k: (2 if k in ("back_low", "knee", "leg_a", "leg_b", "leg_c", "leg_d") else 1) for k in ISLAND}
VISOR_BACK, VISOR_GLASS = 0.05, 0.12   # mix helmet: the dark visor backing and its glass pane above the metal shell (px)
# mix: rail light per material = Renderer Foundation's glass neon for that material (gen_glassarmor.py LOOK edge)
# (leather: warm white - the rails can't take the item's dye, and a fixed orange would fight a team colour)
RAIL_NEON = {"leather": (255, 236, 214), "chainmail": (201, 216, 234), "iron": (238, 244, 255), "golden": (255, 210, 58),
             "diamond": (56, 242, 255), "netherite": (177, 76, 255), "copper": (255, 122, 61), "turtle": (130, 210, 110)}
EDGE = 0.03   # texels: plate edge just inside the island (floor(t) must stay on a painted texel)
RIMBAND = 0.15  # texels: the plate's folded rim strip spans the outline band (< 0.19 = RF's line width) - a lit edge


def isl_uv(isl, u, v, mirror=False, inset=(0.0, 0.0)):
    """normalized uv of (u, v) in 0..1 across island `isl`, edge to edge; inset (texels) pulls u / v toward the middle"""
    x, y, w, h = ISLAND[isl]
    u = min(1.0, max(0.0, u))
    v = min(1.0, max(0.0, v))
    if mirror:
        u = 1 - u
    tx = x + EDGE + (w - 2 * EDGE) * u
    ty = y + EDGE + (h - 2 * EDGE) * v
    tx += inset[0] * (1 if u < 0.5 else -1)
    ty += inset[1] * (1 if v < 0.5 else -1)
    return (tx / 64.0, ty / 32.0)


def disc_to_square(x, y):
    """inverse concentric (Shirley-Chiu) map: unit disc -> [-1, 1]^2, the circle onto the square's border"""
    r = math.hypot(x, y)
    if r < 1e-12:
        return 0.0, 0.0
    phi = math.atan2(y, x)
    k = round(phi / (math.pi / 2))
    psi = phi - k * math.pi / 2                      # -pi/4 .. pi/4
    a, b = r, r * psi * 4 / math.pi
    c, s = [(1, 0), (0, 1), (-1, 0), (0, -1)][k % 4]
    return a * c - b * s, a * s + b * c


# ====================================================================== mesh accumulator
class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.polys = [], [], [], []
        self._p, self._n, self._u = {}, {}, {}

    def _idx(self, table, store, v, nd=4):
        k = tuple(round(float(x), nd) for x in v)
        i = table.get(k)
        if i is None:
            i = table[k] = len(store)
            store.append(list(k))
        return i

    def quad(self, P, uvs, n=None, vn=None):
        """P: 3 or 4 points, counter-clockwise seen from outside; one flat normal (facets), or vn: a normal per corner
        (smooth shading)"""
        P = [np.asarray(p, float) for p in P]
        if n is None:
            n = np.cross(P[1] - P[0], P[2] - P[0])
            if len(P) == 4:
                n = n + np.cross(P[2] - P[0], P[3] - P[0])
        L = np.linalg.norm(n)
        if L < 1e-9:
            return
        n = n / L
        nis = [self._idx(self._n, self.nrm, n)] * len(P) if vn is None else \
            [self._idx(self._n, self.nrm, np.asarray(v, float) / np.linalg.norm(v)) for v in vn]
        poly = [[self._idx(self._p, self.pos, p), ni, self._idx(self._u, self.uv, uv, 5)] for p, ni, uv in zip(P, nis, uvs)]
        if len(poly) == 3:
            poly.append(list(poly[2]))  # Bedrock poly_mesh quads; a repeated corner makes a triangle
        self.polys.append(poly)

    def tris(self):
        return sum(1 if p[2] == p[3] else 2 for p in self.polys)

    def json(self):
        return {"normalized_uvs": True, "positions": self.pos, "normals": self.nrm, "uvs": self.uv, "polys": self.polys}


MESH = {}   # (slot, layer, bone) -> Mesh


def M(slot, bone, layer="metal"):
    return MESH.setdefault((slot, layer, bone), Mesh())


# ====================================================================== rod frames
class Rod:
    """axis from top (s=0) to bottom (s=1). Angle a: 0 = front (-z), +90 = avatar's left (+x), 180 = back"""

    def __init__(self, P):
        P = np.asarray(P, float)
        c = P.mean(0)
        w, V = np.linalg.eigh(np.cov((P - c).T))
        d = V[:, -1]
        d = d if d[1] > 0 else -d
        t = (P - c) @ d
        self.top, self.bot = c + d * t.max(), c + d * t.min()
        self.L = float(np.linalg.norm(self.bot - self.top))
        self.d = (self.bot - self.top) / self.L           # pointing down the rod
        f = np.array([0, 0, -1.0]) - self.d * (-self.d[2])
        self.f = f / np.linalg.norm(f)
        self.x = np.cross(self.f, self.d)
        if self.x[0] < 0:
            self.x = -self.x
        rel = P - self.top
        self._s = rel @ self.d / self.L
        self._r = np.linalg.norm(rel - np.outer(rel @ self.d, self.d), axis=1)

    def clear(self, s0, s1):
        m = (self._s >= min(s0, s1) - .05) & (self._s <= max(s0, s1) + .05)
        return float(self._r[m].max()) if m.any() else float(self._r.max())

    def at(self, s, a, r):
        return self.top + self.d * (s * self.L) + r * (math.cos(a) * self.f + math.sin(a) * self.x)


def leaf(t, p=0.6, top=0.0):
    """width profile 0..1: pointed both ends (top=0) .. broad at the top end (top=1), always pointed at the bottom"""
    t = min(1.0, max(0.0, t))
    pointed = math.sin(math.pi * t) ** p
    broad = math.sin(math.pi * (0.5 + 0.5 * t)) ** p
    return pointed * (1 - top) + broad * top


DENS = []   # (slot, bone, island, (texels per model px across, along)) of every glass plate - reported in the stats


def density(S, isl):
    """texels per model px of a plate mapped onto island `isl`: across (k) at mid-length, along (t) down the middle"""
    x, y, w, h = ISLAND[isl]
    across = sum(float(np.linalg.norm(S(.5, -1 + 2 * (j + 1) / 16) - S(.5, -1 + 2 * j / 16))) for j in range(16))
    along = sum(float(np.linalg.norm(S((i + 1) / 16, 0) - S(i / 16, 0))) for i in range(16))
    return round(w / max(across, 1e-6), 2), round(h / max(along, 1e-6), 2)


def plate(slot, bone, rod, s0, s1, ac, W, gap=0.12, bulge=0.25, flare=0.0, prof=None, twist=0.0,
          ns=8, na=5, th=0.14, region="metal", rim=True, isl=None, mirror=False):
    """a curved plate wrapped round `rod`: centre angle ac (rad), half width W*prof(t) (rad), radius = rod clearance +
    gap, bulging `bulge` in the middle and flaring `flare` toward s0. Returns its surface S(t, k, dr), k in -1..1"""
    prof = prof or (lambda t: leaf(t))
    base = rod.clear(s0, s1) + gap
    glass = isl is not None   # a glass pane (vanilla texture island, RF's glass) or a metal plate (the metal atlas)
    m = M(slot, bone, "glass" if glass else "metal")

    def R(t, k):
        return base + bulge * math.sin(math.pi * t) * (1 - 0.35 * k * k) + flare * (1 - t) ** 2

    def S(t, k, dr=0.0):
        a = ac + twist * (t - .5) + k * W * max(prof(t), 0.02)
        return rod.at(s0 + (s1 - s0) * t, a, R(t, k) + dr)

    def emit(P, uv):
        n = np.cross(P[1] - P[0], P[2] - P[0])
        c = np.mean(P, 0)
        axis_pt = rod.top + rod.d * ((c - rod.top) @ rod.d)
        if n @ (c - axis_pt) < 0:
            P, uv = P[::-1], uv[::-1]
        m.quad(P, uv)

    u0, u1 = REG[region]
    vlo, vhi = 0.0, 1.0
    if STYLE in ("mix", "tiers") and not glass:   # the engraving at about one texel per model px: a short plate shows a part of it
        span = min(1.0, float(np.linalg.norm(S(1, 0) - S(0, 0))) / 6.5)
        vlo, vhi = 0.5 - span / 2, 0.5 + span / 2
    if glass:
        assert isl and ISLAND_LAYER[isl] == LAYER[slot], (slot, isl)
        DENS.append((slot, bone, isl, density(S, isl)))
    for i in range(ns):
        for j in range(na):
            t0, t1 = i / ns, (i + 1) / ns
            k0, k1 = -1 + 2 * j / na, -1 + 2 * (j + 1) / na
            P = [S(t0, k0), S(t0, k1), S(t1, k1), S(t1, k0)]
            if glass:
                uv = [isl_uv(isl, (k + 1) / 2, t, mirror) for t, k in ((t0, k0), (t0, k1), (t1, k1), (t1, k0))]
            else:
                uv = [(u0 + (u1 - u0) * (k + 1) / 2, vlo + (vhi - vlo) * t) for t, k in ((t0, k0), (t0, k1), (t1, k1), (t1, k0))]
            emit(P, uv)
    if rim and glass:   # the plate's thickness: a strip folding inward along every edge, inside the outline band
        edges = [[(i / ns, -1) for i in range(ns + 1)], [(i / ns, 1) for i in range(ns + 1)],
                 [(0, -1 + 2 * j / na) for j in range(na + 1)], [(1, -1 + 2 * j / na) for j in range(na + 1)]]
        for ei, e in enumerate(edges):
            ins = (RIMBAND, 0.0) if ei < 2 else (0.0, RIMBAND)
            for (ta, ka), (tb, kb) in zip(e, e[1:]):
                A, B = S(ta, ka), S(tb, kb)
                C, D = S(tb, kb, -th), S(ta, ka, -th)
                if np.linalg.norm(B - A) < 1e-4:
                    continue
                uva, uvb = isl_uv(isl, (ka + 1) / 2, ta, mirror), isl_uv(isl, (kb + 1) / 2, tb, mirror)
                uvc, uvd = isl_uv(isl, (kb + 1) / 2, tb, mirror, ins), isl_uv(isl, (ka + 1) / 2, ta, mirror, ins)
                m.quad([A, B, C, D], [uva, uvb, uvc, uvd])
        return S
    if rim:   # bevelled edge: a strip folding inward by th along the long edges and both ends (visible thickness)
        ru0, ru1 = REG["rim"]
        edges = [[(i / ns, -1) for i in range(ns + 1)], [(i / ns, 1) for i in range(ns + 1)],
                 [(0, -1 + 2 * j / na) for j in range(na + 1)], [(1, -1 + 2 * j / na) for j in range(na + 1)]]
        for e in edges:
            for (ta, ka), (tb, kb) in zip(e, e[1:]):
                A, B = S(ta, ka), S(tb, kb)
                C, D = S(tb, kb, -th), S(ta, ka, -th)
                if np.linalg.norm(B - A) < 1e-4:
                    continue
                m.quad([A, B, C, D], [(ru0, 0), (ru1, 0), (ru1, 1), (ru0, 1)])
    return S


def fx_v(t_arrive):
    """energy-strip v for a point the bright packet reaches t_arrive seconds into the idle sweep. The strip holds the
    pattern twice (period 0.5 in v) and uv_anim adds 0.5*phase (0..0.5), so v stays in 0..1 and no wrap is needed."""
    a = (t_arrive / PERIOD) % 1.0
    return 0.5 * (1.0 - a)


def rail(slot, bone, path, arrive, width=0.36, n=14):
    """light ribbon along path(t) -> (point, outward normal), t 0..1; arrive = (t0, t1) seconds into the sweep"""
    m = M(slot, bone, "fx")
    if STYLE == "tiers":   # finer light lines: a thick ribbon reads as a sticker on the small plates
        width *= 0.70
    pts = [path(i / n) for i in range(n + 1)]
    # keep v monotonic inside one segment: split where fx_v wraps
    for i in range(n):
        (p0, n0), (p1, n1) = pts[i], pts[i + 1]
        tang = p1 - p0
        tl = np.linalg.norm(tang)
        if tl < 1e-6:
            continue
        tang = tang / tl
        s0 = np.cross(n0, tang)
        s1 = np.cross(n1, tang)
        s0 /= np.linalg.norm(s0)
        s1 /= np.linalg.norm(s1)
        a0, a1 = p0 + n0 * LIFT, p1 + n1 * LIFT
        ta = arrive[0] + (arrive[1] - arrive[0]) * i / n
        tb = arrive[0] + (arrive[1] - arrive[0]) * (i + 1) / n
        v0, v1 = fx_v(ta), fx_v(tb)
        if abs(v1 - v0) > 0.25:          # crossed the wrap: shift one end by a full pattern period (0.5)
            v1 = v1 + 0.5 if v1 < v0 else v1 - 0.5
            if not (0 <= v1 <= 1):
                v0, v1 = v0 - (0.5 if v0 > 0.5 else -0.5), v1 - (0.5 if v1 > 0.5 else -0.5)
        Q = [a0 - s0 * width / 2, a0 + s0 * width / 2, a1 + s1 * width / 2, a1 - s1 * width / 2]
        uv = [(0.25, v0), (0.75, v0), (0.75, v1), (0.25, v1)]
        nn = n0 + n1
        nn = nn / np.linalg.norm(nn)
        if np.cross(Q[1] - Q[0], Q[3] - Q[0]) @ nn < 0:
            Q, uv = Q[::-1], uv[::-1]
        m.quad(Q, uv, nn)


def surf_path(S, k, t0=0.0, t1=1.0, wob=None):
    """a path along a plate surface at fixed k (or k(t)), with outward normals"""
    def p(t):
        tt = t0 + (t1 - t0) * t
        kk = wob(tt) if wob else k
        P = S(tt, kk)
        e = 1e-3
        dt = S(min(1, tt + e), kk) - S(max(0, tt - e), kk)
        dk = S(tt, min(1, kk + e)) - S(tt, max(-1, kk - e))
        nrm = np.cross(dk, dt)
        c = P - S(tt, kk, -0.1)          # outward = away from the rod
        nrm = nrm if nrm @ c > 0 else -nrm
        return P, nrm / np.linalg.norm(nrm)
    return p


# ====================================================================== helmet
# ---------------------------------------------------------------------- accent primitives (tier silhouettes)
def ribbon(slot, bone, pts, widths, side, reg="metal", layer="metal", cross=0.0, isl=None, mirror=False):
    """a strip along pts (list of points), width per point, side(i) -> unit vector across the strip at point i.
    Both faces are emitted (thin pieces seen from either side). reg: atlas region for u across, v along; or isl:
    a clean island (glass, edge to edge). cross > 0: a second strip at 90 degrees (depth), that fraction wide."""
    m = M(slot, bone, layer)
    n = len(pts) - 1
    u0, u1 = REG[reg] if reg else (0, 1)
    def uv(k, i):
        if isl:
            return isl_uv(isl, k, i / n, mirror)
        return (u0 + (u1 - u0) * k, i / n)
    for i in range(n):
        A, B = np.asarray(pts[i], float), np.asarray(pts[i + 1], float)
        sa, sb = np.asarray(side(i), float), np.asarray(side(i + 1), float)
        for sd, wf in ((lambda j, s_: s_, 1.0),) + (((None, cross),) if cross > 0 else ()):
            if sd is None:   # the cross strip: across = tangent x side
                tg = B - A
                sa2 = np.cross(tg, sa); sa2 /= np.linalg.norm(sa2) + 1e-9
                sb2 = np.cross(tg, sb); sb2 /= np.linalg.norm(sb2) + 1e-9
                ca, cb = sa2, sb2
            else:
                ca, cb = sa, sb
            wa, wb = widths[i] * wf / 2, widths[i + 1] * wf / 2
            Q = [A - ca * wa, A + ca * wa, B + cb * wb, B - cb * wb]
            U = [uv(0, i), uv(1, i), uv(1, i + 1), uv(0, i + 1)]
            m.quad(Q, U)
            m.quad(Q[::-1], U[::-1])


def kite(slot, bone, base, tip, half_w, side, isl=None, reg="metal", layer=None, mirror=False, shoulder=0.38):
    """a flat crystal / blade: base, two shoulders, tip - mapped onto an island edge to edge (glass: outlined all
    round) or a region. Both faces."""
    base, tip, side = (np.asarray(x, float) for x in (base, tip, side))
    mid = base + (tip - base) * shoulder
    P = [base, mid - side * half_w, tip, mid + side * half_w]
    layer = layer or ("glass" if isl else "metal")
    m = M(slot, bone, layer)
    if isl:
        U = [isl_uv(isl, 0, 1, mirror), isl_uv(isl, 0, 0, mirror), isl_uv(isl, 1, 0, mirror), isl_uv(isl, 1, 1, mirror)]
    else:
        u0, u1 = REG[reg]
        U = [(u0, 1.0), (u0, 0.0), (u1, 0.0), (u1, 1.0)]
    m.quad(P, U)
    m.quad(P[::-1], U[::-1])


def boxq(slot, bone, c, ax, ay, az, reg="metal"):
    """a box centred at c with half-axes ax, ay, az (vectors), every face over the atlas region"""
    c, ax, ay, az = (np.asarray(x, float) for x in (c, ax, ay, az))
    m = M(slot, bone, "metal")
    u0, u1 = REG[reg]
    U = [(u0, 0), (u1, 0), (u1, 1), (u0, 1)]
    for n_, a_, b_ in ((az, ax, ay), (-az, -ax, ay), (ax, -az, ay), (-ax, az, ay), (ay, ax, -az), (-ay, ax, az)):
        f = c + n_
        Q = [f - a_ + b_, f + a_ + b_, f + a_ - b_, f - a_ - b_]
        if np.cross(Q[1] - Q[0], Q[3] - Q[0]) @ n_ > 0:
            Q, U2 = Q[::-1], U[::-1]
        else:
            U2 = U
        m.quad(Q, U2)


def crystal(slot, bone, base, axis, length, r, isl_side="leg_end", isl_tip="arm_top", tip=0.38, sides=6, twist=0.0):
    """a real crystal: a hexagonal prism with a pointed tip (3D), every face a Novak Glass pane mapped edge to edge
    onto a clean island - RF outlines each facet, so it reads cut, not flat"""
    base, axis = np.asarray(base, float), np.asarray(axis, float)
    axis = axis / np.linalg.norm(axis)
    a = np.cross(axis, [0, 0, 1.0]) if abs(axis[2]) < 0.9 else np.cross(axis, [1.0, 0, 0])
    a /= np.linalg.norm(a)
    b = np.cross(axis, a)
    top = base + axis * length * (1 - tip)
    apex = base + axis * length
    ring = lambda c, k, rr: c + rr * (math.cos(2 * math.pi * k / sides + twist) * a + math.sin(2 * math.pi * k / sides + twist) * b)
    m = M(slot, bone, "glass")
    for k in range(sides):
        B0, B1 = ring(base, k, r * 0.92), ring(base, k + 1, r * 0.92)
        T0, T1 = ring(top, k, r), ring(top, k + 1, r)
        mir = k % 2 == 1
        P = [B0, B1, T1, T0]
        U = [isl_uv(isl_side, 0, 1, mir), isl_uv(isl_side, 1, 1, mir), isl_uv(isl_side, 1, 0, mir), isl_uv(isl_side, 0, 0, mir)]
        if np.cross(P[1] - P[0], P[3] - P[0]) @ ((B0 + T1) / 2 - (base + top) / 2) < 0:
            P, U = P[::-1], U[::-1]
        m.quad(P, U)
        P = [T0, T1, apex]
        U = [isl_uv(isl_tip, 0, 1), isl_uv(isl_tip, 1, 1), isl_uv(isl_tip, 0.5, 0)]
        if np.cross(P[1] - P[0], P[2] - P[0]) @ ((T0 + T1 + apex) / 3 - (top + apex) / 2) < 0:
            P, U = P[::-1], U[::-1]
        m.quad(P, U)


def gem(slot, bone, c, R, facets=8):
    """a brilliant-cut diamond around c (girdle radius R): table, crown facets, girdle, long pavilion to a point.
    Every facet a Novak Glass pane on a clean island, so RF outlines each cut edge."""
    up = np.array([0, 1.0, 0])
    prof = [(0.52, 0.86), (0.84, 0.50), (1.00, 0.14), (1.00, 0.02), (0.62, -0.70), (0.0, -1.62)]   # (radius, height) x R
    rings = []
    for k, (rr, hh) in enumerate(prof):
        tw = (math.pi / facets) * (k % 2)          # alternate rings turn half a facet: kite / star facets
        rings.append([c + up * hh * R + R * rr * np.array([math.cos(2 * math.pi * j / facets + tw), 0,
                                                            math.sin(2 * math.pi * j / facets + tw)]) for j in range(facets)])
    m = M(slot, bone, "glass")
    isls = ("arm_top", "leg_end", "boot", "side_r", "leg_end", "arm_top")
    top = c + up * 0.86 * R
    for j in range(facets):                        # the table (an octagon: triangles to its centre)
        P = [rings[0][j], rings[0][(j + 1) % facets], top]
        m.quad(P[::-1] if np.cross(P[1] - P[0], P[2] - P[0])[1] < 0 else P,
               [isl_uv("crown", 0, 1), isl_uv("crown", 1, 1), isl_uv("crown", .5, 0)])
    for k in range(len(rings) - 1):
        A, B = rings[k], rings[k + 1]
        isl = isls[k]
        for j in range(facets):
            P = [A[j], A[(j + 1) % facets], B[(j + 1) % facets], B[j]]
            U = [isl_uv(isl, 0, 0), isl_uv(isl, 1, 0), isl_uv(isl, 1, 1), isl_uv(isl, 0, 1)]
            if np.linalg.norm(P[2] - P[3]) < 1e-6:   # the point
                P, U = P[:3], U[:3]
            cen = np.mean(P, 0)
            n = np.cross(P[1] - P[0], P[2] - P[0])
            if n @ (cen - (c + up * (prof[k][1] + prof[k + 1][1]) / 2 * R)) < 0:
                P, U = P[::-1], U[::-1]
            m.quad(P, U)


def octa(slot, bone, c, r, h=1.5, isl="arm_top"):
    """a small floating octahedral gem (a diamond's rough form), every face a glass pane"""
    c = np.asarray(c, float)
    eq = [c + r * np.array([math.cos(a), 0, math.sin(a)]) for a in np.linspace(0, 2 * math.pi, 5)[:4] + math.pi / 4]
    tips = [c + np.array([0, r * h, 0]), c - np.array([0, r * h, 0])]
    m = M(slot, bone, "glass")
    for tip in tips:
        for j in range(4):
            P = [eq[j], eq[(j + 1) % 4], tip]
            U = [isl_uv(isl, 0, 1), isl_uv(isl, 1, 1), isl_uv(isl, .5, 0)]
            if np.cross(P[1] - P[0], P[2] - P[0]) @ (np.mean(P, 0) - c) < 0:
                P, U = P[::-1], U[::-1]
            m.quad(P, U)


def helmet(head_pts):
    """faceted enclosed helmet: convex offset shell of the real head ball, crown ridge, tapered chin, dark visor"""
    from scipy.spatial import ConvexHull
    hull = ConvexHull(head_pts)
    hc = np.array([0.0, 29.7, -1.65])
    hn, hd = hull.equations[:, :3], hull.equations[:, 3]
    GROW = 0.40

    def radius(dvec):
        den = hn @ dvec
        ok = den > 1e-8
        return float(np.min((-hd[ok] - hn[ok] @ hc) / den[ok]))

    def H(phi, th, extra=0.0):
        d = np.array([math.sin(phi) * math.sin(th), math.cos(phi), -math.sin(phi) * math.cos(th)])
        r = radius(d) + GROW + extra
        front = max(0.0, math.cos(th))
        r += 0.22 * math.exp(-(th / 0.22) ** 2) * math.sin(phi) ** 2 * (phi < 1.75)   # crown / brow ridge
        r += 0.35 * max(0.0, (phi - 2.2) / 0.94) ** 1.5 * front ** 3                    # tapered chin
        r += 0.42 * (max(0.0, 0.35 - math.cos(th)) / 1.35) ** 0.5 * math.sin(phi) ** 0.3                # back: covers the flat rim
        return hc + r * d

    if STYLE == "glass":
        # a crystal orb: the head always faces the camera (billboard), so the shell is unwrapped as seen from the
        # front - its outline circle onto the border of one clean island (concentric disc -> square map). The neon
        # line then runs round the orb's silhouette and the facets sit evenly across it; no seams anywhere on it.
        NB = 720
        rb = np.zeros(NB)
        for ph in np.linspace(0.01, math.pi - 0.01, 90):
            for th in np.linspace(-math.pi, math.pi, 360, endpoint=False):
                q = H(ph, th) - hc
                k = int(((math.atan2(q[1], q[0]) + math.pi) / (2 * math.pi)) * NB) % NB
                rb[k] = max(rb[k], math.hypot(q[0], q[1]))
        for k in np.nonzero(rb == 0)[0]:
            rb[k] = max(rb[k - 1], rb[(k + 1) % NB])
        # a smooth outline: the binned maximum is jagged (a few samples per bin), and a jagged radius bends the
        # unwrap triangle by triangle - the lens then shows the mesh as a saw-tooth of bright and dark slivers
        rb = np.max([np.roll(rb, i) for i in range(-6, 7)], axis=0)
        ker = np.exp(-np.linspace(-2.5, 2.5, 41) ** 2)
        rb = np.real(np.fft.ifft(np.fft.fft(rb) * np.fft.fft(np.roll(np.pad(ker / ker.sum(), (0, NB - 41)), -20))))

        def orb_uv(P):
            q = P - hc
            phi = math.atan2(q[1], q[0])
            f = ((phi + math.pi) / (2 * math.pi)) * NB - 0.5
            k0 = int(math.floor(f)) % NB
            R = rb[k0] + (rb[(k0 + 1) % NB] - rb[k0]) * (f - math.floor(f))
            r = min(1.0, math.hypot(q[0], q[1]) / max(R, 1e-6))
            sx, sy = disc_to_square(r * math.cos(phi), r * math.sin(phi))
            return isl_uv("crown", (sx + 1) / 2, (1 - sy) / 2)

    def hnorm(p, a, e=1e-3):   # smooth surface normal of the shell (the mix helmet is shaded smooth, not faceted)
        pp = min(max(p, 2e-3), math.pi - 2e-3)
        dp, da = H(pp + e, a) - H(pp - e, a), H(pp, a + e) - H(pp, a - e)
        nn = np.cross(dp, da)
        return nn if nn @ (H(pp, a) - hc) > 0 else -nn

    NP, NT = (16, 28) if STYLE in ("glass", "mix", "tiers") else (10, 16)
    m = M("head", "p17", "glass" if STYLE == "glass" else "metal")
    for i in range(NP):
        for j in range(NT):
            p0, p1 = math.pi * i / NP, math.pi * (i + 1) / NP
            a0, a1 = 2 * math.pi * j / NT - math.pi, 2 * math.pi * (j + 1) / NT - math.pi
            ac = (a0 + a1) / 2
            vis = abs(ac) < 1.15 and 0.36 * math.pi < (p0 + p1) / 2 < 0.86 * math.pi and STYLE not in ("mix", "tiers")
            u0, u1 = REG["visor" if vis else "metal"]
            corners = ((p0, a0), (p1, a0), (p1, a1), (p0, a1))
            P = [H(p, a) for p, a in corners]
            if STYLE == "glass":
                uv = [orb_uv(H(p, a)) for p, a in corners]
            elif vis:
                uv = [(u0 + (u1 - u0) * (0.5 + 0.5 * a / 1.2), min(1, max(0, (p - 0.36 * math.pi) / (0.5 * math.pi))))
                      for p, a in corners]
            elif STYLE in ("mix", "tiers"):   # the keel (u = 0.5) down the back of the helmet, the wrap at the front (visor)
                uv = [(u0 + (u1 - u0) * (0.12 + 0.76 * (a + (2 * math.pi if ac < 0 else 0)) / (2 * math.pi)), p / math.pi)
                      for p, a in corners]
            else:
                uv = [(u0 + (u1 - u0) * (0.2 + 0.6 * (a + math.pi) / (2 * math.pi)), p / math.pi) for p, a in corners]
            if i == 0:   # poles: triangles
                P, uv = [P[0], P[1], P[2]], uv[:3]
            elif i == NP - 1:
                P, uv = [P[0], P[1], P[3]], [uv[0], uv[1], uv[3]]
            n = np.cross(P[1] - P[0], P[2] - P[0])
            vn = [hnorm(p, a) for p, a in corners] if STYLE in ("mix", "tiers") else None
            if vn:
                vn = vn[:3] if i == 0 else [vn[0], vn[1], vn[3]] if i == NP - 1 else vn
            if n @ (np.mean(P, 0) - hc) < 0:
                P, uv = P[::-1], uv[::-1]
                vn = vn[::-1] if vn else None
            m.quad(P, uv, vn=vn)

    def cheek(sign):   # from the brow down both visor edges, sweeping in to the chin
        def p(t):
            phi = 0.30 * math.pi + 0.56 * math.pi * t
            th = sign * (1.22 - 0.95 * t ** 1.6) * (1.14 if STYLE in ("mix", "tiers") else 1.0)   # mix: beside the glass visor
            P = H(phi, th)
            n = P - hc
            return P, n / np.linalg.norm(n)
        return p
    if STYLE in ("mix", "tiers"):
        # the original enclosed helmet in metal; its visor becomes a crystal pane: a dark backing in the visor's
        # shield shape (between the cheek lines) and the glass over it, mapped edge to edge onto one island so RF's
        # neon outline traces the visor - the dark visor of the original seen through Novak Glass
        def VS(t, k, lift):
            phi = 0.30 * math.pi + 0.56 * math.pi * t
            return H(phi, k * (1.22 - 0.95 * t ** 1.6), lift)
        vb, vg = M("head", "p17", "metal"), M("head", "p17", "glass")
        ns, na = 14, 12
        for i in range(ns):
            for j in range(na):
                t0, t1 = i / ns, (i + 1) / ns
                k0, k1 = -1 + 2 * j / na, -1 + 2 * (j + 1) / na
                cs = ((t0, k0), (t1, k0), (t1, k1), (t0, k1))
                for mesh, lift, uvf in ((vb, VISOR_BACK, lambda t, k: (REG["visor"][0] + (REG["visor"][1] - REG["visor"][0]) * (k + 1) / 2, t)),
                                        (vg, VISOR_GLASS, lambda t, k: isl_uv("crown", (k + 1) / 2, t))):
                    P = [VS(t, k, lift) for t, k in cs]
                    uv = [uvf(t, k) for t, k in cs]
                    n = np.cross(P[1] - P[0], P[2] - P[0])
                    if n @ (np.mean(P, 0) - hc) < 0:
                        P, uv = P[::-1], uv[::-1]
                    mesh.quad(P, uv)
        DENS.append(("head", "p17", "crown", density(lambda t, k, dr=0: VS(t, k, VISOR_GLASS), "crown")))
    if STYLE != "glass" and (not TIER or TIER["level"] >= 2):
        for sg in (-1, 1):
            rail("head", "p17", cheek(sg), (PERIOD - 0.25, PERIOD + 0.2), width=0.32, n=16)
    acc = TIER["acc"] if TIER else ()
    if acc:   # the head faces the camera (billboard): accents are built in that frame (front = -z, up = +y)
        Rs = max(math.hypot(*(H(ph, th) - hc)[:2]) for ph in np.linspace(.05, 3.1, 40) for th in np.linspace(-3.1, 3.1, 60))
    if "gemhead" in acc:  # PRISM: the head is a cut diamond - the helmet, visor and cheek lines give way to a gem
        for k_ in [k_ for k_ in MESH if k_[0] == "head" and k_[1] != "metal"]:   # keep the shell: a sapphire core
            del MESH[k_]
        gem("head", "p17", hc + np.array([0, 0.35, 0.0]), Rs * 1.06)
        Rs = max(math.hypot(*(H(ph, th) - hc)[:2]) for ph in np.linspace(.05, 3.1, 40) for th in np.linspace(-3.1, 3.1, 60))
    if "halo" in acc:     # PRISM: a light ring behind the helmet, the spectrum running round it
        c0 = hc + np.array([0.0, 0.35, 1.6])
        def ring(t):
            a = 2 * math.pi * t + math.pi / 2
            return c0 + Rs * 1.16 * np.array([math.cos(a), math.sin(a), 0.0]), np.array([0.0, 0.0, -1.0])
        rail("head", "p17", ring, (0.0, 1.2), width=0.30, n=48)
    if "horns" in acc:    # ECLIPSE: two swept horns from the temples, a violet rail inside each
        mh = M("head", "p17", "metal")
        u0, u1 = REG["metal"]
        for sg in (-1, 1):
            base = H(0.40 * math.pi, sg * 1.50)
            def hp(t, sg=sg, base=base):
                return base + np.array([sg * 1.5 * math.sin(t * 1.4), 3.4 * t ** 1.15 - 0.4 * t ** 3, 0.9 * t])
            N = 12
            pts = [hp(i / N) for i in range(N + 1)]
            wid = [0.95 * (1 - i / N) ** 0.85 + 0.04 for i in range(N + 1)]
            for i in range(N):
                tg = pts[i + 1] - pts[i]
                side = np.cross(tg, [0, 0, 1.0])
                side /= np.linalg.norm(side)
                tg2 = pts[min(i + 2, N)] - pts[i + 1]
                side2 = np.cross(tg2 if np.linalg.norm(tg2) > 1e-6 else tg, [0, 0, 1.0])
                side2 /= np.linalg.norm(side2)
                Q = [pts[i] - side * wid[i] / 2, pts[i] + side * wid[i] / 2,
                     pts[i + 1] + side2 * wid[i + 1] / 2, pts[i + 1] - side2 * wid[i + 1] / 2]
                uv = [(u0, i / N), (u1, i / N), (u1, (i + 1) / N), (u0, (i + 1) / N)]
                mh.quad(Q, uv, n=np.array([0, 0, -1.0]))
                mh.quad(Q[::-1], uv[::-1], n=np.array([0, 0, 1.0]))
                dz = np.array([0, 0, 1.0])   # the cross ribbon (front-back): the horn reads as a blade with depth
                Q2 = [pts[i] - dz * wid[i] * .35, pts[i] + dz * wid[i] * .35,
                      pts[i + 1] + dz * wid[i + 1] * .35, pts[i + 1] - dz * wid[i + 1] * .35]
                nx = np.cross(tg, dz)
                nx = nx / np.linalg.norm(nx) * sg
                mh.quad(Q2, uv, n=nx)
                mh.quad(Q2[::-1], uv[::-1], n=-nx)
            rail("head", "p17", lambda t, hp=hp: (hp(0.05 + 0.8 * t) + np.array([0, 0, -0.08]), np.array([0, 0, -1.0])),
                 (0.0, 0.5), width=0.16, n=10)
    C2 = lambda a, r, z=0.0: hc + np.array([r * math.cos(a), r * math.sin(a), z])   # billboard frame, a from +x
    radial = lambda a: np.array([math.cos(a), math.sin(a), 0.0])
    D_ = math.radians
    if "hood" in acc:     # RUNNER: a soft hood framing the helmet, drawstrings hanging in front
        A = [D_(-25 + 230 * i / 24) for i in range(25)]
        w = [0.45 + 0.75 * max(0.0, math.sin(a)) ** 1.5 for a in A]
        pts = [C2(a, Rs * 0.96 + wi / 2, 0.9) for a, wi in zip(A, w)]
        ribbon("head", "p17", pts, w, lambda i: radial(A[i]), reg="recess")
        for sx in (-0.75, 0.75):
            st = [hc + np.array([sx + 0.05 * math.sin(i), -Rs * 0.86 - 0.42 * i, -Rs * 0.55]) for i in range(7)]
            ribbon("head", "p17", st, [0.16] * 7, lambda i: np.array([1.0, 0, 0]), reg="rim")
    if "antennae" in acc:  # CIRCUIT: two antenna masts with glowing tips, round ear coils
        for a0 in (D_(62), D_(118)):
            mast = [C2(a0, Rs * 0.92 + 2.3 * i / 6, 0.2) + np.array([0, 0.25 * (i / 6) ** 2, 0]) for i in range(7)]
            ribbon("head", "p17", mast, [0.20] * 7, lambda i, a0=a0: np.array([-math.sin(a0), math.cos(a0), 0]),
                   reg="metal", cross=1.0)
            tip = mast[-1]
            kite("head", "p17", tip - np.array([0, 0.45, 0]), tip + np.array([0, 0.45, 0]), 0.42, [1.0, 0, 0], reg="rim")
        for a0 in (0.0, math.pi):
            ring = [C2(a0, Rs * 0.98, 0.15) + 0.85 * np.array([0, math.cos(2 * math.pi * i / 20), 0])
                    + 0.85 * radial(a0) * math.sin(2 * math.pi * i / 20) * 0.6 for i in range(21)]
            ribbon("head", "p17", ring, [0.30] * 21, lambda i: radial(a0), reg="rim")
    if "cowl" in acc:     # PHANTOM: an assassin's cowl rising to a swept point
        A = [D_(10 + 160 * i / 24) for i in range(25)]
        w = [0.30 + 1.9 * max(0.0, 1 - abs(math.degrees(a) - 104) / 34) ** 1.4 for a in A]
        pts = [C2(a, Rs * 0.97 + wi / 2, 0.8) for a, wi in zip(A, w)]
        ribbon("head", "p17", pts, w, lambda i: radial(A[i]), reg="recess")
    if "crest" in acc:    # VANGUARD: a tall crescent crest over the helmet
        A = [D_(48 + 84 * i / 24) for i in range(25)]
        w = [1.9 * math.sin(math.pi * i / 24) ** 0.8 + 0.05 for i in range(25)]
        pts = [C2(a, Rs * 0.90 + wi / 2, 0.35) for a, wi in zip(A, w)]
        ribbon("head", "p17", pts, w, lambda i: radial(A[i]), reg="recess", cross=0.12)
    if "flamecrown" in acc:   # INFERNO: one fire behind the helmet - wide overlapping licks, not separate spikes
        for k, (deg, L, w0, z) in enumerate(((90, 3.0, 2.4, 0.9), (60, 2.2, 1.9, 0.7), (120, 2.2, 1.9, 0.7),
                                               (38, 1.3, 1.4, 0.5), (142, 1.3, 1.4, 0.5), (78, 2.0, 1.3, 0.35),
                                               (104, 2.3, 1.2, 0.35))):
            a0 = D_(deg)
            bend = (90 - deg) / 90 * 0.7
            perp = np.array([-math.sin(a0), math.cos(a0), 0])
            pts = [C2(a0, Rs * 0.72 + L * i / 10, z) + np.array([bend * (i / 10) ** 2, 0, 0])
                   + perp * 0.30 * math.sin(i / 10 * math.pi * 1.5 + k * 1.7) * (i / 10) for i in range(11)]
            ribbon("head", "p17", pts, [w0 * (1 - i / 10) ** 1.25 + 0.02 for i in range(11)],
                   lambda i, a0=a0: np.array([-math.sin(a0), math.cos(a0), 0]), reg="rim")
    if "crescent" in acc:  # ECLIPSE: an eclipse crescent behind the helmet
        A = [D_(i * 360 / 48) for i in range(49)]
        w = [max(0.0, math.cos(a - D_(125))) ** 1.3 * 1.25 + 0.02 for a in A]
        pts = [C2(a, Rs * 1.12 + wi / 2, 1.4) for a, wi in zip(A, w)]
        ribbon("head", "p17", pts, w, lambda i: radial(A[i]), reg="metal")
        rail("head", "p17", lambda t: (C2(D_(360 * t), Rs * 1.12, 1.35), np.array([0, 0, -1.0])), (0.0, 1.2),
             width=0.16, n=48)
    if "tiara" in acc:    # PRISM: cut crystals rising from the crown of the helmet (above it: the head hides little)
        for deg, L, rr, z in ((90, 3.6, 0.55, 0.10), (68, 2.4, 0.42, 0.35), (112, 2.4, 0.42, 0.35),
                              (50, 1.3, 0.30, 0.5), (130, 1.3, 0.30, 0.5)):
            a0 = D_(deg)
            crystal("head", "p17", C2(a0, Rs * 0.78, z), radial(a0) + np.array([0, 0, 0.12]), L, rr,
                    twist=0.3 * (deg != 90))
    if "nemes" in acc:    # SOLAR (gold): a striped royal headdress - flaps framing the helmet down past the shoulders
        for sg in (-1, 1):
            pts, w = [], []
            for i in range(15):
                t = i / 14
                if t < 0.5:   # round the top of the helmet to its side
                    a = D_(90 - sg * 0 + sg * (-90) * (t / 0.5) * -1)
                    a = D_(90 + sg * 90 * (t / 0.5) * -1) if sg > 0 else D_(90 + 90 * (t / 0.5))
                    p_ = C2(a, Rs * 1.02 + 0.35, 0.9)
                else:         # then down, flaring out
                    q = (t - 0.5) / 0.5
                    p_ = C2(D_(0 if sg > 0 else 180), Rs * 1.02 + 0.35 + 1.1 * q, 0.9) + np.array([0, -4.2 * q, 0])
                pts.append(p_)
                w.append(0.9 + 1.5 * t ** 1.4)
            def nside(i, pts=pts, sg=sg):
                tg = pts[min(i + 1, 14)] - pts[max(i - 1, 0)]
                v_ = np.cross(tg, [0, 0, 1.0]) * sg
                return v_ / np.linalg.norm(v_)
            ribbon("head", "p17", pts, w, nside, reg="recess")
    return check_inside(m, head_pts, hc)


def check_inside(m, pts, c):
    """min clearance (px) between the head vertices and the shell along rays from the head centre (> 0 = inside)"""
    P = np.array(m.pos)
    tri = []
    for poly in m.polys:
        a, b, cc, d = (P[x[0]] for x in poly)
        tri.append((a, b, cc))
        if poly[2] != poly[3]:
            tri.append((a, cc, d))
    T = np.array(tri)
    e1, e2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
    s = c - T[:, 0]
    q = np.cross(s, e1)
    worst = 1e9
    for v in np.unique(np.round(pts, 5), axis=0):
        dvec = v - c
        dist = np.linalg.norm(dvec)
        dvec /= dist
        h = np.cross(dvec, e2)
        det = np.einsum("ij,ij->i", e1, h)
        ok = np.abs(det) > 1e-12
        f = np.where(ok, 1 / np.where(ok, det, 1), 0)
        u = f * np.einsum("ij,ij->i", s, h)
        w = f * (q @ dvec)
        t = f * np.einsum("ij,ij->i", e2, q)
        hit = ok & (u >= -1e-6) & (w >= -1e-6) & (u + w <= 1 + 1e-6) & (t > 0)
        if hit.any():
            worst = min(worst, float(t[hit].min() - dist))
    return worst


# ====================================================================== the suit
def build_suit(bones):
    mesh = {n: np.array(b["poly_mesh"]["positions"]) for n, b in bones.items() if "poly_mesh" in b}
    rod = {n: Rod(mesh[n]) for n in ("p22", "p30", "p32", "p37", "p39", "p44", "p46", "p48", "p52", "p54", "p56")}
    stats = {"helmet_clearance_px": helmet(mesh["p17"])}
    D = math.radians
    T = rod["p22"]
    sy = lambda y: (T.top[1] - y) / T.L   # torso s for a model y

    # ---------------- chest: V breastplate, layered pectoral leaves, spine
    V = plate("chest", "p22", T, sy(26.9), sy(20.2), 0.0, D(115), gap=0.10, bulge=0.20, flare=0.30,
              prof=lambda t: (1 - t) ** 0.8 * (0.55 + 0.45 * math.sin(math.pi * min(1, t * 2.2))) + 0.03, ns=9, na=6)
    for sg in (-1, 1):
        plate("chest", "p22", T, sy(26.75), sy(22.6), sg * D(62), D(42), gap=0.42, bulge=0.22, flare=0.15,
              prof=lambda t: leaf(t, .5, top=.7), twist=-sg * D(20), ns=6, na=4)
    plate("chest", "p22", T, sy(26.6), sy(19.6), D(180), D(38), gap=0.10, bulge=0.18,
          prof=lambda t: leaf(t, .45, top=.5), ns=8, na=3)
    # V rails: from the sternum up and out to the shoulders; a short one down to the tip (packet starts at the centre)
    for sg in (-1, 1):
        rail("chest", "p22", surf_path(V, 0, 0.47, 0.0, wob=lambda t, sg=sg: sg * min(0.92, 0.08 + 1.75 * (0.47 - t))),
             (0.0, 0.42), n=12)
    rail("chest", "p22", surf_path(V, 0, 0.47, 0.95), (0.0, 0.22), n=8)

    # ---------------- chest: shoulder petals, forearm blades
    for up, lo, sg in (("p30", "p32", 1), ("p37", "p39", -1)):
        U, F = rod[up], rod[lo]
        out = sg * D(90)
        for k, (s0, s1, w, g) in enumerate(((0.0, 0.46, 100, 0.20), (0.07, 0.40, 80, 0.40), (0.12, 0.33, 58, 0.58))):
            P = plate("chest", up, U, s0, s1, out - sg * D(12 * k), D(w), gap=g, bulge=0.18, flare=0.35 - 0.08 * k,
                      prof=lambda t: leaf(t, .55, top=.8), twist=sg * D(18), ns=6, na=4)
            if k == 1:
                rail("chest", up, surf_path(P, 0, 0.05, 0.95), (0.42, 0.62), n=8)
        B = plate("chest", lo, F, 0.10, 0.97, out + sg * D(10), D(88), gap=0.08, bulge=0.18,
                  prof=lambda t: leaf(t, .5), twist=sg * D(25), ns=10, na=5)
        plate("chest", lo, F, 0.42, 1.0, out + sg * D(25), D(55), gap=0.45, bulge=0.18,
              prof=lambda t: leaf(t, .5), twist=sg * D(30), ns=6, na=3)
        rail("chest", lo, surf_path(B, 0.15, 0.05, 0.95), (0.62, 1.15), n=12)
        rail("chest", lo, surf_path(B, -0.55, 0.25, 0.85), (0.72, 1.05), width=0.16, n=8)

    # ---------------- legs: belt with a front V and hip petals, thigh petals, knee caps
    belt = plate("legs", "p22", T, sy(17.7), sy(16.5), 0.0, D(178), gap=0.18, bulge=0.05,
                 prof=lambda t: 1.0, ns=2, na=12)
    plate("legs", "p22", T, sy(17.8), sy(15.3), 0.0, D(36), gap=0.32, bulge=0.12, prof=lambda t: leaf(t, .6, top=1),
          ns=5, na=3)
    for sg in (-1, 1):
        plate("legs", "p22", T, sy(17.6), sy(15.0), sg * D(68), D(32), gap=0.36, bulge=0.15,
              prof=lambda t: leaf(t, .6, top=1), twist=sg * D(10), ns=5, na=3)
    rail("legs", "p22", surf_path(belt, 0, 0.5, 0.5, wob=lambda t: -0.9 + 1.8 * t), (0.3, 0.3), width=0.18, n=14)
    for th_, sh, sg in (("p44", "p46", 1), ("p52", "p54", -1)):
        R1, R2 = rod[th_], rod[sh]
        A = plate("legs", th_, R1, 0.04, 0.62, sg * D(38), D(70), gap=0.08, bulge=0.12,
                  prof=lambda t: leaf(t, .55, top=.6), twist=sg * D(15), ns=8, na=5)
        plate("legs", th_, R1, 0.40, 0.88, sg * D(24), D(48), gap=0.22, bulge=0.10,
              prof=lambda t: leaf(t, .55), twist=sg * D(12), ns=7, na=4)
        rail("legs", th_, surf_path(A, 0.1, 0.05, 0.95), (0.30, 0.65), n=10)
        plate("legs", sh, R2, 0.0, 0.17, 0.0, D(74), gap=0.12, bulge=0.22, prof=lambda t: leaf(t, .4), ns=4, na=4)

    # ---------------- feet: long tapered greaves with swept seams, foot shells
    for sh, ft, sg in (("p46", "p48", 1), ("p54", "p56", -1)):
        R2, R3 = rod[sh], rod[ft]
        G = plate("feet", sh, R2, 0.20, 0.93, sg * D(10), D(118), gap=0.08, bulge=0.12,
                  prof=lambda t: 0.25 + 0.75 * math.sin(math.pi * min(1, t * 0.62 + 0.05)) ** .7, ns=10, na=6)
        plate("feet", sh, R2, 0.26, 0.72, sg * D(62), D(40), gap=0.38, bulge=0.16, prof=lambda t: leaf(t, .5),
              twist=-sg * D(22), ns=6, na=3)
        for kk, arr in ((0.55, (0.66, 1.25)), (-0.55, (0.72, 1.32))):
            rail("feet", sh, surf_path(G, 0, 0.04, 0.97, wob=lambda t, kk=kk: kk * (0.55 + 0.35 * math.sin(t * math.pi))),
                 arr, n=14)
        plate("feet", ft, R3, 0.0, 1.0, 0.0, D(150), gap=0.12, bulge=0.12,
              prof=lambda t: 0.6 + 0.4 * math.sin(math.pi * t), ns=5, na=6)
    return stats


def build_suit_glass(bones):
    """Novak Glass on the stick: few, clean crystal pieces - each one plate mapped edge to edge onto one clean island,
    so each reads like one cube face of the regular glass armor (neon outline all round, facets, lens bevel). No
    layered leaves (every layer would draw its own outline: a tangle of lines) and no light rails (the glass pass
    draws its own light; rails on top read as stickers)."""
    mesh = {n: np.array(b["poly_mesh"]["positions"]) for n, b in bones.items() if "poly_mesh" in b}
    rod = {n: Rod(mesh[n]) for n in ("p22", "p30", "p32", "p37", "p39", "p44", "p46", "p48", "p52", "p54", "p56")}
    DENS.clear()
    stats = {"helmet_clearance_px": helmet(mesh["p17"])}
    D = math.radians
    T = rod["p22"]
    sy = lambda y: (T.top[1] - y) / T.L

    # chest: a V breastplate (broad at the collar, sharp at the sternum) and a back plate
    plate("chest", "p22", T, sy(26.9), sy(19.6), 0.0, D(118), gap=0.22, bulge=0.36, flare=0.30,
          prof=lambda t: (1 - t) ** 0.75 * (0.55 + 0.45 * math.sin(math.pi * min(1, t * 2.2))) + 0.04, ns=12, na=8,
          isl="side_r")
    plate("chest", "p22", T, sy(26.8), sy(20.2), D(180), D(84), gap=0.20, bulge=0.30, flare=0.20,
          prof=lambda t: leaf(t, .5, top=.7), ns=10, na=6, isl="side_l")
    # arms: a pauldron round the outside of the shoulder, a bracer round the forearm (mirrored pairs)
    for up, lo, sg in (("p30", "p32", 1), ("p37", "p39", -1)):
        U, F = rod[up], rod[lo]
        out = sg * D(90)
        plate("chest", up, U, 0.0, 0.50, out, D(105), gap=0.22, bulge=0.24, flare=0.35,
              prof=lambda t: leaf(t, .55, top=.8), twist=sg * D(18), ns=8, na=8, isl="arm_top", mirror=sg < 0)
        plate("chest", lo, F, 0.10, 0.96, out + sg * D(10), D(100), gap=0.12, bulge=0.20,
              prof=lambda t: leaf(t, .5), twist=sg * D(25), ns=12, na=8, isl="boot", mirror=sg < 0)
    # legs: a belt front and back, a thigh guard front and back per leg
    plate("legs", "p22", T, sy(18.0), sy(15.6), 0.0, D(95), gap=0.22, bulge=0.06,
          prof=lambda t: 1.0 - 0.45 * t ** 2, ns=4, na=12, isl="back_low")
    plate("legs", "p22", T, sy(18.0), sy(16.0), D(180), D(80), gap=0.22, bulge=0.06,
          prof=lambda t: 1.0 - 0.45 * t ** 2, ns=4, na=10, isl="knee")
    for th_, sg in (("p44", 1), ("p52", -1)):
        R1 = rod[th_]
        plate("legs", th_, R1, 0.04, 0.92, sg * D(20), D(80), gap=0.12, bulge=0.16,
              prof=lambda t: leaf(t, .5, top=.6), twist=sg * D(12), ns=12, na=8, isl="leg_b", mirror=sg < 0)
        plate("legs", th_, R1, 0.06, 0.86, D(180) - sg * D(20), D(72), gap=0.10, bulge=0.14,
              prof=lambda t: leaf(t, .5, top=.6), twist=-sg * D(10), ns=12, na=6, isl="leg_c", mirror=sg < 0)
    # feet: a greave down the shin, a calf guard behind it and a shell over the foot
    for sh, ft, sg in (("p46", "p48", 1), ("p54", "p56", -1)):
        R2, R3 = rod[sh], rod[ft]
        plate("feet", sh, R2, 0.06, 0.94, sg * D(8), D(100), gap=0.12, bulge=0.16,
              prof=lambda t: 0.25 + 0.75 * math.sin(math.pi * min(1, t * 0.62 + 0.05)) ** .7, ns=12, na=8,
              isl="side_r" if sg > 0 else "side_l")
        plate("feet", sh, R2, 0.12, 0.86, D(180), D(70), gap=0.10, bulge=0.14, prof=lambda t: leaf(t, .5, top=.5),
              ns=12, na=6, isl="boot", mirror=sg < 0)
        plate("feet", ft, R3, 0.0, 1.0, 0.0, D(150), gap=0.12, bulge=0.12,
              prof=lambda t: 0.6 + 0.4 * math.sin(math.pi * t), ns=6, na=8, isl="leg_end", mirror=sg < 0)
    stats["texels_per_px"] = {f"{s}/{b}/{i}": d for s, b, i, d in DENS}
    return stats


def body_accents(acc, rod, T, sy, D):
    """the tiers' body silhouettes (model space: up +y, front -z, avatar's left +x)"""
    X, Y, Z = np.eye(3)
    nb = T.at(sy(26.6), D(180), T.clear(sy(27), sy(26)) + 0.25)     # back of the neck
    if "scarf" in acc:    # RUNNER: a scarf round the neck, two tails flying behind
        plate("chest", "p22", T, sy(27.3), sy(26.1), 0.0, D(180), gap=0.30, bulge=0.12, prof=lambda t: 1.0,
              ns=2, na=14, region="recess", rim=False)
        for sx, L in ((-0.45, 9.5), (0.50, 7.2)):
            pts = [nb + np.array([sx + 1.6 * sx * (i / 10) ** 1.3, -L * i / 10, 0.35 + 2.2 * (i / 10) ** 1.2])
                   for i in range(11)]
            ribbon("chest", "p22", pts, [1.35 - 0.45 * i / 10 for i in range(11)], lambda i: X, reg="recess")
    if "pack" in acc:     # CIRCUIT: a power cell on the back
        boxq("chest", "p22", nb + np.array([0, -3.2, 1.05]), X * 1.55, Y * 2.4, Z * 0.75)
        for sx in (-0.75, 0.75):   # two glowing vents on top
            boxq("chest", "p22", nb + np.array([sx, -0.55, 1.15]), X * 0.24, Y * 0.42, Z * 0.24, reg="rim")
        boxq("chest", "p22", nb + np.array([0, -3.2, 1.82]), X * 0.9, Y * 1.6, Z * 0.04, reg="rim")   # the cell window
    if "strips" in acc:   # PHANTOM: a tattered cape of lamellar strips
        for k, (sx, L) in enumerate(((-1.6, 9.0), (-0.8, 11.5), (0.0, 10.0), (0.8, 12.0), (1.6, 8.4))):
            pts = [nb + np.array([sx * (1 + 0.45 * i / 10), -0.3 - L * i / 10, 0.35 + 1.5 * (i / 10) ** 1.3])
                   for i in range(11)]
            ribbon("chest", "p22", pts, [0.95 * (1 - 0.55 * i / 10) for i in range(11)], lambda i: X, reg="recess")
    if "halfcape" in acc:  # SOLAR: a crimson half-cape off the left shoulder, swept across the back
        b0 = nb + np.array([1.1, -0.1, 0.1])
        pts = [b0 + np.array([-1.0 * (i / 12) ** 1.2, -11.5 * i / 12, 0.40 + 2.0 * (i / 12) ** 1.4]) for i in range(13)]
        ribbon("chest", "p22", pts, [2.0 + 1.8 * (i / 12) ** 0.8 for i in range(13)], lambda i: X, reg="recess")
        boxq("chest", "p22", b0 + np.array([0.2, 0.0, -0.1]), X * 0.5, Y * 0.5, Z * 0.3, reg="rim")   # the clasp
    if "splitcape" in acc:  # PRISM: a white mantle split in two long panels, cyan light along the hems
        for sg in (-1, 1):
            pts = [nb + np.array([sg * (1.15 + 1.3 * (i / 12) ** 1.2), -0.2 - 13.5 * i / 12, 0.45 + 2.2 * (i / 12) ** 1.4])
                   for i in range(13)]
            ribbon("chest", "p22", pts, [2.3 + 1.2 * (i / 12) ** 0.8 for i in range(13)], lambda i: X, reg="recess")
    if "collar" in acc:   # ECLIPSE: a high standing collar round the back of the helmet
        plate("chest", "p22", T, sy(28.6), sy(26.4), D(180), D(200), gap=0.55, bulge=0.10, flare=0.9,
              prof=lambda t: 1.0 - 0.2 * t, ns=3, na=16, region="recess")
    if "cape" in acc:     # VANGUARD: a knight's cape from the shoulders
        pts = [nb + np.array([0, -0.2 - 13.0 * i / 12, 0.40 + 2.2 * (i / 12) ** 1.4]) for i in range(13)]
        ribbon("chest", "p22", pts, [3.4 + 2.6 * (i / 12) ** 0.8 for i in range(13)], lambda i: X, reg="recess")
    if "shoulderflames" in acc:   # INFERNO: flames off both shoulders
        for up, sg in (("p30", 1), ("p37", -1)):
            U = rod[up]
            for k, (s_, L) in enumerate(((0.22, 3.0), (0.42, 2.2))):
                base = U.at(s_, sg * D(120), U.clear(0, .5) + 0.35)
                pts = [base + np.array([sg * 1.6 * (i / 8), L * (i / 8) ** 0.9, 0.7 * (i / 8) ** 2]) for i in range(9)]
                def fside(i, pts=pts):
                    tg = pts[min(i + 1, 8)] - pts[max(i - 1, 0)]
                    v_ = np.cross(tg, Z)
                    return v_ / np.linalg.norm(v_)
                ribbon("chest", up, pts, [0.80 * (1 - i / 8) ** 0.9 + 0.03 for i in range(9)], fside,
                       reg="rim", cross=0.3)
    if "tails" in acc:    # ECLIPSE: two long blade tails from the waist
        for sg in (-1, 1):
            b0 = T.at(sy(17.2), D(180) - sg * D(28), T.clear(sy(18), sy(16)) + 0.35)
            pts = [b0 + np.array([sg * 1.2 * i / 10, -9.5 * i / 10, 1.4 * (i / 10) ** 1.2]) for i in range(11)]
            ribbon("legs", "p22", pts, [1.5 * (1 - i / 10) ** 0.7 + 0.04 for i in range(11)], lambda i: X, reg="recess")
    if "gems" in acc:      # PRISM: two gems floating beside the head, a ring of light round each
        for sg in (-1, 1):
            c = nb + np.array([sg * 5.2, 0.6, 0.0])
            octa("chest", "p22", c, 1.15, h=1.6, isl="arm_top")
            rail("chest", "p22", lambda t, c=c: (c + 1.75 * np.array([math.cos(2 * math.pi * t), 0.25 * math.sin(4 * math.pi * t),
                                                                      math.sin(2 * math.pi * t)]),
                                                 np.array([math.cos(2 * math.pi * t), 0, math.sin(2 * math.pi * t)])),
                 (0.0, 1.0), width=0.14, n=24)


TIER = None   # the tier being built (stick_tiers.TIERS entry) - None: the plain mix at level 3


def build_suit_mix(bones):
    """The mix: the original light-rail armor and Novak Glass in one suit. Metal plates are the structure (layered
    petals, belt, knee caps, foot shells, back plate, enclosed helmet), the main faces are panes (breastplate V, the
    outer shoulder petal, forearm blades, thigh plates, greaves, the visor; RF glass or a tier's own pane texture)
    and light rails run over them. Panes are mapped edge to edge onto clean islands (RF's outline goes all round).
    With TIER set, its level decides the pieces and rails (1 base, 2 + layered leaves and first rails, 3 everything,
    4 + the tier's accents)."""
    L = TIER["level"] if TIER else 3
    acc = TIER["acc"] if TIER else ()
    mesh = {n: np.array(b["poly_mesh"]["positions"]) for n, b in bones.items() if "poly_mesh" in b}
    rod = {n: Rod(mesh[n]) for n in ("p22", "p30", "p32", "p37", "p39", "p44", "p46", "p48", "p52", "p54", "p56")}
    DENS.clear()
    stats = {"helmet_clearance_px": helmet(mesh["p17"])}
    D = math.radians
    T = rod["p22"]
    sy = lambda y: (T.top[1] - y) / T.L

    # ---------------- chest: V breastplate between pectoral leaves, back plate with a spine shard; V rails
    V = plate("chest", "p22", T, sy(26.9), sy(20.0), 0.0, D(115), gap=0.14, bulge=0.26, flare=0.30,
              prof=lambda t: (1 - t) ** 0.8 * (0.55 + 0.45 * math.sin(math.pi * min(1, t * 2.2))) + 0.03, ns=12, na=8,
              isl="side_r")
    if L >= 2:
        for sg in (-1, 1):
            plate("chest", "p22", T, sy(26.75), sy(22.6), sg * D(62), D(42), gap=0.46, bulge=0.22, flare=0.15,
                  prof=lambda t: leaf(t, .5, top=.7), twist=-sg * D(20), ns=6, na=4)
    plate("chest", "p22", T, sy(26.7), sy(19.4), D(180), D(62), gap=0.10, bulge=0.18,
          prof=lambda t: leaf(t, .45, top=.6), ns=8, na=5)
    Bk = plate("chest", "p22", T, sy(26.4), sy(20.4), D(180), D(30), gap=0.30, bulge=0.20,
               prof=lambda t: leaf(t, .5, top=.4), ns=10, na=6, isl="side_l")
    if L >= 3:
        rail("chest", "p22", surf_path(Bk, 0, 0.06, 0.94), (0.10, 0.40), width=0.22, n=10)
    if L >= 2:
        for sg in (-1, 1):
            rail("chest", "p22", surf_path(V, 0, 0.47, 0.0, wob=lambda t, sg=sg: sg * min(0.92, 0.08 + 1.75 * (0.47 - t))),
                 (0.0, 0.42), n=12)
        rail("chest", "p22", surf_path(V, 0, 0.47, 0.95), (0.0, 0.22), n=8)
    if "spikes" in acc:   # ECLIPSE: three blades standing off the spine
        for k, (y0, y1, g) in enumerate(((26.2, 24.6, 0.75), (24.4, 22.8, 0.95), (22.6, 21.2, 0.75))):
            plate("chest", "p22", T, sy(y0), sy(y1), D(180), D(14), gap=g, bulge=0.05, flare=0.55,
                  prof=lambda t: leaf(t, .4, top=.9), ns=5, na=3)
    if "shards" in acc:   # PRISM: a crystal blade sweeping back off each elbow (glass, outlined), a light line inside
        for lo, sg in (("p32", 1), ("p39", -1)):   # (on the forearm: above the shoulder the helmet hides it)
            F = rod[lo]
            Sh = plate("chest", lo, F, -0.38, 0.40, sg * D(90) + sg * D(35), D(15), gap=0.62, bulge=0.40, flare=0.9,
                       prof=lambda t: leaf(t, .45, top=.0) * (0.4 + 0.6 * (1 - t)), twist=sg * D(10), ns=10, na=4,
                       isl="leg_end", mirror=sg < 0)
            rail("chest", lo, surf_path(Sh, 0, 0.10, 0.85), (0.60, 0.85), width=0.20, n=8)
    if "fins" in acc:     # (unused) two crystal fins sweeping off the shoulder blades, a rail along each
        for sg in (-1, 1):
            Fn = plate("chest", "p22", T, sy(26.8), sy(19.6), D(180) - sg * D(46), D(18), gap=1.30, bulge=0.35,
                       flare=1.6, prof=lambda t: leaf(t, .5, top=.85), twist=sg * D(14), ns=10, na=4, isl="side_r",
                       mirror=sg < 0)
            rail("chest", "p22", surf_path(Fn, 0, 0.05, 0.95), (0.05, 0.45), width=0.18, n=10)

    # ---------------- chest: shoulder petals (pane under metal scales), forearm pane + metal fin
    fl = 0.30 if "flares" in acc else 0.0   # INFERNO: flared shoulders
    for up, lo, sg in (("p30", "p32", 1), ("p37", "p39", -1)):
        U, F = rod[up], rod[lo]
        out = sg * D(90)
        big = 1.0 if "pauldrons" in acc else 0.0    # VANGUARD: broad round pauldrons
        layers = ((0.0, 0.48 + fl * 0.2 + big * 0.08, 100 + fl * 60 + big * 40, 0.20 + big * 0.22),
                  (0.07, 0.40, 80, 0.44 + big * 0.2), (0.12, 0.33, 58, 0.64 + big * 0.2))
        for k, (s0, s1, w, g) in enumerate(layers[:1 + (L >= 2) + (L >= 3)]):
            P = plate("chest", up, U, s0, s1, out - sg * D(12 * k), D(w), gap=g, bulge=0.18,
                      flare=0.35 - 0.08 * k + fl * (1.4 if k == 0 else 0.6),
                      prof=(lambda t: 1.0) if "boxy" in acc else (lambda t: leaf(t, .55, top=.8)),   # CIRCUIT: square plates
                      twist=sg * D(18), ns=8 if k == 0 else 6, na=6 if k == 0 else 4,
                      isl="arm_top" if k == 0 else None, mirror=sg < 0)
            if k == 1 and L >= 3:
                rail("chest", up, surf_path(P, 0, 0.05, 0.95), (0.42, 0.62), n=8)
        B = plate("chest", lo, F, 0.10, 0.97, out + sg * D(10), D(92), gap=0.10, bulge=0.18,
                  prof=lambda t: leaf(t, .5), twist=sg * D(25), ns=12, na=6, isl="boot", mirror=sg < 0)
        if L >= 3:
            plate("chest", lo, F, 0.42, 1.0, out + sg * D(25), D(55), gap=0.52, bulge=0.18,
                  prof=lambda t: leaf(t, .5), twist=sg * D(30), ns=6, na=3)
            rail("chest", lo, surf_path(B, 0.15, 0.05, 0.95), (0.62, 1.15), n=12)
        if L >= 4:
            rail("chest", lo, surf_path(B, -0.55, 0.25, 0.85), (0.72, 1.05), width=0.16, n=8)

    # ---------------- legs: belt with a front plate and hip petals; thigh panes, metal thigh-backs and knees
    belt = plate("legs", "p22", T, sy(17.7), sy(16.5), 0.0, D(178), gap=0.18, bulge=0.05,
                 prof=lambda t: 1.0, ns=2, na=12)
    plate("legs", "p22", T, sy(17.8), sy(15.3), 0.0, D(36), gap=0.32, bulge=0.12, prof=lambda t: leaf(t, .6, top=1),
          ns=6, na=4, isl="knee")
    if L >= 2:
        for sg in (-1, 1):
            plate("legs", "p22", T, sy(17.6), sy(15.0), sg * D(68), D(32), gap=0.36, bulge=0.15,
                  prof=lambda t: leaf(t, .6, top=1), twist=sg * D(10), ns=5, na=3)
    if L >= 3:
        rail("legs", "p22", surf_path(belt, 0, 0.5, 0.5, wob=lambda t: -0.9 + 1.8 * t), (0.3, 0.3), width=0.18, n=14)
    for th_, sh, sg in (("p44", "p46", 1), ("p52", "p54", -1)):
        R1, R2 = rod[th_], rod[sh]
        A = plate("legs", th_, R1, 0.04, 0.62, sg * D(38), D(70), gap=0.08, bulge=0.12,
                  prof=lambda t: leaf(t, .55, top=.6), twist=sg * D(15), ns=10, na=6, isl="leg_b", mirror=sg < 0)
        Hm = plate("legs", th_, R1, 0.06, 0.86, D(180) - sg * D(22), D(62), gap=0.10, bulge=0.12,
                   prof=lambda t: leaf(t, .5, top=.6), twist=-sg * D(10), ns=10, na=5)
        if L >= 3:
            rail("legs", th_, surf_path(A, 0.1, 0.05, 0.95), (0.30, 0.65), n=10)
            rail("legs", th_, surf_path(Hm, 0, 0.08, 0.90), (0.34, 0.70), width=0.24, n=10)
        plate("legs", sh, R2, 0.0, 0.17, 0.0, D(74), gap=0.12, bulge=0.22,
              prof=lambda t: leaf(t, .4 if "flares" not in acc else .25), ns=4, na=4)

    # ---------------- feet: greave panes with swept rails, calf panes, side fins and foot shells
    for sh, ft, sg in (("p46", "p48", 1), ("p54", "p56", -1)):
        R2, R3 = rod[sh], rod[ft]
        G = plate("feet", sh, R2, 0.20, 0.93, sg * D(10), D(118), gap=0.08, bulge=0.12,
                  prof=lambda t: 0.25 + 0.75 * math.sin(math.pi * min(1, t * 0.62 + 0.05)) ** .7, ns=12, na=8,
                  isl="side_r" if sg > 0 else "side_l")
        plate("feet", sh, R2, 0.16, 0.86, D(180) - sg * D(15), D(66), gap=0.10, bulge=0.14,
              prof=lambda t: leaf(t, .5, top=.55), twist=-sg * D(10), ns=10, na=6, isl="boot", mirror=sg < 0)
        if L >= 3:
            plate("feet", sh, R2, 0.26, 0.72, sg * D(70), D(34), gap=0.30, bulge=0.16, prof=lambda t: leaf(t, .5),
                  twist=-sg * D(22), ns=6, na=3)
        if L >= 2:
            for kk, arr in (((0.55, (0.66, 1.25)), (-0.55, (0.72, 1.32))) if L >= 3 else ((0.0, (0.66, 1.25)),)):
                rail("feet", sh, surf_path(G, 0, 0.04, 0.97, wob=lambda t, kk=kk: kk * (0.55 + 0.35 * math.sin(t * math.pi))),
                     arr, n=14)
        plate("feet", ft, R3, 0.0, 1.0, 0.0, D(150), gap=0.12, bulge=0.12,
              prof=lambda t: 0.6 + 0.4 * math.sin(math.pi * t), ns=5, na=6)
    body_accents(acc, rod, T, sy, D)
    stats["texels_per_px"] = {f"{s}/{b}/{i}": d for s, b, i, d in DENS}
    return stats


# ====================================================================== textures
def engrave(col, name, b, d, h, neon):
    """the mix's metal: art-directed relief on the plate region (32 texels across a plate, 64 along it), sparse
    enough to stay readable on the small plates (no noise, no texture-wide pattern):
      * the plate rounded across (lit in the middle, darker toward the bevel)
      * a keel down the middle - one lit texel, its shadow beside it
      * an inlaid line in the glass's neon hue just inside the bevel groove (leather: stitching)
      * three engraved chevron seams pointing down the plate (groove + lit lip under it): segmented, like plate armor
      * rivets between the seams, inside the inlay"""
    H_, W_ = col.shape[:2]
    yy, xx = np.mgrid[0:H_, 0:W_]
    u, v = (xx + .5) / W_, (yy + .5) / H_
    edge = np.minimum(u, 1 - u)
    body = (edge >= .11)
    col = col * np.where(body, 0.90 + 0.16 * np.cos((u - .5) * math.pi * 1.6), 1.0)[..., None]
    lit, shadow = h * 0.80 + b * 0.20, d * 0.85 + b * 0.15
    # chevron seams (v at the centre line; the arms rise toward the edges)
    for v0 in (0.27, 0.52, 0.76):
        r = np.round((v0 - 0.20 * np.abs(u - .5)) * H_ - .5).astype(int)
        m = body & (edge >= .17)
        col[m & (yy == r)] = shadow
        col[m & (yy == r + 1)] = lit * 0.9 + col[m & (yy == r + 1)] * 0.1
    # keel
    ridge = (v > .05) & (v < .95)
    col[ridge & (xx == W_ // 2 - 1)] = lit
    col[ridge & (xx == W_ // 2)] = col[ridge & (xx == W_ // 2)] * 0.55 + shadow * 0.45
    # inlay just inside the bevel groove
    inl = (edge >= .11) & (edge < .145) & (v > .03) & (v < .97)
    if name == "leather":
        col[inl & (yy % 4 < 2)] = h * 0.95
    else:
        col[inl] = neon * 0.72 + b * 0.28
    # rivets
    for v0 in (0.14, 0.40, 0.64, 0.88):
        for x0 in (6, W_ - 7):
            y0 = int(v0 * H_)
            col[y0, x0] = h * 0.85 + 255 * 0.15
            col[y0 + 1, x0] = shadow
    return np.clip(col, 0, 255)


def metal_atlas(name, body, dark, hl, accent, polish=False):
    """polish (the mix): a clean polished finish next to the glass - the brushed rows read as stripes on the
    helmet's dome (one texel row per latitude) and the sparse glint pixels as noise"""
    if polish and name == "leather":   # the mix: neutral leather, coloured by the item's dye (Hive team colours)
        body, dark, hl, accent = (214, 210, 204), (82, 78, 74), (250, 248, 244), (255, 255, 255)
    b, d, h, a = (np.array(c, float) for c in (body, dark, hl, accent))
    img = np.zeros((TEX, TEX, 4), float)
    img[..., 3] = 255
    yy, xx = np.mgrid[0:TEX, 0:TEX]
    rng = np.random.default_rng(sum(map(ord, name)))
    n1 = rng.random((TEX, TEX))
    W = TEX // 2
    u = (xx[:, :W] + .5) / W
    v = (yy[:, :W] + .5) / TEX
    shade = 1.08 - 0.30 * v + 0.10 * np.cos(u * math.pi * 2)
    brush = (np.repeat(rng.random((TEX, 1)), W, 1) - .5) * (0.02 if polish else 0.10)
    col = b[None, None] * (shade + brush)[..., None]
    if name == "chainmail":      # interlinked mesh insert, gunmetal borders (polish: a quieter mesh - at a few
        cx, cy = xx[:, :W] % 3, (yy[:, :W] + (xx[:, :W] // 3) % 2) % 3   # px per texel the full-contrast one is noise)
        inside = (u > .22) & (u < .78)
        k = 0.15 if polish else 1.0
        ring = ((cx == 1) ^ (cy == 1)) & inside
        hole = (cx == 1) & (cy == 1) & inside
        col[ring] = col[ring] * (1 - k) + h * 0.85 * k
        col[hole] = col[hole] * (1 - k) + d * 0.7 * k
    elif name == "leather":      # grain + stitched seam (polish: fine grain, no seam - on the dome it reads as a zip)
        col *= (0.92 + (0.05 if polish else 0.12) * n1[:, :W])[..., None]
        if not polish:
            col[(np.abs(u - .5) < .02) & (yy[:, :W] % 4 < 2)] = a * 0.85
    elif name in ("diamond", "netherite", "iron"):   # crisp facets: diagonal light/dark bands
        fac = ((xx[:, :W] + yy[:, :W] // 2) // 6) % 2
        col *= (0.92 + (0.16 if name != "iron" else 0.08) * (0.4 if polish else 1.0) * fac)[..., None]
    edge = np.minimum(u, 1 - u)
    col = np.where((edge < .07)[..., None], (h * 0.62 + b * 0.38)[None, None] * (1.0 - edge[..., None] * 2.5), col)   # bevel (kept below the rails)
    col = np.where(((edge >= .07) & (edge < .11))[..., None], d[None, None] * 1.2, col)          # dark line
    if not polish:
        col[(n1[:, :W] > 0.996) & (edge > .12)] = h * 0.6 + 255 * 0.4                             # sparse glints
    else:
        col = engrave(col, name, b, d, h, np.array(RAIL_NEON[name], float))
    img[:, :W, :3] = col
    x2 = slice(W, W + TEX // 4)                                     # visor: deep tinted glass + reflection streak
    uu = (xx[:, x2] - W + .5) / (TEX // 4)
    vv = (yy[:, x2] + .5) / TEX
    streak = np.exp(-((vv - 0.22 - uu * 0.18) / 0.05) ** 2) * 0.5
    img[:, x2, :3] = (d * 0.30 + np.array([4, 6, 10.0]))[None, None] + (h * 0.35)[None, None] * streak[..., None]
    img[:, W + TEX // 4: W + 3 * TEX // 8, :3] = h * 0.55 + b * 0.45  # rim: bevel
    img[:, W + 3 * TEX // 8:, :3] = d * 0.75                          # recess: graphite
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")


def energy_strip(accent, alpha, base=0.50):
    """vertical strip, pattern twice (period 0.5): dim continuous track + bright travelling packet with a tail"""
    a = np.array(accent, float)
    img = np.zeros((FXH, FXW, 4), float)
    for y in range(FXH):
        v = (y + .5) / FXH
        x = (v - 0.25) % 0.5
        dpk = min(x, 0.5 - x)
        # the offset grows with time, so smaller v is reached later: the trail behind the packet is at larger v
        tail = math.exp(-(x / 0.10)) * 0.50 if x < 0.25 else 0.0
        g = base + 1.2 * math.exp(-(dpk / 0.05) ** 2) + tail
        img[y, :, :3] = np.clip(a * min(g, 1.0) + 255 * max(0.0, g - 0.85) * 0.9, 0, 255)
        img[y, :, 3] = alpha
    return Image.fromarray(img.astype(np.uint8), "RGBA")


# ====================================================================== output
def build_tiers(stick_geo: Path, player_entity, figura_rc, out: Path):
    """STYLE "tiers": every material its own suit and textures (stick_tiers.TIERS). Per slot three render
    controllers (plates, rails, panes) pick the material's geometry / texture / material from arrays."""
    global MESH, TIER
    import importlib.util
    spec = importlib.util.spec_from_file_location("stick_tiers", Path(__file__).with_name("stick_tiers.py"))
    st = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(st)
    g = json.loads(Path(stick_geo).read_text())["minecraft:geometry"][0]
    bones = {b["name"]: b for b in g["bones"]}
    names = [m[0] for m in MATERIALS]
    meshes, stats = {}, {"tiers": {}}
    for mat in names:
        MESH, TIER = {}, st.TIERS[mat]
        ss = build_suit_mix(bones)
        meshes[mat] = MESH
        stats["tiers"][mat] = {"name": TIER["name"], "level": TIER["level"],
                               "triangles": sum(m.tris() for (sl, _, _), m in MESH.items()
                                                if sl == "head" or mat != "turtle")}
    TIER = None
    count = {s: len(ITEMS[s]) for s in SLOTS}            # turtle only has a helmet
    has = {(s, l): any(any(mm.polys for (ss_, ll, _), mm in meshes[m].items() if ss_ == s and ll == l)
                       for m in names[:count[s]]) for s in SLOTS for l in LAYERS}
    geos = []
    for s in SLOTS:
        for layer, (suffix, tw, th) in LAYERS.items():
            if not has[s, layer]:
                continue
            for mat in names[:count[s]]:
                bl = []
                for b in g["bones"]:
                    nb = {k: v for k, v in b.items() if k in ("name", "parent", "pivot", "rotation")}
                    mm = meshes[mat].get((s, layer, b["name"]))
                    if mm and mm.polys:
                        nb["poly_mesh"] = mm.json()
                    bl.append(nb)
                geos.append({"description": {"identifier": f"geometry.fb_armor.stick.{s}{suffix}.{mat}",
                                             "texture_width": tw, "texture_height": th,
                                             "visible_bounds_width": 4, "visible_bounds_height": 5,
                                             "visible_bounds_offset": [0, 2, 0]}, "bones": bl})
    out = Path(out)
    (out / "models/entity").mkdir(parents=True, exist_ok=True)
    (out / "models/entity/fb_armor_stick.geo.json").write_text(
        json.dumps({"format_version": "1.21.0", "minecraft:geometry": geos}, separators=(",", ":")))
    td = out / "textures/figura/fb_armor"
    td.mkdir(parents=True, exist_ok=True)
    for mat in names:
        if st.TIERS[mat].get("mix"):   # the approved mix finish: engraved polished metal, rails in the glass's neon
            _, body, dark, hl, acc_ = next(m for m in MATERIALS if m[0] == mat)
            im = metal_atlas(mat, body, dark, hl, acc_, polish=True).resize((st.TEXM, st.TEXM), Image.NEAREST)
            cl = st.plate_tex(mat)   # the tier's cloth lives in the recess region (capes, collar)
            x0 = int(REG["recess"][0] * st.TEXM)
            im.paste(cl.crop((x0, 0, st.TEXM, st.TEXM)), (x0, 0))
            im.save(td / f"t_{mat}.png")
            energy_strip(RAIL_NEON[mat], 0, base=0.78).save(td / f"t_{mat}_fx.png")
        else:
            st.plate_tex(mat).save(td / f"t_{mat}.png")
            st.rail_tex(mat).save(td / f"t_{mat}_fx.png")
        if not st.TIERS[mat]["rf"]:
            for layer in (1, 2):
                st.pane_tex(mat, layer, ISLAND, ISLAND_LAYER).save(td / f"t_{mat}_pane{layer}.png")

    def idx(var, m):
        return f"v.{var} == {names.index(m) + 1}"

    def pick(var, vals, default):   # a ternary chain over the material index
        e = default
        for m, val in vals.items():
            e = f"({idx(var, m)} ? ({val}) : {e})"
        return e
    rf = [m for m in names if st.TIERS[m]["rf"]]
    react = [m for m in names if st.TIERS[m]["react"]]
    rcs = {}
    for s in SLOTS:
        var, slot_i, n = VAR[s], SLOTS.index(s), count[s]
        on = f"(v.{var} ?? 0) > 0 && !variable.is_first_person && (v.fbstickarmor ?? 0)"
        dye = {c: f"{idx(var, 'leather')} ? q.armor_color_slot({slot_i}, {i}) : 1.0" for i, c in enumerate("rgb")}
        ov = {}
        if react:
            o = {m: st.react_overlay(var, m) for m in react}
            ov = {"overlay_color": {c: pick(var, {m: o[m][c] for m in react}, "0.0" if c == "a" else "1.0")
                                    for c in "rgba"}}
        geo_arr = lambda suffix: {"Array.geo": [f"Geometry.fbarmor_{s}{suffix}_{m}" for m in names[:n]]}
        if has[s, "metal"]:
            rcs[f"controller.render.player.fb_armor_{s}_metal"] = {
                "arrays": {"textures": {"Array.t": [f"Texture.fbt_{m}" for m in names[:n]]}, "geometries": geo_arr("_metal")},
                "geometry": f"Array.geo[v.{var} - 1]", "textures": [f"Array.t[v.{var} - 1]"],
                "materials": [{"*": f"{idx(var, 'leather')} ? Material.fbaml : Material.fbame"}],
                "color": {**dye, "a": 1.0}, **ov}
        if has[s, "fx"]:
            rcs[f"controller.render.player.fb_armor_{s}_fx"] = {
                "arrays": {"textures": {"Array.t": [f"Texture.fbtfx_{m}" for m in names[:n]]}, "geometries": geo_arr("_fx")},
                "geometry": f"Array.geo[v.{var} - 1]", "textures": [f"Array.t[v.{var} - 1]"],
                "materials": [{"*": "Material.fbamfx"}],
                "uv_anim": {"offset": [0.0, f"(v.fbaph_{s} ?? 0) * 0.5"], "scale": [1.0, 1.0]}, **ov}
        if has[s, "glass"]:
            tex = [f"Texture.fbam{LAYER[s]}_{m}" if m in rf else f"Texture.fbtp{LAYER[s]}_{m}" for m in names[:n]]
            mats_ = " || ".join(idx(var, m) for m in rf)
            rcs[f"controller.render.player.fb_armor_{s}"] = {
                "arrays": {"textures": {"Array.t": tex}, "geometries": geo_arr("")},
                "geometry": f"Array.geo[v.{var} - 1]", "textures": [f"Array.t[v.{var} - 1]"],
                "materials": [{"*": f"{idx(var, 'leather')} ? Material.fbaml : (({mats_}) ? Material.fbam : Material.fbame)"}],
                "color": {**dye, "a": 1.0}}
    (out / "render_controllers").mkdir(parents=True, exist_ok=True)
    (out / "render_controllers/fb_stick_armor.render_controllers.json").write_text(
        json.dumps({"format_version": "1.10.0", "render_controllers": rcs}, indent=1))
    stats["total_triangles"] = sum(mm.tris() for M_ in meshes.values() for mm in M_.values())

    if player_entity is not None:
        ent = json.loads(Path(player_entity).read_text())
        d = ent["minecraft:client_entity"]["description"]
        for k in [k for k in d["geometry"] if k.startswith("fbarmor_")]:
            del d["geometry"][k]
        for s in SLOTS:
            for layer, (suffix, _, _) in LAYERS.items():
                if has[s, layer]:
                    for m in names[:count[s]]:
                        d["geometry"][f"fbarmor_{s}{suffix}_{m}"] = f"geometry.fb_armor.stick.{s}{suffix}.{m}"
        for k in [k for k in d["textures"] if k.startswith(("fbam_", "fbam1_", "fbam2_", "fbamfx_", "fbt"))]:
            del d["textures"][k]
        for m in names:
            d["textures"][f"fbt_{m}"] = f"textures/figura/fb_armor/t_{m}"
            d["textures"][f"fbtfx_{m}"] = f"textures/figura/fb_armor/t_{m}_fx"
            for layer in (1, 2):
                d["textures"][f"fbtp{layer}_{m}" if m not in rf else f"fbam{layer}_{m}"] = \
                    f"textures/figura/fb_armor/t_{m}_pane{layer}" if m not in rf else f"textures/models/armor/{VANILLA[m]}_{layer}"
        d["materials"]["fbam"] = "entity_alphatest"                   # Renderer Foundation glass (tier 4 panes)
        d["materials"]["fbaml"] = "entity_alphatest_change_color"     # leather: the item's dye
        d["materials"]["fbame"] = "entity_emissive_alpha"             # the tiers' glowing designs (alpha 0 = glow)
        d["materials"]["fbamfx"] = "figura_uv_emissive"
        pre = d["scripts"].setdefault("pre_animation", [])
        mine = ("variable.fbstickarmor", "variable.fbaph", "variable.fbarate",
                *(f"variable.{VAR[s]} " for s in SLOTS), *(f"variable.{VAR[s]}t " for s in SLOTS))
        pre[:] = [l for l in pre if not l.startswith(mine)]
        if not any(l.startswith("variable.fbnoarmor") for l in pre):
            pre.append("variable.fbnoarmor = (v.fbvan ?? 1) ? 0.0 : 1.0;")
        pre.append("variable.fbstickarmor = 1.0;")
        d["scripts"].setdefault("variables", {})["variable.fbnoarmor"] = "public"
        r0, r1 = f"{1 / PERIOD:.4f}", f"{1 / SPRINT_PERIOD:.4f}"
        pre.append(f"variable.fbarate = v.fbnoarmor ? (v.fbarate ?? {r0}) + ((q.is_sprinting ? {r1} : {r0}) - (v.fbarate ?? {r0}))"
                   f" * math.min(1, q.delta_time * 3) : {r0};")
        for s in SLOTS:
            terms = " + ".join(f"q.is_item_name_any('{SLOT_NAME[s]}', 0, 'minecraft:{it}') * {i + 1}"
                               for i, it in enumerate(ITEMS[s]))
            pre.append(f"variable.{VAR[s]} = v.fbnoarmor ? ({terms}) : 0.0;")
            pre.append(f"variable.{VAR[s]}t = v.{VAR[s]} > 0 ? math.min((v.{VAR[s]}t ?? 0) + q.delta_time, 10) : 0.0;")
            # each slot's rail phase runs at its tier's speed (sprinting speeds every tier up)
            k = pick(VAR[s], {m: st.TIERS[m]["speed"] for m in names[:count[s]]}, "1.0")
            pre.append(f"variable.fbaph_{s} = v.{VAR[s]} > 0 ? math.mod((v.fbaph_{s} ?? 0) + q.delta_time * v.fbarate * {k}, 1.0) : 0.0;")
        rc = d["render_controllers"]
        rc[:] = [r for r in rc if not (isinstance(r, dict) and any(k.startswith("controller.render.player.fb_armor_") for k in r))]
        for s in SLOTS:
            on = f"(v.{VAR[s]} ?? 0) > 0 && !variable.is_first_person && (v.fbstickarmor ?? 0)"
            if has[s, "metal"]:
                rc.append({f"controller.render.player.fb_armor_{s}_metal": on})
            if has[s, "fx"]:
                rc.append({f"controller.render.player.fb_armor_{s}_fx": f"{on} && (v.{VAR[s]}t ?? 0) > {START[s]}"})
            if has[s, "glass"]:
                rc.append({f"controller.render.player.fb_armor_{s}": on})
        (out / "entity").mkdir(parents=True, exist_ok=True)
        (out / "entity/player.entity.json").write_text(json.dumps(ent, indent=1))
    if figura_rc is not None:   # hide the outline disc p6 while a helmet is worn
        R = json.loads(Path(figura_rc).read_text())
        tag = "!((v.fbstickarmor ?? 0) && (v.fbah ?? 0) > 0)"
        for c in R["render_controllers"].values():
            for pv in c.get("part_visibility", []):
                if isinstance(pv.get("p6"), str) and tag not in pv["p6"]:
                    pv["p6"] = f"({pv['p6']}) && {tag}"
        (out / "render_controllers").mkdir(parents=True, exist_ok=True)
        (out / "render_controllers/figura_player.render_controllers.json").write_text(json.dumps(R, separators=(",", ":")))
    return stats


# geometry suffix and texture size of each mesh layer (an empty layer is not emitted)
LAYERS = {"metal": ("_metal", TEX, TEX), "fx": ("_fx", FXW, FXH), "glass": ("", 64, 32)}


def build(stick_geo: Path, player_entity, figura_rc, out: Path, fx_alpha=0):
    global MESH
    if STYLE == "tiers":
        return build_tiers(stick_geo, player_entity, figura_rc, out)
    MESH = {}
    g = json.loads(Path(stick_geo).read_text())["minecraft:geometry"][0]
    bones = {b["name"]: b for b in g["bones"]}
    stats = {"glass": build_suit_glass, "mix": build_suit_mix}.get(STYLE, build_suit)(bones)   # (tiers: build_tiers)
    has = {(s, l): any(m.polys for (ss, ll, _), m in MESH.items() if ss == s and ll == l) for s in SLOTS for l in LAYERS}
    used = {l: any(has[s, l] for s in SLOTS) for l in LAYERS}
    geos = []
    for s in SLOTS:
        for layer, (suffix, tw, th) in LAYERS.items():
            if not has[s, layer]:
                continue
            bl = []
            for b in g["bones"]:
                nb = {k: v for k, v in b.items() if k in ("name", "parent", "pivot", "rotation")}
                m = MESH.get((s, layer, b["name"]))
                if m and m.polys:
                    nb["poly_mesh"] = m.json()
                bl.append(nb)
            geos.append({"description": {"identifier": f"geometry.fb_armor.stick.{s}{suffix}",
                                         "texture_width": tw, "texture_height": th,
                                         "visible_bounds_width": 4, "visible_bounds_height": 5,
                                         "visible_bounds_offset": [0, 2, 0]}, "bones": bl})
    out = Path(out)
    (out / "models/entity").mkdir(parents=True, exist_ok=True)
    (out / "models/entity/fb_armor_stick.geo.json").write_text(
        json.dumps({"format_version": "1.21.0", "minecraft:geometry": geos}, separators=(",", ":")))
    td = out / "textures/figura/fb_armor"
    td.mkdir(parents=True, exist_ok=True)
    for name, body, dark, hl, acc in MATERIALS:
        if used["metal"]:
            metal_atlas(name, body, dark, hl, acc, polish=STYLE in ("mix", "tiers")).save(td / f"{name}.png")
        if used["fx"]:   # mix: the rails carry the glass's neon (its hue, a lit track), not the metal's pale accent
            if STYLE in ("mix", "tiers"):
                energy_strip(RAIL_NEON[name], fx_alpha, base=0.78).save(td / f"{name}_fx.png")
            else:
                energy_strip(acc, fx_alpha).save(td / f"{name}_fx.png")

    rcs = {}
    for s in SLOTS:
        slot_i = SLOTS.index(s)
        dye = lambda ch: f"v.{VAR[s]} == 1 ? q.armor_color_slot({slot_i}, {ch}) : 1.0"
        if has[s, "metal"]:
            rcs[f"controller.render.player.fb_armor_{s}_metal"] = {
                "arrays": {"textures": {"Array.fbam": [f"Texture.fbam_{m[0]}" for m in MATERIALS]}},
                "geometry": f"Geometry.fbarmor_{s}_metal", "materials": [{"*": "Material.fbam"}],
                "textures": [f"Array.fbam[v.{VAR[s]} - 1]"]}
            if STYLE in ("mix", "tiers"):   # leather metal takes the item's dye too
                rcs[f"controller.render.player.fb_armor_{s}_metal"].update({
                    "materials": [{"*": f"v.{VAR[s]} == 1 ? Material.fbaml : Material.fbam"}],
                    "color": {"r": dye(0), "g": dye(1), "b": dye(2), "a": 1.0}})
        if has[s, "fx"]:
            rcs[f"controller.render.player.fb_armor_{s}_fx"] = {
                "arrays": {"textures": {"Array.fbamfx": [f"Texture.fbamfx_{m[0]}" for m in MATERIALS]}},
                "geometry": f"Geometry.fbarmor_{s}_fx", "materials": [{"*": "Material.fbamfx"}],
                "textures": [f"Array.fbamfx[v.{VAR[s]} - 1]"],
                "uv_anim": {"offset": [0.0, "(v.fbaph ?? 0) * 0.5"], "scale": [1.0, 1.0]}}
        if has[s, "glass"]:   # the game's own armor texture of this slot's layer; leather tinted with the item's dye
            rcs[f"controller.render.player.fb_armor_{s}"] = {
                "arrays": {"textures": {"Array.fbamg": [f"Texture.fbam{LAYER[s]}_{m[0]}" for m in MATERIALS]}},
                "geometry": f"Geometry.fbarmor_{s}",
                "materials": [{"*": f"v.{VAR[s]} == 1 ? Material.fbaml : Material.fbam"}],
                "textures": [f"Array.fbamg[v.{VAR[s]} - 1]"],
                "color": {"r": dye(0), "g": dye(1), "b": dye(2), "a": 1.0}}
    (out / "render_controllers").mkdir(parents=True, exist_ok=True)
    (out / "render_controllers/fb_stick_armor.render_controllers.json").write_text(
        json.dumps({"format_version": "1.10.0", "render_controllers": rcs}, indent=1))

    stats["triangles"] = {f"{s}/{l}": sum(m.tris() for (ss, ll, _), m in MESH.items() if ss == s and ll == l)
                          for s in SLOTS for l in ("metal", "fx")}
    stats["total_triangles"] = sum(m.tris() for m in MESH.values())

    if player_entity is not None:
        ent = json.loads(Path(player_entity).read_text())
        d = ent["minecraft:client_entity"]["description"]
        for s in SLOTS:
            for layer, (suffix, _, _) in LAYERS.items():
                d["geometry"].pop(f"fbarmor_{s}{suffix}", None)
                if has[s, layer]:
                    d["geometry"][f"fbarmor_{s}{suffix}"] = f"geometry.fb_armor.stick.{s}{suffix}"
        for k in [k for k in d["textures"] if k.startswith(("fbam_", "fbam1_", "fbam2_", "fbamfx_"))]:
            del d["textures"][k]
        for m in MATERIALS:
            if used["glass"]:
                for layer in (1, 2):
                    d["textures"][f"fbam{layer}_{m[0]}"] = f"textures/models/armor/{VANILLA[m[0]]}_{layer}"
            if used["metal"]:
                d["textures"][f"fbam_{m[0]}"] = f"textures/figura/fb_armor/{m[0]}"
            if used["fx"]:
                d["textures"][f"fbamfx_{m[0]}"] = f"textures/figura/fb_armor/{m[0]}_fx"
        d["materials"]["fbam"] = "entity_alphatest"
        d["materials"]["fbaml"] = "entity_alphatest_change_color"   # leather: the item's dye (RC color)
        d["materials"].pop("fbamfx", None)
        if used["fx"]:
            d["materials"]["fbamfx"] = "figura_uv_emissive"
        pre = d["scripts"].setdefault("pre_animation", [])
        mine = ("variable.fbstickarmor", "variable.fbaph", "variable.fbarate",
                *(f"variable.{VAR[s]} " for s in SLOTS), *(f"variable.{VAR[s]}t " for s in SLOTS))
        pre[:] = [l for l in pre if not l.startswith(mine)]
        if not any(l.startswith("variable.fbnoarmor") for l in pre):
            pre.append("variable.fbnoarmor = (v.fbvan ?? 1) ? 0.0 : 1.0;")
        pre.append("variable.fbstickarmor = 1.0;")
        # attachables read it from their owner (c.owning_entity->v.fbnoarmor): only public variables are visible there
        d["scripts"].setdefault("variables", {})["variable.fbnoarmor"] = "public"
        for s in SLOTS:
            terms = " + ".join(f"q.is_item_name_any('{SLOT_NAME[s]}', 0, 'minecraft:{it}') * {i + 1}"
                               for i, it in enumerate(ITEMS[s]))
            pre.append(f"variable.{VAR[s]} = v.fbnoarmor ? ({terms}) : 0.0;")
            # seconds since this slot was equipped (0 while empty): drives the equip light-up sequence
            if used["fx"]:
                pre.append(f"variable.{VAR[s]}t = v.{VAR[s]} > 0 ? math.min((v.{VAR[s]}t ?? 0) + q.delta_time, 10) : 0.0;")
        r0, r1 = f"{1 / PERIOD:.4f}", f"{1 / SPRINT_PERIOD:.4f}"
        # light-rail timers: only on the avatar's player (every other player skips them: v.fbnoarmor = 0)
        if used["fx"]:
            pre.append(f"variable.fbarate = v.fbnoarmor ? (v.fbarate ?? {r0}) + ((q.is_sprinting ? {r1} : {r0}) - (v.fbarate ?? {r0}))"
                       f" * math.min(1, q.delta_time * 3) : {r0};")
            pre.append("variable.fbaph = v.fbnoarmor ? math.mod((v.fbaph ?? 0) + q.delta_time * v.fbarate, 1.0) : 0.0;")
        rc = d["render_controllers"]
        rc[:] = [r for r in rc if not (isinstance(r, dict) and any(k.startswith("controller.render.player.fb_armor_") for k in r))]
        for s in SLOTS:
            on = f"(v.{VAR[s]} ?? 0) > 0 && !variable.is_first_person && (v.fbstickarmor ?? 0)"  # 0 on other players
            # order: metal, then the rails, then the glass - RF's glass refracts what is already drawn
            if has[s, "metal"]:
                rc.append({f"controller.render.player.fb_armor_{s}_metal": on})
            if has[s, "fx"]:
                rc.append({f"controller.render.player.fb_armor_{s}_fx": f"{on} && (v.{VAR[s]}t ?? 0) > {START[s]}"})
            if has[s, "glass"]:
                rc.append({f"controller.render.player.fb_armor_{s}": on})
        (out / "entity").mkdir(parents=True, exist_ok=True)
        (out / "entity/player.entity.json").write_text(json.dumps(ent, indent=1))

    if figura_rc is not None:   # hide the outline disc p6 while a helmet is worn, composed with its own visibility
        R = json.loads(Path(figura_rc).read_text())
        tag = "!((v.fbstickarmor ?? 0) && (v.fbah ?? 0) > 0)"
        n = 0
        for c in R["render_controllers"].values():
            for pv in c.get("part_visibility", []):
                if isinstance(pv.get("p6"), str) and tag not in pv["p6"]:
                    pv["p6"] = f"({pv['p6']}) && {tag}"
                    n += 1
        stats["p6_patched"] = n
        (out / "render_controllers").mkdir(parents=True, exist_ok=True)
        (out / "render_controllers/figura_player.render_controllers.json").write_text(json.dumps(R, separators=(",", ":")))
    return stats


if __name__ == "__main__":
    a = sys.argv[1:]
    geo, outd = Path(a[0]), Path(a[-1])
    ent = Path(a[1]) if len(a) >= 3 else None
    frc = Path(a[2]) if len(a) >= 4 else None
    print(json.dumps(build(geo, ent, frc, outd), indent=1))
