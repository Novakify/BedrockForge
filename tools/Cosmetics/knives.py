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


def stadium(m, bone, xc, y0, y1, r, z, side, region, inner=False, ring_of=None):
    """a pocket outline with round ends (a stadium) lying on a face at height z: filled (inner) or as a band from
    radius ring_of out to r (the chamfer round a milled pocket)"""
    pts = []
    for a in np.linspace(0, math.pi, 9):
        pts.append((xc + r * math.cos(a), y1 + r * math.sin(a) - r))
    for a in np.linspace(math.pi, 2 * math.pi, 9):
        pts.append((xc + r * math.cos(a), y0 - r * math.sin(a) * -1 + r))
    n = np.array([0, 0, float(side)])
    if ring_of is None:
        c = P(xc, (y0 + y1) / 2, z)
        for k in range(len(pts)):
            a_, b_ = P(*pts[k], z), P(*pts[(k + 1) % len(pts)], z)
            tri = [c, a_, b_] if side > 0 else [c, b_, a_]
            m.tri(bone, tri, [n] * 3, [(region, 0.5, 0.5), (region, 0.1, 0.1), (region, 0.9, 0.1)])
        return
    k_ = ring_of / r
    for k in range(len(pts)):
        a_, b_ = pts[k], pts[(k + 1) % len(pts)]
        ai = (xc + (a_[0] - xc) * k_, (y0 + y1) / 2 + (a_[1] - (y0 + y1) / 2) * (1 - (r - ring_of) / max(y1 - y0, 1e-3) * 2))
        bi = (xc + (b_[0] - xc) * k_, (y0 + y1) / 2 + (b_[1] - (y0 + y1) / 2) * (1 - (r - ring_of) / max(y1 - y0, 1e-3) * 2))
        q = [P(*a_, z), P(*b_, z), P(*bi, z), P(*ai, z)]
        m.quad(bone, q if side > 0 else q[::-1], [n] * 4, [(region, 0, 0), (region, 1, 0), (region, 1, 1), (region, 0, 1)])


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


def steel_loft(m, sections, blade_from_y, regs=("face", "plate", "spine", "rim"), blade_from_index=None):
    """one continuous steel body from a list of sections (spine2d, belly2d, profile[(d, hz)]): tang / ricasso /
    blade are the SAME surface, so nothing is spliced; the Damascus/finish runs on with arc length. Faces before
    blade_from_y use the plain plate finish, after it the blade finish (grind, bevel, apex)."""
    face, plate, spine, rim = regs
    L = [0.0]
    for i in range(1, len(sections)):
        L.append(L[-1] + float(np.linalg.norm(sections[i][0] - sections[i - 1][0])))
    U = [l / L[-1] for l in L]
    i_bl = blade_from_index if blade_from_index is not None else next(i for i, (sp, _, _) in enumerate(sections) if sp[1] > blade_from_y)

    def ring_pts(sec):
        sp, be, prof = sec
        top = [(sp + (be - sp) * d, hz) for d, hz in prof]
        return [P(q[0], q[1], z) for q, z in top] + [P(q[0], q[1], -z) for q, z in reversed(top)]
    loops = [ring_pts(sec) for sec in sections]
    K = len(loops[0])
    nprof = len(sections[0][2])
    dvals = [d for d, _ in sections[0][2]]
    for i in range(len(loops) - 1):
        blade_zone = i >= i_bl
        for k in range(K):
            k2 = (k + 1) % K
            ps = [loops[i][k], loops[i + 1][k], loops[i + 1][k2], loops[i][k2]]
            n = face_normal(ps)
            c = np.mean(ps, 0)
            mid = np.mean([loops[i][0], loops[i][nprof - 1]], 0)
            if np.dot(n, c - mid) < 0:
                n = -n
            if k < nprof - 1 or nprof <= k < K - 1:
                j = k if k < nprof - 1 else (K - 2 - k)
                reg = face if blade_zone else plate
                v0, v1 = (dvals[j], dvals[j + 1]) if k < nprof - 1 else (dvals[j + 1], dvals[j])
                uv = [(reg, U[i], v0), (reg, U[i + 1], v0), (reg, U[i + 1], v1), (reg, U[i], v1)]
            else:
                rg = spine if k == K - 1 else rim
                uv = [(rg, U[i], 0), (rg, U[i + 1], 0), (rg, U[i + 1], 1), (rg, U[i], 1)]
            m.quad("bfk_blade" if blade_zone else "bfk_body", ps, [n] * 4, uv)
    # close the first section (the tang end) so it never shows a hole
    first = loops[0]
    c = np.mean(first, 0)
    for k in range(K):
        m.tri("bfk_body", [c, first[(k + 1) % K], first[k]], [np.array([0, -1.0, 0])] * 3, [(rim, 0.5, 0.5)] * 3)


