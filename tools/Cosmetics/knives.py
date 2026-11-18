"""Bedrock Forge cosmetics: original 3D knives (by Novak) - each one a properly designed real-world knife.

Every design follows a real reference closely (shape, proportions, construction, materials) with our own details
on top - never a copy, never a shared "client" colour scheme. Each knife has its own atlas and finishes
(materials.py): Damascus, stonewash, mirror polish, cocobolo, titanium, bronze, brass, blued steel.

Frame: Mojang's trident frame (grip centre on (0, 24, 0), item along +y, flat facing +-z), so the World FX hold
animations and the vanilla swing apply as they do for every World FX weapon.
"""
from __future__ import annotations

import math

import numpy as np

import materials as mt
from mesh import Atlas, Mesh, face_normal, loft, nrm, torus

GRIP_Y = 24.0


# ------------------------------------------------------------------ shared geometry helpers
def P(x, y, z=0.0):
    return np.array([x, y + GRIP_Y, z])


def blade_from_sections(m, bone, secs, profile_d, single, N):
    """secs[i] = list of (point2d, half_thickness) at each profile d (single edge: spine->edge). Builds both faces
    with blade-face UVs (u along, v = d) and, for a single edge, the spine cap."""
    for side in (1, -1):
        for j in range(len(profile_d) - 1):
            v0, v1 = profile_d[j], profile_d[j + 1]
            for i in range(N):
                a0, a1 = secs[i][j], secs[i][j + 1]
                b0, b1 = secs[i + 1][j], secs[i + 1][j + 1]
                ps = [P(a0[0][0], a0[0][1], side * a0[1]), P(b0[0][0], b0[0][1], side * b0[1]),
                      P(b1[0][0], b1[0][1], side * b1[1]), P(a1[0][0], a1[0][1], side * a1[1])]
                n = face_normal(ps)
                if n[2] * side < 0:
                    n = -n
                m.quad(bone, ps, [n] * 4, [("face", i / N, v0), ("face", (i + 1) / N, v0), ("face", (i + 1) / N, v1), ("face", i / N, v1)])
    if single:
        for i in range(N):
            a, b = secs[i][0], secs[i + 1][0]
            ps = [P(a[0][0], a[0][1], a[1]), P(b[0][0], b[0][1], b[1]), P(b[0][0], b[0][1], -b[1]), P(a[0][0], a[0][1], -a[1])]
            out = np.append(secs[i][0][0] - secs[i][1][0], 0.0)
            n = face_normal(ps)
            if np.dot(n, out) < 0:
                n = -n
            m.quad(bone, ps, [n] * 4, [("spine", i / N, 0), ("spine", (i + 1) / N, 0), ("spine", (i + 1) / N, 1), ("spine", i / N, 1)])
    # close the heel
    hs = secs[0]
    ring = [P(q[0], q[1], hz) for q, hz in hs] + [P(q[0], q[1], -hz) for q, hz in reversed(hs)]
    c = np.mean(ring, 0)
    for k in range(len(ring)):
        m.tri(bone, [c, ring[k], ring[(k + 1) % len(ring)]], [np.array([0, -1.0, 0])] * 3, [("spine", 0.5, 0.5)] * 3)


def pin(m, bone, centre, r, depth, axis_z_sign, region, n=10):
    """a flush pin / rivet head on a face: a short cylinder along z with a domed cap"""
    c = np.asarray(centre, float)
    ring0 = [c + [r * math.cos(a), r * math.sin(a), 0] for a in np.linspace(0, 2 * math.pi, n, endpoint=False)]
    ring1 = [p + [0, 0, axis_z_sign * depth] for p in ring0]
    loft(m, bone, [ring0, ring1], region, flat=False, outward=lambda p: np.array([p[0] - c[0], p[1] - c[1], 0]))
    top = c + [0, 0, axis_z_sign * depth * 1.6]
    for k in range(n):
        a, b = ring1[k], ring1[(k + 1) % n]
        m.tri(bone, [top, a, b] if axis_z_sign > 0 else [top, b, a], [nrm(top - c)] * 3, [(region, 0.5, 0.0), (region, 0.4, 0.5), (region, 0.6, 0.5)])


def rrect(cx, y, a_pos, a_neg, b, n=12, e=3.0, cz=0.0):
    out = []
    for k in range(n):
        ang = 2 * math.pi * k / n
        cs, sn = math.cos(ang), math.sin(ang)
        a = a_pos if cs >= 0 else a_neg
        out.append(P(cx + a * math.copysign(abs(cs) ** (2 / e), cs), y, cz + b * math.copysign(abs(sn) ** (2 / e), sn)))
    return out


