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

    def quad(self, P, uvs, n=None):
        """P: 3 or 4 points, counter-clockwise seen from outside; one flat normal (facets)"""
        P = [np.asarray(p, float) for p in P]
        if n is None:
            n = np.cross(P[1] - P[0], P[2] - P[0])
            if len(P) == 4:
                n = n + np.cross(P[2] - P[0], P[3] - P[0])
        L = np.linalg.norm(n)
        if L < 1e-9:
            return
        n = n / L
        ni = self._idx(self._n, self.nrm, n)
        poly = [[self._idx(self._p, self.pos, p), ni, self._idx(self._u, self.uv, uv, 5)] for p, uv in zip(P, uvs)]
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


def plate(slot, bone, rod, s0, s1, ac, W, gap=0.12, bulge=0.25, flare=0.0, prof=None, twist=0.0,
          ns=8, na=5, th=0.14, region="metal", rim=True):
    """a curved plate wrapped round `rod`: centre angle ac (rad), half width W*prof(t) (rad), radius = rod clearance +
    gap, bulging `bulge` in the middle and flaring `flare` toward s0. Returns its surface S(t, k, dr), k in -1..1"""
    prof = prof or (lambda t: leaf(t))
    base = rod.clear(s0, s1) + gap
    m = M(slot, bone)

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
    for i in range(ns):
        for j in range(na):
            t0, t1 = i / ns, (i + 1) / ns
            k0, k1 = -1 + 2 * j / na, -1 + 2 * (j + 1) / na
            P = [S(t0, k0), S(t0, k1), S(t1, k1), S(t1, k0)]
            uv = [(u0 + (u1 - u0) * (k + 1) / 2, t) for t, k in ((t0, k0), (t0, k1), (t1, k1), (t1, k0))]
            emit(P, uv)
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

    NP, NT = 10, 16
    m = M("head", "p17")
    for i in range(NP):
        for j in range(NT):
            p0, p1 = math.pi * i / NP, math.pi * (i + 1) / NP
            a0, a1 = 2 * math.pi * j / NT - math.pi, 2 * math.pi * (j + 1) / NT - math.pi
            ac = (a0 + a1) / 2
            vis = abs(ac) < 1.15 and 0.36 * math.pi < (p0 + p1) / 2 < 0.86 * math.pi
            u0, u1 = REG["visor" if vis else "metal"]
            corners = ((p0, a0), (p1, a0), (p1, a1), (p0, a1))
            P = [H(p, a) for p, a in corners]
            if vis:
                uv = [(u0 + (u1 - u0) * (0.5 + 0.5 * a / 1.2), min(1, max(0, (p - 0.36 * math.pi) / (0.5 * math.pi))))
                      for p, a in corners]
            else:
                uv = [(u0 + (u1 - u0) * (0.2 + 0.6 * (a + math.pi) / (2 * math.pi)), p / math.pi) for p, a in corners]
            if i == 0:   # poles: triangles
                P, uv = [P[0], P[1], P[2]], uv[:3]
            elif i == NP - 1:
                P, uv = [P[0], P[1], P[3]], [uv[0], uv[1], uv[3]]
            n = np.cross(P[1] - P[0], P[2] - P[0])
            if n @ (np.mean(P, 0) - hc) < 0:
                P, uv = P[::-1], uv[::-1]
            m.quad(P, uv)

    def cheek(sign):   # from the brow down both visor edges, sweeping in to the chin
        def p(t):
            phi = 0.30 * math.pi + 0.56 * math.pi * t
            th = sign * (1.22 - 0.95 * t ** 1.6)
            P = H(phi, th)
            n = P - hc
            return P, n / np.linalg.norm(n)
        return p
    for sg in (-1, 1):
        rail("head", "p17", cheek(sg), (PERIOD - 0.25, PERIOD + 0.2), width=0.32, n=16)
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