# ------------------------------------------------------------------ Novak Karambit
def karambit_atlas():
    a = Atlas(512)
    a.add("face", 384, 64, lambda w, h: mt.makers_mark(mt.damascus(w, h, 5), 0.43, 0.62, 13))
    a.add("plate", 384, 64, lambda w, h: mt.damascus(w, h, 5, blade=False))     # same billet, no grinds
    a.add("spine", 384, 8, lambda w, h: mt.spine_steel(w, h, (98, 98, 106), 0.47, 0.37))
    a.add("scales", 256, 96, lambda w, h: mt.cocobolo(w, h, 23))
    a.add("liner", 16, 16, lambda w, h: mt.g10_liner(w, h))
    a.add("rim", 128, 8, lambda w, h: mt.spine_steel(w, h, (120, 120, 128), 0.0))
    a.add("ringface", 128, 32, lambda w, h: mt.damascus(w, h, 9, 6, blade=False))
    a.add("brass", 16, 16, lambda w, h: mt.brass_plain(w, h))
    return a


K_RING = (-0.45, -4.85)        # ring centre (model units, grip-relative)
K_RING_R = (1.0, 1.52)         # inner / outer radius: ~1" finger hole
K_HZ = 0.27                    # half-thickness of the steel plate (tang / ricasso / ring): ~0.2" stock, a touch heavier to read in game