def cap(m, bone, sec, y_sign, region):
    c = np.mean(sec, 0)
    for k in range(len(sec)):
        a, b = sec[k], sec[(k + 1) % len(sec)]
        m.tri(bone, [c, b, a] if y_sign < 0 else [c, a, b], [np.array([0, float(y_sign), 0])] * 3, [(region, 0.5, 0.5)] * 3)


# ------------------------------------------------------------------ Novak Karambit
def karambit_atlas():
    a = Atlas(512)
    a.add("face", 256, 64, lambda w, h: mt.makers_mark(mt.damascus(w, h, 5), 0.10, 0.62, 13))
    a.add("spine", 256, 8, lambda w, h: mt.spine_steel(w, h, (92, 92, 100), 0.22))
    a.add("scales", 192, 96, lambda w, h: mt.cocobolo(w, h, 23))
    a.add("ring", 128, 24, lambda w, h: mt.polished_steel(w, h))
    a.add("brass", 16, 16, lambda w, h: mt.brass_plain(w, h))
    return a


def karambit():
    """Reference: a full-tang fixed-blade karambit (Indonesian claw, modern hawkbill): blade ~ handle (~3.5"), a
    curved handle that continues the claw, finger grooves on the edge side, an index-finger ring (~1" inner) at the
    butt, jimping on the spine. Ours: an etched Damascus blade with a mirror-bright sharpened bevel, cocobolo scales
    over a red G10 liner with the steel tang showing at the seams, two brass pins, a polished steel ring."""
    m = Mesh()
    R, th1 = 4.6, math.radians(100)
    heel = np.array([0.72, 3.0])
    ctr = heel - np.array([R, 0.0])
    N = 24
    prof = [(0.0, 0.34), (0.08, 0.5), (0.36, 0.47), (0.86, 0.06), (1.0, 0.0)]
    secs = []
    for i in range(N + 1):
        s = i / N
        th = th1 * s
        sp = ctr + R * np.array([math.cos(th), math.sin(th)])
        inward = -np.array([math.cos(th), math.sin(th)])
        w = 2.2 * (1 - s) ** 0.58 * (1 + 0.16 * math.sin(math.pi * s)) + 1e-3
        t = 0.95 * (1 - 0.72 * s ** 1.1)
        sw = max(0.0, (s - 0.62) / 0.38)                       # back edge: the spine is ground to an edge near the tip
        pr = [(0.0, 0.34 * (1 - 0.85 * sw)), (0.08 + 0.12 * sw, 0.5), (0.36, 0.47), (0.86, 0.06), (1.0, 0.0)]
        secs.append([(sp + inward * d * w, hz * t) for d, hz in pr])
    blade_from_sections(m, "bfk_blade", secs, [d for d, _ in prof], True, N)

    hy0, hy1 = -3.75, 3.0
    rings = []
    HN = 34
    for i in range(HN + 1):
        s = i / HN
        y = hy1 + (hy0 - hy1) * s
        cx = -0.45 + 0.22 * math.sin(math.pi * s)
        a_pos = 0.86 + 0.08 * math.sin(math.pi * s)
        groove = sum(0.17 * max(0.0, math.cos(min(1.0, abs(s - g) / 0.12) * math.pi / 2)) ** 2 for g in (0.27, 0.5, 0.73))
        a_neg = 0.92 - groove + 0.06 * math.sin(math.pi * s) + 0.42 * math.exp(-((s - 0.035) / 0.045) ** 2)   # front brake
        b = 0.56 + 0.04 * math.sin(math.pi * s)
        rings.append(rrect(cx, y, a_pos, a_neg, b, 16, 3.2))
    loft(m, "bfk_body", rings, "scales", flat=False, outward=lambda p: np.array([p[0] + 0.45, 0, p[2]]))
    cap(m, "bfk_body", rings[0], 1, "scales")
    cap(m, "bfk_body", rings[-1], -1, "ring")
    for yy in (1.6, -1.9):                               # brass pins through both scales
        s = (hy1 - yy) / (hy1 - hy0)
        cx = -0.45 + 0.22 * math.sin(math.pi * s)
        bz = 0.56 + 0.04 * math.sin(math.pi * s)
        for side in (1, -1):
            pin(m, "bfk_body", P(cx + 0.05, yy, side * (bz - 0.02)), 0.17, 0.05, side, "brass")
    rc = P(-0.45, hy0 - 1.44, 0)
    torus(m, "bfk_body", rc, 1.48, 0.36, [1, 0, 0], [0, 1, 0], "ring", seg=26, sides=9)
    return m, karambit_atlas()


RING_CENTRE = (-0.45, GRIP_Y - 3.75 - 1.44, 0.0)   # karambit spin pivot (the finger in the ring)
SPIN_PIVOT = {"karambit": RING_CENTRE, "balisong": (0.0, GRIP_Y, 0.0), "dagger": (0.0, GRIP_Y - 2.0, 0.0)}
EXTRA_BONES = {"balisong": [("bfb_safe", (0.0, GRIP_Y + 3.85, 0.33)), ("bfb_bite", (0.0, GRIP_Y + 3.85, -0.33))]}