# ====================================================================== textures
def metal_atlas(name, body, dark, hl, accent):
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
    brush = (np.repeat(rng.random((TEX, 1)), W, 1) - .5) * 0.10
    col = b[None, None] * (shade + brush)[..., None]
    if name == "chainmail":      # interlinked mesh insert, gunmetal borders
        cx, cy = xx[:, :W] % 3, (yy[:, :W] + (xx[:, :W] // 3) % 2) % 3
        inside = (u > .22) & (u < .78)
        col[((cx == 1) ^ (cy == 1)) & inside] = h * 0.85
        col[(cx == 1) & (cy == 1) & inside] = d * 0.7
    elif name == "leather":      # grain + stitched seam
        col *= (0.92 + 0.12 * n1[:, :W])[..., None]
        col[(np.abs(u - .5) < .02) & (yy[:, :W] % 4 < 2)] = a * 0.85
    elif name in ("diamond", "netherite", "iron"):   # crisp facets: diagonal light/dark bands
        fac = ((xx[:, :W] + yy[:, :W] // 2) // 6) % 2
        col *= (0.92 + (0.16 if name != "iron" else 0.08) * fac)[..., None]
    edge = np.minimum(u, 1 - u)
    col = np.where((edge < .07)[..., None], (h * 0.62 + b * 0.38)[None, None] * (1.0 - edge[..., None] * 2.5), col)   # bevel (kept below the rails)
    col = np.where(((edge >= .07) & (edge < .11))[..., None], d[None, None] * 1.2, col)          # dark line
    col[(n1[:, :W] > 0.996) & (edge > .12)] = h * 0.6 + 255 * 0.4                                 # sparse glints
    img[:, :W, :3] = col
    x2 = slice(W, W + TEX // 4)                                     # visor: deep tinted glass + reflection streak
    uu = (xx[:, x2] - W + .5) / (TEX // 4)
    vv = (yy[:, x2] + .5) / TEX
    streak = np.exp(-((vv - 0.22 - uu * 0.18) / 0.05) ** 2) * 0.5
    img[:, x2, :3] = (d * 0.30 + np.array([4, 6, 10.0]))[None, None] + (h * 0.35)[None, None] * streak[..., None]
    img[:, W + TEX // 4: W + 3 * TEX // 8, :3] = h * 0.55 + b * 0.45  # rim: bevel
    img[:, W + 3 * TEX // 8:, :3] = d * 0.75                          # recess: graphite
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")


def energy_strip(accent, alpha):
    """vertical strip, pattern twice (period 0.5): dim continuous track + bright travelling packet with a tail"""
    a = np.array(accent, float)
    img = np.zeros((FXH, FXW, 4), float)
    for y in range(FXH):
        v = (y + .5) / FXH
        x = (v - 0.25) % 0.5
        dpk = min(x, 0.5 - x)
        # the offset grows with time, so smaller v is reached later: the trail behind the packet is at larger v
        tail = math.exp(-(x / 0.10)) * 0.50 if x < 0.25 else 0.0
        g = 0.50 + 1.2 * math.exp(-(dpk / 0.05) ** 2) + tail
        img[y, :, :3] = np.clip(a * min(g, 1.0) + 255 * max(0.0, g - 0.85) * 0.9, 0, 255)
        img[y, :, 3] = alpha
    return Image.fromarray(img.astype(np.uint8), "RGBA")


# ====================================================================== output
def build(stick_geo: Path, player_entity, figura_rc, out: Path, fx_alpha=0):
    global MESH
    MESH = {}
    g = json.loads(Path(stick_geo).read_text())["minecraft:geometry"][0]
    bones = {b["name"]: b for b in g["bones"]}
    stats = build_suit(bones)
    geos = []
    for s in SLOTS:
        for layer, suffix in (("metal", ""), ("fx", "_fx")):
            bl = []
            for b in g["bones"]:
                nb = {k: v for k, v in b.items() if k in ("name", "parent", "pivot", "rotation")}
                m = MESH.get((s, layer, b["name"]))
                if m and m.polys:
                    nb["poly_mesh"] = m.json()
                bl.append(nb)
            geos.append({"description": {"identifier": f"geometry.fb_armor.stick.{s}{suffix}",
                                         "texture_width": TEX if layer == "metal" else FXW,
                                         "texture_height": TEX if layer == "metal" else FXH,
                                         "visible_bounds_width": 4, "visible_bounds_height": 5,
                                         "visible_bounds_offset": [0, 2, 0]}, "bones": bl})
    out = Path(out)
    (out / "models/entity").mkdir(parents=True, exist_ok=True)
    (out / "models/entity/fb_armor_stick.geo.json").write_text(
        json.dumps({"format_version": "1.21.0", "minecraft:geometry": geos}, separators=(",", ":")))
    td = out / "textures/figura/fb_armor"
    td.mkdir(parents=True, exist_ok=True)
    for name, body, dark, hl, acc in MATERIALS:
        metal_atlas(name, body, dark, hl, acc).save(td / f"{name}.png")
        energy_strip(acc, fx_alpha).save(td / f"{name}_fx.png")

    arr = [f"Texture.fbam_{m[0]}" for m in MATERIALS]
    arrfx = [f"Texture.fbamfx_{m[0]}" for m in MATERIALS]
    rcs = {}
    for s in SLOTS:
        rcs[f"controller.render.player.fb_armor_{s}"] = {
            "arrays": {"textures": {"Array.fbam": arr}},
            "geometry": f"Geometry.fbarmor_{s}", "materials": [{"*": "Material.fbam"}],
            "textures": [f"Array.fbam[v.{VAR[s]} - 1]"]}
        rcs[f"controller.render.player.fb_armor_{s}_fx"] = {
            "arrays": {"textures": {"Array.fbamfx": arrfx}},
            "geometry": f"Geometry.fbarmor_{s}_fx", "materials": [{"*": "Material.fbamfx"}],
            "textures": [f"Array.fbamfx[v.{VAR[s]} - 1]"],
            "uv_anim": {"offset": [0.0, "(v.fbaph ?? 0) * 0.5"], "scale": [1.0, 1.0]}}
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
            d["geometry"][f"fbarmor_{s}"] = f"geometry.fb_armor.stick.{s}"
            d["geometry"][f"fbarmor_{s}_fx"] = f"geometry.fb_armor.stick.{s}_fx"
        for m in MATERIALS:
            d["textures"][f"fbam_{m[0]}"] = f"textures/figura/fb_armor/{m[0]}"
            d["textures"][f"fbamfx_{m[0]}"] = f"textures/figura/fb_armor/{m[0]}_fx"
        d["materials"]["fbam"] = "entity_alphatest"
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
            pre.append(f"variable.{VAR[s]}t = v.{VAR[s]} > 0 ? math.min((v.{VAR[s]}t ?? 0) + q.delta_time, 10) : 0.0;")
        r0, r1 = f"{1 / PERIOD:.4f}", f"{1 / SPRINT_PERIOD:.4f}"
        pre.append(f"variable.fbarate = (v.fbarate ?? {r0}) + ((q.is_sprinting ? {r1} : {r0}) - (v.fbarate ?? {r0}))"
                   f" * math.min(1, q.delta_time * 3);")
        pre.append("variable.fbaph = math.mod((v.fbaph ?? 0) + q.delta_time * v.fbarate, 1.0);")
        rc = d["render_controllers"]
        rc[:] = [r for r in rc if not (isinstance(r, dict) and any(k.startswith("controller.render.player.fb_armor_") for k in r))]
        for s in SLOTS:
            on = f"!variable.is_first_person && (v.fbstickarmor ?? 0) && (v.{VAR[s]} ?? 0) > 0"
            rc.append({f"controller.render.player.fb_armor_{s}": on})
            rc.append({f"controller.render.player.fb_armor_{s}_fx": f"{on} && (v.{VAR[s]}t ?? 0) > {START[s]}"})
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
