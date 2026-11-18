"""Bedrock Forge cosmetics: original 3D knives (by Novak) - first design: the Novak Karambit.

Design (ours, not from any client): a hooked claw blade in dark satin gunmetal with a mirror-polished primary grind
and a thin neon grind line (the client UI's accent stroke), a smoked-violet crystal spine with jimping, a frosted
pearl handle (the glass UI's panels) with three finger grooves and a dark liner seam, a gunmetal finger ring whose
inner rim glows. Low-poly, faceted where steel is ground flat, smooth where it is turned or moulded.

Frame: Mojang's trident frame (grip centre on (0, 24, 0), item along +y, flat facing +-z), so the World FX hold
animations and the vanilla swing apply as they do for every World FX weapon.
"""
from __future__ import annotations

import math

import numpy as np

from mesh import Atlas, Mesh, face_normal, loft, nrm, torus

GRIP_Y = 24.0
ACCENT = (200, 118, 255)       # Novak neon purple (Client UI accent 0), a hair brighter for the emissive strip
ID_METAL = 255 - 4 * 12        # World FX actor material id in alpha (see tools/WorldFx/atmos/iconmodel.py)


def _noise1(n, seed, scale=1.0):
    r = np.random.default_rng(seed)
    k = max(2, int(n / 3))
    pts = r.random(k + 1)
    x = np.linspace(0, k, n)
    i = np.minimum(x.astype(int), k - 1)
    f = x - i
    f = f * f * (3 - 2 * f)
    return (pts[i] * (1 - f) + pts[i + 1] * f - 0.5) * scale