def bones(kind="karambit"):
    """bone tree shared by every knife: wfx_item (bound to the hand, World FX hold), bfk_pose (inspect lift/turn
    about the grip), bfk_spin (draw / inspect spins: the karambit about its ring), then the parts; the balisong's
    handles are their own bones on their pivot pins for the flip"""
    out = [
        {"name": "wfx_item", "pivot": [0, 24, 0], "binding": "q.item_slot_to_bone_name(c.item_slot)"},
        {"name": "bfk_pose", "parent": "wfx_item", "pivot": [0, 24, 0]},
        {"name": "bfk_spin", "parent": "bfk_pose", "pivot": list(SPIN_PIVOT[kind])},
        {"name": "bfk_blade", "parent": "bfk_spin", "pivot": [0, 24, 0]},
        {"name": "bfk_body", "parent": "bfk_spin", "pivot": [0, 24, 0]},
    ]
    for n, pv in EXTRA_BONES.get(kind, []):
        out.append({"name": n, "parent": "bfk_spin", "pivot": list(pv)})
    return out


# ------------------------------------------------------------------ straight blades
def straight_sections(N, y0, length, spine_fn, edge_fn, thick_fn, prof):
    secs = []
    for i in range(N + 1):
        s = i / N
        y = y0 + length * s
        sx, ex = spine_fn(s), edge_fn(s)
        w = max(sx - ex, 1e-3)
        t = thick_fn(s)
        secs.append([(np.array([sx - d * w, y]), hz * t) for d, hz in prof])
    return secs


def balisong_atlas():
    a = Atlas(512)
    a.add("face", 256, 64, lambda w, h: mt.makers_mark(mt.stonewash(w, h, 11), 0.12, 0.62, 13))
    a.add("spine", 256, 8, lambda w, h: mt.spine_steel(w, h, (126, 128, 134), 0.0))
    a.add("ti", 192, 64, lambda w, h: mt.titanium(w, h, 31))
    a.add("slot", 32, 128, lambda w, h: mt.slot_floor(w, h))
    a.add("bronze", 32, 16, lambda w, h: mt.bronze_ano(w, h))
    return a