def _smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def karambit():
    """Reference: a full-tang fixed karambit, built like the real ones - ONE steel plate (blade, ricasso, tang and
    finger ring cut from the same Damascus billet), cocobolo scales pinned to both sides over red G10 liners, so a
    rim of the same steel shows all round the handle and the ring is plainly part of the blade. The primary grind
    runs out at a plunge line into a flat ricasso with a small choil; a front brake guards the index finger; the
    back edge is ground near the tip; jimping on the thumb rise; brass pins; our hexagon N mark by the plunge."""
    m = Mesh()
    # ---------- the steel: one loft from the ring neck, up the tang, through the ricasso, round the claw to the tip
    hy_neck, hy_ric, hy_heel = -3.78, 2.2, 3.2
    R, th1 = 4.6, math.radians(100)
    heel = np.array([0.72, hy_heel])
    ctr = heel - np.array([R, 0.0])

    def handle_edges(y):
        """(spine x, belly x) of the steel plate along the handle"""
        s = (hy_ric - y) / (hy_ric - hy_neck)                     # 0 at the ricasso .. 1 at the ring neck
        cx = -0.45 + 0.22 * math.sin(math.pi * min(s, 1))
        spine = cx + 0.86 + 0.08 * math.sin(math.pi * min(s, 1))
        groove = sum(0.17 * max(0.0, math.cos(min(1.0, abs(s - g) / 0.11) * math.pi / 2)) ** 2 for g in (0.30, 0.52, 0.74))
        brake = 0.46 * math.exp(-((s - 0.05) / 0.05) ** 2)
        belly = cx - (0.92 - groove + 0.06 * math.sin(math.pi * min(s, 1)) + brake)
        flare = 0.34 * _smooth((s - 0.80) / 0.20) ** 1.5          # the neck flares into the ring (a fillet, not a cut)
        return spine + flare * 0.7, belly - flare

    sections = []        # (spine2d, belly2d, profile [(d, hz)], u-length)
    # tang + ricasso along y (spine/belly from handle_edges, morphing into the blade heel at the top)
    ys = list(np.linspace(hy_neck, hy_ric, 30)) + list(np.linspace(hy_ric, hy_heel, 9))[1:]
    sp_heel, be_heel = heel, heel + np.array([-2.2, 0.0])
    plate = [(0.0, 0.62), (0.07, 1.0), (0.36, 1.0), (0.86, 1.0), (0.93, 1.0), (1.0, 0.62)]
    for y in ys:
        sp, be = handle_edges(min(y, hy_ric))
        if y > hy_ric:                                           # ricasso: widen to the blade heel, choil on the belly
            t = _smooth((y - hy_ric) / (hy_heel - hy_ric))
            sp = sp + (sp_heel[0] - sp) * t
            be = be + (be_heel[0] - be) * t
            be += 0.28 * math.exp(-((y - (hy_ric + 0.30)) / 0.16) ** 2)     # the choil: a small notch before the edge
        prof = [(d, f * K_HZ) for d, f in plate]
        if y > hy_ric + 0.55:                                    # the plunge: the grind starts mid-ricasso
            g = _smooth((y - hy_ric - 0.55) / (hy_heel - hy_ric - 0.55))
            bl = [(0.0, 0.34), (0.07, 0.50), (0.36, 0.475), (0.86, 0.06), (0.93, 0.03), (1.0, 0.0)]
            prof = [(d, hp * (1 - g) + hb * 0.60 * g) for (d, hp), (_, hb) in zip(prof, bl)]
        sections.append((np.array([sp, y]), np.array([be, y]), prof))
    # the claw
    N = 26
    for i in range(1, N + 1):
        s = i / N
        th = th1 * s
        sp = ctr + R * np.array([math.cos(th), math.sin(th)])
        inward = -np.array([math.cos(th), math.sin(th)])
        w = 2.2 * (1 - s) ** 0.58 * (1 + 0.16 * math.sin(math.pi * s)) + 1e-3
        t = 0.60 * (1 - 0.72 * s ** 1.1)
        sw = max(0.0, (s - 0.62) / 0.38)                         # back edge ground near the tip
        prof = [(0.0, 0.34 * (1 - 0.85 * sw) * t), (0.07 + 0.12 * sw, 0.5 * t), (0.36, 0.475 * t), (0.86, 0.06 * t),
                (0.93, 0.03 * t), (1.0, 0.0)]
        sections.append((sp, sp + inward * w, prof))
    steel_loft(m, sections, hy_heel - 0.3)

    # ---------- the finger ring: part of the same plate (annulus, rounded edges, Damascus faces)
    rc = np.array(K_RING)
    r_in, r_out = K_RING_R
    rm, rw = (r_in + r_out) / 2, (r_out - r_in) / 2
    hz_r = K_HZ + 0.015                     # a hair proud of the neck: no z-fighting where the plate meets the ring
    sec = []                                # (radial offset, z) round the ring's cross-section
    for a_ in np.linspace(0, 2 * math.pi, 12, endpoint=False):
        c_, s_ = math.cos(a_ + math.pi / 12), math.sin(a_ + math.pi / 12)
        sec.append((rw * math.copysign(abs(c_) ** (2 / 5), c_), hz_r * math.copysign(abs(s_) ** (2 / 5), s_)))
    SEG = 40
    pts = [[P(rc[0] + (rm + dr) * math.cos(2 * math.pi * j / SEG), rc[1] + (rm + dr) * math.sin(2 * math.pi * j / SEG), z) for dr, z in sec] for j in range(SEG + 1)]
    for j in range(SEG):
        for k in range(len(sec)):
            k2 = (k + 1) % len(sec)
            ps = [pts[j][k], pts[j + 1][k], pts[j + 1][k2], pts[j][k2]]
            n = face_normal(ps)
            ang = 2 * math.pi * (j + 0.5) / SEG
            ctr3 = P(rc[0] + rm * math.cos(ang), rc[1] + rm * math.sin(ang), 0)
            if np.dot(n, np.mean(ps, 0) - ctr3) < 0:
                n = -n
            if abs(n[2]) > 0.6:
                vr = lambda dr: 0.5 + dr / (2 * rw)
                uv = [("ringface", j / SEG, vr(sec[k][0])), ("ringface", (j + 1) / SEG, vr(sec[k][0])),
                      ("ringface", (j + 1) / SEG, vr(sec[k2][0])), ("ringface", j / SEG, vr(sec[k2][0]))]
            else:
                uv = [("rim", j / SEG, 0.2), ("rim", (j + 1) / SEG, 0.2), ("rim", (j + 1) / SEG, 0.8), ("rim", j / SEG, 0.8)]
            m.quad("bfk_body", ps, [n] * 4, uv)

    # ---------- scales: sculpted cocobolo slabs over red liners, inset from the steel, tapering into it at both ends
    s0, s1 = 0.0, 0.86
    NS, NT = 48, 11
    for side in (1, -1):
        for layer in ("liner", "scales"):
            inset = 0.10 if layer == "liner" else 0.135
            z0 = K_HZ
            grid = []
            for i in range(NS + 1):
                s = s0 + (s1 - s0) * i / NS
                y = hy_ric - s * (hy_ric - hy_neck)
                sp, be = handle_edges(y)
                end = _smooth(i / 7) * _smooth((NS - i) / 7)          # the shaped ends: the slab thins into the steel
                e = min(i, NS - i) / 5.0                              # rounded ends in plan (an ellipse, not a cut)
                wf = math.sqrt(max(0.0, 1 - (1 - min(1.0, e)) ** 2)) if e < 1 else 1.0
                row = []
                for j in range(NT + 1):
                    t = j / NT
                    mid_x = (be + sp) / 2
                    half = (sp - be) / 2 - inset
                    x = mid_x + (2 * t - 1) * half * max(wf, 0.25)
                    if layer == "liner":
                        z = z0 + 0.04 * end
                    else:
                        dome = (1 - abs(2 * t - 1) ** 3.2) ** 0.5
                        z = z0 + 0.04 * end + (0.06 + 0.19 * dome) * end
                    row.append(P(x, y, side * z))
                grid.append(row)
            for i in range(NS):
                for j in range(NT):
                    ps = [grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]]
                    n = face_normal(ps)
                    if n[2] * side < 0:
                        n = -n
                    if layer == "liner":
                        uv = [("liner", 0.5, 0.5)] * 4
                    else:
                        vv = lambda jj: 0.25 + 0.3 * jj / NT
                        uv = [("scales", i / NS, vv(j)), ("scales", (i + 1) / NS, vv(j)), ("scales", (i + 1) / NS, vv(j + 1)), ("scales", i / NS, vv(j + 1))]
                    m.quad("bfk_body", ps, [n] * 4, uv)
            if layer == "liner":                                 # the liner's visible edge (a thin red line all round)
                for i in range(NS):
                    for j in (0, NT):
                        a_, b_ = grid[i][j], grid[i + 1][j]
                        a0, b0 = a_.copy(), b_.copy()
                        a0[2] = side * K_HZ
                        b0[2] = side * K_HZ
                        ps = [a0, b0, b_, a_]
                        n = np.array([1.0 if j == NT else -1.0, 0, 0])
                        m.quad("bfk_body", ps, [n] * 4, [("liner", 0.5, 0.5)] * 4)
        for sp_ in (0.24, 0.66):                                 # brass pins through the scales
            y = hy_ric - (s0 + (s1 - s0) * sp_) * (hy_ric - hy_neck)
            spx, bex = handle_edges(y)
            cx = (spx + bex) / 2 + 0.05
            pin(m, "bfk_body", P(cx, y, side * (K_HZ + 0.04 + 0.24)), 0.15, 0.02, side, "brass")
    return m, karambit_atlas()