def _rgba(rgb, a=255):
    out = np.zeros(rgb.shape[:2] + (4,), np.uint8)
    out[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    out[..., 3] = a
    return out


# ------------------------------------------------------------------ painters (u = along x, v = across y)
def paint_flat(w, h):
    """blade flat: satin gunmetal, a faint violet cast, brushed along the blade, darker towards the spine"""
    v = np.linspace(0, 1, h)[:, None]
    base = np.array([44, 41, 56.0]) + np.array([10, 9, 14.0]) * v[..., None]
    streak = np.stack([_noise1(w, 11 + j, 10) for j in range(h)])     # one streak line per texel row
    rgb = base + streak[..., None] * np.array([1, 1, 1.15])
    return _rgba(rgb, ID_METAL)


def paint_grind(w, h):
    """primary grind, mirror-polished: it reflects a horizon - dark just below the grind line, a bright cool band
    through the middle, a darker floor reflection, then the light catches the bevel again near the edge. Fine
    brushing along the blade keeps it steel, not chrome plastic."""
    v = np.linspace(0, 1, h)
    k = np.interp(v, [0.0, 0.12, 0.38, 0.52, 0.72, 1.0], [0.18, 0.30, 0.95, 0.80, 0.42, 0.70])
    lo, hi = np.array([40, 38, 54.0]), np.array([226, 226, 240.0])
    rgb = lo + (hi - lo) * k[:, None]
    rgb = np.broadcast_to(rgb[:, None, :], (h, w, 3)).copy()
    rgb += np.stack([_noise1(w, 40 + j, 14) for j in range(h)])[..., None]
    rgb[0] = rgb[0] * 0.6                                              # the grind line: a crisp dark step
    return _rgba(rgb, ID_METAL)


def paint_edge(w, h):
    rgb = np.zeros((h, w, 3)) + np.array([238, 238, 247.0])
    rgb[0] -= 26
    return _rgba(rgb, ID_METAL)


def paint_spine(w, h):
    """smoked violet crystal spine; jimping near the heel (u < 0.2): dark notches every other texel"""
    v = np.linspace(-1, 1, h)[:, None]
    rgb = np.array([62, 42, 92.0]) + np.array([40, 30, 52.0]) * (1 - np.abs(v))[..., None] ** 2
    rgb = np.broadcast_to(rgb, (h, w, 3)).copy()
    u = np.arange(w)
    jimp = (u < int(w * 0.2)) & (u % 2 == 1) & (u > 0)
    rgb[:, jimp] *= 0.45
    return _rgba(rgb, ID_METAL)


def paint_handle(w, h):
    """smoked-glass scales (the client's dark glass panels): deep violet-black faces with a frosted white chamfer
    where each face turns into the side (v ~ 0.12 / 0.38 / 0.62 / 0.88), a faint layered grain like micarta under
    glass, and a dark liner seam where the two scales meet (v = 0 and 0.5)"""
    v = (np.arange(h) + 0.5) / h
    face = np.abs(np.sin(2 * math.pi * v))                             # 1 on the flat faces (+-z), 0 at the seams
    base = np.array([30, 24, 44.0]) + np.array([22, 16, 34.0]) * face[:, None]
    rgb = np.broadcast_to(base[:, None, :], (h, w, 3)).copy()
    u = np.arange(w)
    rgb += (np.sin(u * 1.3 + np.sin(u * 0.37) * 2)[None, :, None] * 4)  # layered grain along the handle
    r = np.random.default_rng(7)
    rgb += (r.random((h, w)) - 0.5)[..., None] * 5
    for c in (0.125, 0.375, 0.625, 0.875):                              # frosted chamfers
        g = np.exp(-((v - c) / 0.035) ** 2)
        rgb = rgb + (np.array([214, 208, 236.0]) - rgb) * (0.75 * g)[:, None, None]
    seam = (np.minimum(np.abs(v - 0.0), np.abs(v - 1.0)) < 0.04) | (np.abs(v - 0.5) < 0.04)
    rgb[seam] = np.array([14, 12, 20.0])
    return _rgba(rgb, 255)


def paint_bolster(w, h):
    v = np.linspace(0, 1, h)[:, None]
    rgb = np.array([34, 32, 42.0]) + np.array([30, 28, 40.0]) * (np.abs(np.sin(v * math.pi * 4)) ** 6)[..., None]
    rgb = np.broadcast_to(rgb, (h, w, 3)).copy()
    return _rgba(rgb, ID_METAL)


def paint_ring(w, h):
    """frosted pearl (the UI's white glass): bright on the outside of the tube, cooler and darker inside"""
    v = (np.arange(h) + 0.5) / h
    lit = 0.5 + 0.5 * np.cos(2 * math.pi * v)
    base = np.array([138, 132, 162.0]) + np.array([100, 100, 82.0]) * lit[:, None]
    rgb = np.broadcast_to(base[:, None, :], (h, w, 3)).copy()
    r = np.random.default_rng(9)
    rgb += (r.random((h, w)) - 0.5)[..., None] * 7
    return _rgba(rgb, 255)


def paint_neon(w, h):
    rgb = np.zeros((h, w, 3)) + np.array(ACCENT, float)
    v = np.linspace(-1, 1, h)[:, None]
    rgb = rgb + (255 - rgb) * (1 - np.abs(v))[..., None] ** 3 * 0.55     # white-hot core, accent at the rim
    return _rgba(np.broadcast_to(rgb, (h, w, 3)), 0)                      # alpha 0: fully emissive


def atlas():
    a = Atlas(128)
    a.add("flat", 48, 8, paint_flat)
    a.add("grind", 48, 12, paint_grind)
    a.add("edge", 32, 3, paint_edge)
    a.add("spine", 48, 4, paint_spine)
    a.add("handle", 32, 24, paint_handle)
    a.add("bolster", 8, 16, paint_bolster)
    a.add("ring", 48, 8, paint_ring)
    a.add("neon", 8, 4, paint_neon)
    return a


# ------------------------------------------------------------------ the Novak Karambit
def karambit():
    m = Mesh()
    Z = np.array([0, 0, 1.0])

    def P(x, y, z=0.0):
        return np.array([x, y + GRIP_Y, z])

    # ---- blade: the spine is an arc (convex side out), the edge is on the inside (concave) and hooks forward
    R, th0, th1 = 5.0, 0.0, math.radians(108)
    heel = np.array([0.72, 3.55])
    ctr = heel - np.array([R, 0.0])                    # centre to the -x side: the blade curls towards -x
    N = 22
    sections = []
    for i in range(N + 1):
        s = i / N
        th = th0 + (th1 - th0) * s
        sp = ctr + R * np.array([math.cos(th), math.sin(th)])            # on the spine
        inward = -np.array([math.cos(th), math.sin(th)])                  # towards the edge
        w = 2.35 * (1 - s) ** 0.58 * (1 + 0.16 * math.sin(math.pi * s)) + 1e-3
        t = 0.95 * (1 - 0.72 * s ** 1.1)
        # cross-section corners at d (0 spine -> 1 edge) and half-thickness
        prof = [(0.0, 0.34), (0.08, 0.5), (0.34, 0.47), (0.86, 0.06), (1.0, 0.0)]
        pts = []
        for d, hz in prof:
            q = sp + inward * d * w
            pts.append((q, hz * t))
        sections.append(pts)
    # faces: per side (+z / -z) the strips between profile corners, each its own atlas region
    strip_region = ["spine", "flat", "grind", "edge"]
    for side in (1, -1):
        for j in range(len(sections[0]) - 1):
            reg = strip_region[j]
            for i in range(N):
                a0, a1 = sections[i][j], sections[i][j + 1]
                b0, b1 = sections[i + 1][j], sections[i + 1][j + 1]
                ps = [P(a0[0][0], a0[0][1], side * a0[1]), P(b0[0][0], b0[0][1], side * b0[1]),
                      P(b1[0][0], b1[0][1], side * b1[1]), P(a1[0][0], a1[0][1], side * a1[1])]
                n = face_normal(ps)
                if n[2] * side < 0:
                    n = -n
                uv = [(reg, i / N, 0.0), (reg, (i + 1) / N, 0.0), (reg, (i + 1) / N, 1.0), (reg, i / N, 1.0)]
                if side < 0:
                    uv = [(r_, u, v) for (r_, u, v) in uv]
                m.quad("bfk_blade", ps, [n] * 4, uv)
    # spine cap between the two sides (d = 0)
    for i in range(N):
        a, b = sections[i][0], sections[i + 1][0]
        ps = [P(a[0][0], a[0][1], a[1]), P(b[0][0], b[0][1], b[1]), P(b[0][0], b[0][1], -b[1]), P(a[0][0], a[0][1], -a[1])]
        out = nrm(np.append(sections[i][0][0] - sections[i][1][0], 0.0))
        n = face_normal(ps)
        if np.dot(n, out) < 0:
            n = -n
        m.quad("bfk_blade", ps, [n] * 4, [("spine", i / N, 0), ("spine", (i + 1) / N, 0), ("spine", (i + 1) / N, 1), ("spine", i / N, 1)])
    # heel face (closes the blade where it meets the bolster)
    hs = sections[0]
    ring_pts = [P(q[0], q[1], hz) for q, hz in hs] + [P(q[0], q[1], -hz) for q, hz in reversed(hs)]
    c = np.mean(ring_pts, 0)
    for k in range(len(ring_pts)):
        p0, p1 = ring_pts[k], ring_pts[(k + 1) % len(ring_pts)]
        m.tri("bfk_blade", [c, p0, p1], [np.array([0, -1.0, 0])] * 3, [("bolster", 0.5, 0.5)] * 3)

    # neon grind line: a thin strip just proud of each flat, along the grind line (d ~ 0.34), fading out at the tip
    for side in (1, -1):
        for i in range(int(N * 0.82)):
            pts = []
            for ii in (i, i + 1):
                s = ii / N
                q0, h0 = sections[ii][2]          # grind line (d = 0.34)
                q1, h1 = sections[ii][3]          # bevel line (d = 0.86)
                f = 0.11 * (1 - 0.55 * s)         # strip width as a fraction of the grind, thinner towards the tip
                b = q0 + (q1 - q0) * f
                hb = h0 + (h1 - h0) * f
                pts.append((P(q0[0], q0[1], side * (h0 + 0.018)), P(b[0], b[1], side * (hb + 0.018))))
            ps = [pts[0][0], pts[1][0], pts[1][1], pts[0][1]]
            n = np.array([0, 0, float(side)])
            m.quad("bfk_neon", ps, [n] * 4, [("neon", 0, 0), ("neon", 1, 0), ("neon", 1, 1), ("neon", 0, 1)])

    # ---- bolster: a short faceted collar between blade and handle
    def rrect(cx, y, a_pos, a_neg, b, n=12, e=3.0):
        out = []
        for k in range(n):
            ang = 2 * math.pi * k / n
            cs, sn = math.cos(ang), math.sin(ang)
            a = a_pos if cs >= 0 else a_neg
            x = cx + a * math.copysign(abs(cs) ** (2 / e), cs)
            z = b * math.copysign(abs(sn) ** (2 / e), sn)
            out.append(P(x, y, z))
        return out

    bol = [rrect(-0.45, 3.0, 0.95, 0.95, 0.62, 8, 6), rrect(-0.45, 3.12, 1.03, 1.03, 0.68, 8, 6),
           rrect(-0.45, 3.42, 1.03, 1.03, 0.68, 8, 6), rrect(-0.45, 3.62, 0.94, 0.94, 0.58, 8, 6)]
    loft(m, "bfk_body", bol, "bolster", flat=True, outward=lambda p: np.array([p[0] + 0.45, 0, p[2]]))

    # ---- handle: bowed, three finger grooves on the -x side, pearl scales
    hy0, hy1 = -2.55, 3.0
    rings = []
    HN = 34
    for i in range(HN + 1):
        s = i / HN
        y = hy1 + (hy0 - hy1) * s                       # top -> bottom
        cx = -0.45 + 0.22 * math.sin(math.pi * s)       # gentle bow towards the spine side
        a_pos = 0.86 + 0.08 * math.sin(math.pi * s)
        groove = sum(0.17 * max(0.0, math.cos(min(1.0, abs(s - g) / 0.12) * math.pi / 2)) ** 2 for g in (0.27, 0.5, 0.73))
        a_neg = 0.92 - groove + 0.06 * math.sin(math.pi * s)
        b = 0.56 + 0.04 * math.sin(math.pi * s)
        rings.append(rrect(cx, y, a_pos, a_neg, b, 16, 3.2))
    loft(m, "bfk_body", rings, "handle", flat=False,
         outward=lambda p: np.array([p[0] + 0.45, 0, p[2]]))
    # bottom cap into the ring
    bot = rings[-1]
    c = np.mean(bot, 0)
    for k in range(len(bot)):
        m.tri("bfk_body", [c, bot[(k + 1) % len(bot)], bot[k]], [np.array([0, -1.0, 0])] * 3, [("ring", 0.5, 0.5)] * 3)
    # neon seam along the spine side of the handle (where the scales meet at +x)
    for i in range(1, HN - 1):
        p0, p1 = rings[i][0], rings[i + 1][0]
        off = np.array([0.03, 0, 0])
        ps = [p0 + off + np.array([0, 0, 0.07]), p1 + off + np.array([0, 0, 0.07]),
              p1 + off - np.array([0, 0, 0.07]), p0 + off - np.array([0, 0, 0.07])]
        m.quad("bfk_neon", ps, [np.array([1.0, 0, 0])] * 4, [("neon", 0, 0), ("neon", 1, 0), ("neon", 1, 1), ("neon", 0, 1)])

    # ---- finger ring (in the flat's plane), and its glowing inner rim
    rc = P(-0.45 + 0.0, hy0 - 1.62, 0)
    torus(m, "bfk_body", rc, 1.66, 0.40, [1, 0, 0], [0, 1, 0], "ring", seg=24, sides=8)
    torus(m, "bfk_neon", rc, 1.66 - 0.36, 0.075, [1, 0, 0], [0, 1, 0], "neon", seg=24, sides=6)
    return m


RING_CENTRE = (-0.45, GRIP_Y - 2.55 - 1.62, 0.0)   # spin pivot for the flip animations (the finger in the ring)


def bones(ident_prefix="bfk"):
    rc = list(RING_CENTRE)
    return [
        {"name": "wfx_item", "pivot": [0, 24, 0], "binding": "q.item_slot_to_bone_name(c.item_slot)"},
        # anim bones: bfk_pose (whole knife, inspect lift/tilt about the grip), bfk_spin (finger-ring spin)
        {"name": "bfk_pose", "parent": "wfx_item", "pivot": [0, 24, 0]},
        {"name": "bfk_spin", "parent": "bfk_pose", "pivot": rc},
        {"name": "bfk_blade", "parent": "bfk_spin", "pivot": [0, 24, 0]},
        {"name": "bfk_body", "parent": "bfk_spin", "pivot": [0, 24, 0]},
        {"name": "bfk_neon", "parent": "bfk_spin", "pivot": [0, 24, 0]},
    ]