def balisong():
    """Reference: a modern channel-construction balisong - 4" clip point with a swedge, 5" titanium channel
    handles, pivot + zen pins, a kicker and choil before the edge, the latch on the bite handle. Ours: a stonewashed
    blade, bead-blasted titanium handles with diagonal milled grip grooves on the faces, bronze-anodised pivots,
    zen pins and latch. Handles are their own bones (bfb_safe / bfb_bite) for the flip."""
    m = Mesh()
    tang_y0, tang_y1 = 4.35, 5.25
    clip = lambda s: 0.95 - 0.55 * max(0.0, (s - 0.62) / 0.38) ** 1.4
    edge = lambda s: -0.95 + 0.25 * s ** 1.5 + 1.10 * max(0.0, (s - 0.5) / 0.5) ** 2.0
    prof = [(0.0, 0.30), (0.07, 0.5), (0.36, 0.48), (0.86, 0.07), (1.0, 0.0)]
    N = 22
    secs = straight_sections(N, tang_y1, 7.2, clip, edge, lambda s: 0.78 * (1 - 0.6 * s), prof)
    blade_from_sections(m, "bfk_blade", secs, [d for d, _ in prof], True, N)
    tang = [rrect(0.0, y, 0.97, 0.97, 0.36, 12, 8) for y in (tang_y0, tang_y1)]
    loft(m, "bfk_blade", tang, "spine", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    kick = [rrect(-0.78, y, 0.38, 0.38, 0.32, 10, 4) for y in (tang_y0 + 0.08, tang_y0 + 0.55)]
    loft(m, "bfk_blade", kick, "spine", flat=False, outward=lambda p: np.array([p[0] + 0.78, 0, p[2]]))
    hy0, hy1 = -4.55, 4.3
    for side, bone in ((1, "bfb_safe"), (-1, "bfb_bite")):
        cz = side * 0.33
        secs_h = []
        for j in range(13):
            t_ = j / 12
            y = hy0 + (hy1 - hy0) * t_
            ax = 0.66 + 0.10 * t_ + 0.05 * math.sin(math.pi * t_ * 2.0) ** 2
            secs_h.append(rrect(0.0, y, ax, ax, 0.30, 16, 7, cz=cz))
        loft(m, bone, secs_h, "ti", flat=False, outward=lambda p, cz=cz: np.array([p[0], 0, p[2] - cz]))
        cap(m, bone, secs_h[0], -1, "ti")
        cap(m, bone, secs_h[-1], 1, "ti")
        zf = cz + side * 0.302
        # two milled slots per face, the carbon-fibre spacer showing through: drawn just proud of the face, the
        # slot's depth painted in (lit top chamfer, shadowed lower wall) - a real cut would need a hole in the loft
        for x0, x1 in ((-0.42, -0.07), (0.07, 0.42)):
            g0, g1 = hy0 + 1.1, hy1 - 1.5
            zz = zf + side * 0.012
            fl = [P(x0, g0, zz), P(x1, g0, zz), P(x1, g1, zz), P(x0, g1, zz)]
            m.quad(bone, fl if side > 0 else fl[::-1], [np.array([0, 0, float(side)])] * 4,
                   [("slot", 0, 1), ("slot", 1, 1), ("slot", 1, 0), ("slot", 0, 0)] if side > 0 else
                   [("slot", 0, 0), ("slot", 1, 0), ("slot", 1, 1), ("slot", 0, 1)])
        pin(m, "bfk_body", P(0.0, hy1 - 0.45, zf), 0.26, 0.05, side, "bronze", 12)          # pivot
        pin(m, "bfk_body", P(0.0, hy1 - 1.05, zf), 0.11, 0.035, side, "bronze", 8)          # zen pin
        pin(m, "bfk_body", P(0.0, hy0 + 0.55, zf), 0.13, 0.035, side, "bronze", 8)          # butt screw
    latch = [rrect(0.0, y, 0.6, 0.6, 0.5, 12, 5) for y in (hy0 - 0.34, hy0)]
    loft(m, "bfk_body", latch, "bronze", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    cap(m, "bfk_body", latch[0], -1, "bronze")
    return m, balisong_atlas()


# ------------------------------------------------------------------ Novak Dagger
def dagger_atlas():
    a = Atlas(512)
    a.add("face", 256, 64, lambda w, h: mt.makers_mark(mt.mirror(w, h, 17, True), 0.07, 0.42, 11))
    a.add("spine", 32, 8, lambda w, h: mt.spine_steel(w, h, (180, 182, 190), 0.0))
    a.add("blued", 96, 32, lambda w, h: mt.blued_steel(w, h))
    a.add("grip", 240, 64, lambda w, h: mt.brass_rings(w, h, 15))
    a.add("brass", 64, 24, lambda w, h: mt.brass_plain(w, h))
    return a


def dagger():
    """Reference: the Fairbairn-Sykes fighting knife, 3rd pattern - 7" slender double-edged spear point with a
    ground centre ridge, a short flat oval crossguard, a vase-shaped ring grip and a pommel nut. Ours: a mirror-
    polished blade (the original's bright finish), a hot-blued guard, a knurled brass ring grip with darker patina
    in the grooves, a hexagonal brass pommel nut."""
    m = Mesh()
    N = 18
    for side in (1, -1):        # each half of the lens: ridge (x=0) -> edge (x=+-w/2), both faces
        secs = []
        for i in range(N + 1):
            s = i / N
            y = 0.95 + 9.0 * s
            w = 1.55 * (1 - s ** 1.9) ** 0.9 + 1e-3
            t = 0.62 * (1 - 0.55 * s)
            prof = [(0.0, 0.5), (0.86, 0.09), (1.0, 0.0)]
            secs.append([(np.array([side * d * w / 2, y]), hz * t) for d, hz in prof])
        blade_from_sections(m, "bfk_blade", secs, [0.0, 0.86, 1.0], False, N)
    guard = [rrect(0.0, y, ax, ax, az, 18, 2.4) for y, ax, az in ((0.05, 1.42, 0.38), (0.12, 1.5, 0.42), (0.88, 1.5, 0.42), (0.95, 1.42, 0.38))]
    loft(m, "bfk_body", guard, "blued", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    cap(m, "bfk_body", guard[0], -1, "blued")
    cap(m, "bfk_body", guard[-1], 1, "blued")
    rings = []
    G = 30
    for i in range(G + 1):
        s = i / G
        y = 0.05 - 6.1 * s
        r = 0.40 + 0.20 * math.sin(math.pi * min(1.0, s / 0.85)) ** 0.8 + 0.035 * math.cos(s * G * math.pi)
        rings.append([P(r * math.cos(a), y, r * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 16, endpoint=False)])
    loft(m, "bfk_body", rings, "grip", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    nut = [[P(0.42 * math.cos(a), y, 0.42 * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 6, endpoint=False)] for y in (-6.05, -6.5)]
    loft(m, "bfk_body", nut, "brass", flat=True, outward=lambda p: np.array([p[0], 0, p[2]]))
    cap(m, "bfk_body", nut[-1], -1, "brass")
    return m, dagger_atlas()