RING_CENTRE = (K_RING[0], GRIP_Y + K_RING[1], 0.0)   # karambit spin pivot (the finger in the ring)
SPIN_PIVOT = {"karambit": RING_CENTRE, "balisong": (0.0, GRIP_Y, 0.0), "dagger": (0.0, GRIP_Y - 2.0, 0.0)}
# the blade's point (slash arcs and hit sparks come off it)
_kt = math.radians(100)
TIP = {"karambit": (0.72 - 4.6 + 4.6 * math.cos(_kt), GRIP_Y + 3.2 + 4.6 * math.sin(_kt), 0.0),
       "balisong": (0.4, GRIP_Y + 5.55 + 0.4 + 7.0, 0.0),
       "dagger": (0.0, GRIP_Y + 1.5 + 8.6, 0.0)}
EXTRA_BONES = {"balisong": [("bfb_safe", (0.0, GRIP_Y + 4.25, 0.33)), ("bfb_bite", (0.0, GRIP_Y + 4.25, -0.33))]}


def bones(kind="karambit"):
    """bone tree shared by every knife: wfx_item (bound to the hand, World FX hold), bfk_pose (inspect lift/turn
    about the grip), bfk_spin (draw / inspect spins: the karambit about its ring), then the parts; the balisong's
    handles are their own bones on their pivot pins for the flip"""
    out = [
        {"name": "wfx_item", "pivot": [0, 24, 0], "binding": "q.item_slot_to_bone_name(c.item_slot)"},
        {"name": "bfk_pose", "parent": "wfx_item", "pivot": [0, 24, 0]},
        {"name": "bfk_spin", "parent": "bfk_pose", "pivot": list(SPIN_PIVOT[kind])},
        {"name": "bfk_blade", "parent": "bfk_spin", "pivot": [0, 24, 0], "locators": {"tip": list(TIP[kind])}},
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
    a.add("face", 256, 64, lambda w, h: mt.makers_mark(mt.two_tone(w, h, 13), 0.16, 0.22, 13))
    a.add("plate", 256, 64, lambda w, h: mt.stonewash(w, h, 13))
    a.add("spine", 256, 8, lambda w, h: mt.spine_steel(w, h, (134, 136, 142), 0.0))
    a.add("rim", 64, 8, lambda w, h: mt.spine_steel(w, h, (134, 136, 142), 0.0))
    a.add("ti", 192, 64, lambda w, h: mt.black_ti(w, h))
    a.add("slot", 32, 128, lambda w, h: mt.slot_floor(w, h))
    a.add("chamfer", 16, 8, lambda w, h: mt.chamfer_ti(w, h))
    a.add("bronze", 32, 16, lambda w, h: mt.bronze_ano(w, h))
    a.add("steel", 16, 16, lambda w, h: mt.polished_steel(w, h))
    return a


def balisong():
    """Reference: a modern channel/chanwich balisong - 4" clip point with a swedge, 5" handles straddling the tang
    on two pivot pins, a kicker and choil before the edge, zen pins, the latch on the bite handle. Built like one:
    blade, ricasso and tang are one stonewashed steel body that runs down BETWEEN the handle tops (no block sitting
    on top), the handle ends are domed round the pivots, bronze hardware. Handles are their own bones for the flip."""
    m = Mesh()
    pivot_y, hand_top, plunge_y, blade_len = 4.25, 4.75, 5.55, 7.0
    clip = lambda s: 0.95 - 0.55 * max(0.0, (s - 0.62) / 0.38) ** 1.4
    edge = lambda s: -0.95 + 0.25 * s ** 1.5 + 1.10 * max(0.0, (s - 0.5) / 0.5) ** 2.0
    plate = [(0.0, 0.62), (0.07, 1.0), (0.36, 1.0), (0.86, 1.0), (0.93, 1.0), (1.0, 0.62)]
    HZ = 0.22
    sections = []
    for y in np.linspace(3.6, plunge_y + 0.4, 26):                # tang + ricasso (+ kicker, choil), morphing into the grind
        be = -0.95 - 0.20 * math.exp(-((y - 4.9) / 0.26) ** 2) + 0.16 * math.exp(-((y - 5.32) / 0.15) ** 2)
        prof = [(d, f * HZ) for d, f in plate]
        if y > plunge_y - 0.3:
            g = _smooth((y - plunge_y + 0.3) / 0.7)
            bl = [(0.0, 0.30), (0.07, 0.48), (0.36, 0.46), (0.86, 0.06), (0.93, 0.03), (1.0, 0.0)]
            prof = [(d, hp * (1 - g) + hb * 0.5 * g) for (d, hp), (_, hb) in zip(prof, bl)]
        sections.append((np.array([0.95, y]), np.array([be, y]), prof))
    N = 22
    for i in range(1, N + 1):
        s = i / N
        y = plunge_y + 0.4 + blade_len * s
        sw = max(0.0, (s - 0.55) / 0.45)                           # swedge: the spine bevelled along the clip
        t = 0.5 * (1 - 0.6 * s)
        prof = [(0.0, 0.30 * (1 - 0.8 * sw) * t), (0.07 + 0.15 * sw, 0.48 * t), (0.36, 0.46 * t), (0.86, 0.06 * t), (0.93, 0.03 * t), (1.0, 0.0)]
        sections.append((np.array([clip(s), y]), np.array([edge(s), y]), prof))
    steel_loft(m, sections, plunge_y)
    hy0 = -4.55
    for side, bone in ((1, "bfb_safe"), (-1, "bfb_bite")):
        cz = side * 0.33
        secs_h = []
        NH = 26
        for j in range(NH + 1):
            t_ = j / NH
            y = hy0 + (hand_top - hy0) * t_
            ax = 0.64 + 0.08 * t_ - 0.05 * math.sin(math.pi * t_) + 0.07 * math.exp(-(t_ / 0.12) ** 2)   # hourglass, butt flare
            e_top = min(1.0, (hand_top - y) / 0.75)                    # domed round the pivot
            e_bot = min(1.0, (y - hy0) / 0.35)
            k_ = math.sqrt(max(0.0, 1 - (1 - e_top) ** 2)) * math.sqrt(max(0.0, 1 - (1 - e_bot) ** 2))
            secs_h.append(rrect(0.0, y, max(ax * k_, 0.05), max(ax * k_, 0.05), 0.30 * (0.55 + 0.45 * k_), 16, 7, cz=cz))
        loft(m, bone, secs_h, "ti", flat=False, outward=lambda p, cz=cz: np.array([p[0], 0, p[2] - cz]))
        zf = cz + side * 0.302
        for xc in (-0.245, 0.245):                                  # two milled pockets: stadium shape, chamfered
            g0, g1 = hy0 + 1.15, hand_top - 1.6
            stadium(m, bone, xc, g0, g1, 0.15, zf + side * 0.012, side, "slot", inner=True)
            stadium(m, bone, xc, g0 - 0.06, g1 + 0.06, 0.21, zf + side * 0.008, side, "chamfer", ring_of=0.15)
        pin(m, bone, P(0.0, pivot_y, zf - side * 0.01), 0.26, 0.05, side, "bronze", 12)     # pivot
        pin(m, bone, P(0.0, pivot_y - 0.62, zf), 0.11, 0.035, side, "bronze", 8)            # zen pin
        pin(m, bone, P(0.0, hy0 + 0.55, zf), 0.13, 0.035, side, "bronze", 8)                # butt screw
    # the latch: a flat bronze bar pinned to the bite handle's butt, swung across under the safe handle, T-shaped end
    latch = [rrect(0.0, y, 0.22, 0.22, 0.62, 10, 4) for y in (hy0 - 0.16, hy0 + 0.02)]
    loft(m, "bfb_bite", latch, "bronze", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    cap(m, "bfb_bite", latch[0], -1, "bronze")
    tee = [rrect(0.0, y, 0.42, 0.42, 0.16, 10, 4, cz=0.55) for y in (hy0 - 0.17, hy0 + 0.03)]
    loft(m, "bfb_bite", tee, "bronze", flat=False, outward=lambda p: np.array([p[0], 0, p[2] - 0.55]))
    for side in (1, -1):                                        # tang pins: they stop the handles in the open position
        pin(m, "bfk_blade", P(0.0, hand_top + 0.22, side * 0.22), 0.09, 0.07, side, "steel", 8)
    return m, balisong_atlas()


# ------------------------------------------------------------------ Novak Dagger
def dagger_atlas():
    a = Atlas(512)
    a.add("face", 256, 64, lambda w, h: mt.makers_mark(mt.mirror(w, h, 17, True, symmetric=True), 0.10, 0.30, 11))
    a.add("plate", 64, 32, lambda w, h: mt.mirror(w, h, 19, True, symmetric=True))
    a.add("spine", 32, 8, lambda w, h: mt.spine_steel(w, h, (180, 182, 190), 0.0))
    a.add("rim", 32, 8, lambda w, h: mt.spine_steel(w, h, (180, 182, 190), 0.0))
    a.add("steel", 32, 16, lambda w, h: mt.polished_steel(w, h))
    a.add("shadow", 4, 4, lambda w, h: mt.black_parts(w, h))
    a.add("blued", 96, 32, lambda w, h: mt.blued_steel(w, h))
    a.add("grip", 320, 64, lambda w, h: mt.worn_black_brass(w, h, 20))
    a.add("brass", 64, 24, lambda w, h: mt.brass_plain(w, h))
    a.add("knurl", 64, 24, lambda w, h: mt.brass_rings(w, h, 10))
    return a


def dagger():
    """Reference: the Fairbairn-Sykes fighting knife, 3rd pattern, built like one: the blade's tang runs through
    the guard and the grip and the pommel nut screws onto its end. One mirror-polished steel body (a short flat
    ricasso where it enters the guard, then the lens-section spear point with its centre ridge); a hot-blued guard
    with the blade's slot shadow; a steel ferrule, the knurled brass vase grip, a brass nut with the tang's end."""
    m = Mesh()
    HZ = 0.25
    sections = []
    for y in np.linspace(0.3, 1.5, 7):                            # tang end (in the guard) + ricasso -> grind
        w = 1.0 + 0.4 * _smooth((y - 0.9) / 0.6)
        prof = [(0.0, 0.0), (0.07, 0.8 * HZ), (0.5, HZ), (0.93, 0.8 * HZ), (1.0, 0.0)]
        if y > 1.15:
            g = _smooth((y - 1.15) / 0.35)
            bl = [(0.0, 0.0), (0.07, 0.03), (0.5, 0.31), (0.93, 0.03), (1.0, 0.0)]
            prof = [(d, hp * (1 - g) + hb * g) for (d, hp), (_, hb) in zip(prof, bl)]
        sections.append((np.array([w / 2, y]), np.array([-w / 2, y]), prof))
    N = 20
    for i in range(1, N + 1):
        s = i / N
        y = 1.5 + 8.6 * s
        w = 1.4 * (1 - s ** 1.9) ** 0.9 + 1e-3
        t = 0.62 * (1 - 0.55 * s)
        prof = [(0.0, 0.0), (0.07, 0.05 * t), (0.5, 0.5 * t), (0.93, 0.05 * t), (1.0, 0.0)]
        sections.append((np.array([w / 2, y]), np.array([-w / 2, y]), prof))
    steel_loft(m, sections, 1.1)
    # guard: the 1st-pattern S-guard - an oval bar that sweeps up on one side and down on the other, rounded ends,
    # a boss round the blade with the slot's shadow on top
    GX = 1.55
    gsecs = []
    for i in range(25):
        x = -GX + 2 * GX * i / 24
        yc = 0.62 + 0.22 * math.sin(math.pi * x / GX) * min(1.0, abs(x) / 0.6)
        endk = math.sqrt(max(0.0, 1 - max(0.0, (abs(x) - (GX - 0.22)) / 0.22) ** 2))
        hy, hz = 0.21 * (0.45 + 0.55 * endk), 0.34 * (0.45 + 0.55 * endk)
        sec = []
        for k in range(12):
            a_ = 2 * math.pi * k / 12 + math.pi / 12
            c_, s_ = math.cos(a_), math.sin(a_)
            sec.append(P(x, yc + hy * math.copysign(abs(c_) ** (2 / 3), c_), hz * math.copysign(abs(s_) ** (2 / 3), s_)))
        gsecs.append(sec)
    for i in range(24):                                             # loft along x (sections are y-z rings)
        for k in range(12):
            k2 = (k + 1) % 12
            ps = [gsecs[i][k], gsecs[i + 1][k], gsecs[i + 1][k2], gsecs[i][k2]]
            n = face_normal(ps)
            ctr_ = (gsecs[i][0] + gsecs[i][6]) / 2
            if np.dot(n, np.mean(ps, 0) - ctr_) < 0:
                n = -n
            m.quad("bfk_body", ps, [n] * 4, [("blued", i / 24, k / 12), ("blued", (i + 1) / 24, k / 12), ("blued", (i + 1) / 24, (k + 1) / 12), ("blued", i / 24, (k + 1) / 12)])
    for i_, sgn in ((0, -1), (24, 1)):
        c_ = np.mean(gsecs[i_], 0)
        for k in range(12):
            tri = [c_, gsecs[i_][k], gsecs[i_][(k + 1) % 12]]
            if sgn < 0:
                tri = [c_, gsecs[i_][(k + 1) % 12], gsecs[i_][k]]
            m.tri("bfk_body", tri, [np.array([float(sgn), 0, 0])] * 3, [("blued", 0.5, 0.5)] * 3)
    boss = [rrect(0.0, y, 0.6, 0.6, 0.31, 14, 3) for y in (0.30, 0.86, 1.0)]
    loft(m, "bfk_body", boss, "blued", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    sh = [P(-0.54, 1.005, -0.17), P(0.54, 1.005, -0.17), P(0.54, 1.005, 0.17), P(-0.54, 1.005, 0.17)]
    m.quad("bfk_body", sh, [np.array([0, 1.0, 0])] * 4, [("shadow", 0.5, 0.5)] * 4)
    # ferrule (steel collar) between guard and grip
    fer = [[P(r * math.cos(a), y, r * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 16, endpoint=False)] for y, r in ((0.05, 0.46), (-0.18, 0.44), (-0.26, 0.40))]
    loft(m, "bfk_body", fer, "steel", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    rings = []
    G = 20 * 5
    for i in range(G + 1):
        s = i / G
        y = -0.26 - 5.75 * s
        ridge = 0.045 * max(-1.0, min(1.0, 2.4 * math.cos(s * 20 * 2 * math.pi)))      # flat crests, V grooves
        r = 0.40 + 0.20 * math.sin(math.pi * min(1.0, s / 0.85)) ** 0.8 + ridge
        rings.append([P(r * math.cos(a), y, r * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 16, endpoint=False)])
    loft(m, "bfk_body", rings, "grip", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    # nut: a hexagonal brass nut with a chamfer, the tang's threaded end showing at its bottom
    prof = [(-6.0, 0.34), (-6.06, 0.44), (-6.30, 0.47), (-6.42, 0.44), (-6.52, 0.36), (-6.6, 0.24), (-6.65, 0.10)]
    nut = [[P(r * math.cos(a), y, r * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 20, endpoint=False)] for y, r in prof]
    loft(m, "bfk_body", nut, "knurl", flat=False, outward=lambda p: np.array([p[0], 0, p[2]]))
    cap(m, "bfk_body", nut[-1], -1, "steel")
    return m, dagger_atlas()
