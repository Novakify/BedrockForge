"""Bedrock Forge cosmetics: small mesh kit for real 3D held models (by Novak).

Models are built here as triangle/quad meshes in model units (1/16 block), in Mojang's trident frame (see
tools/WorldFx/atmos/equip.py): the item stands along +y with its grip centre on (0, 24, 0), its flat facing +-z.
Each part has a material region of the atlas; vertices carry (u, v) inside that region. Export is Bedrock
`poly_mesh` (normalized uvs, one normal per corner), so the game draws the shape natively in the item pass:
no extra draw calls of ours, nothing for a server to know about.
"""
from __future__ import annotations

import math

import numpy as np


class Mesh:
    def __init__(self):
        self.parts: dict[str, list] = {}   # bone -> list of polys; poly = list of (pos, normal, (region, u, v))

    def quad(self, bone, ps, ns, uvs):
        self.parts.setdefault(bone, []).append([(np.asarray(p, float), np.asarray(n, float), uv) for p, n, uv in zip(ps, ns, uvs)])

    def tri(self, bone, ps, ns, uvs):
        self.quad(bone, list(ps) + [ps[2]], list(ns) + [ns[2]], list(uvs) + [uvs[2]])


def nrm(v):
    v = np.asarray(v, float)
    l = np.linalg.norm(v)
    return v / l if l > 1e-9 else np.array([0.0, 0.0, 1.0])


def face_normal(ps):
    p = [np.asarray(x, float) for x in ps]
    n = np.cross(p[1] - p[0], p[2] - p[0])
    if np.linalg.norm(n) < 1e-9 and len(p) > 3:
        n = np.cross(p[2] - p[0], p[3] - p[0])
    return nrm(n)


def loft(mesh, bone, rings, region, flat=True, close=True, outward=None, cap=False):
    """rings: list of cross-sections, each a list of 3D points (same count). Quads between consecutive rings;
    u runs along the loft, v around the section. flat=True gives faceted (per-poly) normals."""
    R = len(rings)
    K = len(rings[0])
    kk = K if close else K - 1
    for i in range(R - 1):
        for k in range(kk):
            k2 = (k + 1) % K
            ps = [rings[i][k], rings[i + 1][k], rings[i + 1][k2], rings[i][k2]]
            uv = [(region, i / (R - 1), k / kk), (region, (i + 1) / (R - 1), k / kk),
                  (region, (i + 1) / (R - 1), (k + 1) / kk), (region, i / (R - 1), (k + 1) / kk)]
            if flat:
                n = face_normal(ps)
                if outward is not None and np.dot(n, outward(np.mean(ps, 0))) < 0:
                    n = -n
                ns = [n] * 4
            else:
                ns = [smooth_normal(rings, a, b) for a, b in ((i, k), (i + 1, k), (i + 1, k2), (i, k2))]
                if outward is not None:
                    ns = [n if np.dot(n, outward(p)) >= 0 else -n for n, p in zip(ns, ps)]
            mesh.quad(bone, ps, ns, uv)


def smooth_normal(rings, i, k):
    R, K = len(rings), len(rings[0])
    c = np.mean(rings[i], 0)
    return nrm(np.asarray(rings[i][k]) - c)


def torus(mesh, bone, centre, R, r, axis_u, axis_v, region, seg=22, sides=8, arc=(0.0, 2 * math.pi)):
    """ring in the plane (axis_u, axis_v) around centre; smooth normals"""
    c = np.asarray(centre, float)
    au, av = nrm(axis_u), nrm(axis_v)
    aw = np.cross(au, av)
    a0, a1 = arc
    full = abs((a1 - a0) - 2 * math.pi) < 1e-6
    n_ring = seg if full else seg + 1
    for i in range(seg):
        t0 = a0 + (a1 - a0) * i / seg
        t1 = a0 + (a1 - a0) * (i + 1) / seg
        for j in range(sides):
            s0 = 2 * math.pi * j / sides
            s1 = 2 * math.pi * (j + 1) / sides
            ps, ns, uvs = [], [], []
            for t, s, uu, vv in ((t0, s0, i, j), (t1, s0, i + 1, j), (t1, s1, i + 1, j + 1), (t0, s1, i, j + 1)):
                d = math.cos(t) * au + math.sin(t) * av
                n = math.cos(s) * d + math.sin(s) * aw
                ps.append(c + R * d + r * n)
                ns.append(n)
                uvs.append((region, uu / seg, vv / sides))
            mesh.quad(bone, ps, ns, uvs)


# ------------------------------------------------------------------ atlas
class Atlas:
    """regions packed on a fixed grid of a square texture; each region is painted by a function (w, h) -> RGBA"""

    def __init__(self, size=128):
        self.size = size
        self.img = np.zeros((size, size, 4), np.uint8)
        self.rect: dict[str, tuple[int, int, int, int]] = {}
        self._x = self._y = self._shelf = 0

    def add(self, name, w, h, painter):
        if self._x + w > self.size:
            self._x, self._y, self._shelf = 0, self._y + self._shelf, 0
        x, y = self._x, self._y
        assert y + h <= self.size, f"atlas full at {name}"
        self.img[y:y + h, x:x + w] = painter(w, h)
        self.rect[name] = (x, y, w, h)
        self._x += w
        self._shelf = max(self._shelf, h)

    def uv(self, region, u, v):
        """region-local (u along, v across) in 0..1 -> normalized atlas uv (Bedrock poly_mesh: v up from the bottom).
        Half a texel of inset so filtering never reads the neighbour."""
        x, y, w, h = self.rect[region]
        px = x + 0.5 + u * (w - 1)
        py = y + 0.5 + v * (h - 1)
        return px / self.size, 1.0 - py / self.size

    def texel(self, region, u, v):
        x, y, w, h = self.rect[region]
        return x + 0.5 + u * (w - 1), y + 0.5 + v * (h - 1)


def export_poly_mesh(mesh, atlas, bones_meta, ident, visible=(3, 4)):
    """bones_meta: list of dict(name, parent?, pivot, rotation?, binding?, mesh_bone?) in order; the bone's polys come
    from mesh.parts[mesh_bone or name]"""
    bones = []
    for bm in bones_meta:
        b = {k: v for k, v in bm.items() if k != "mesh_bone"}
        polys = mesh.parts.get(bm.get("mesh_bone", bm["name"]), [])
        if polys:
            P, N, U, F = [], [], [], []
            pi, ni, ui = {}, {}, {}

            def idx(table, lst, key, val):
                if key not in table:
                    table[key] = len(lst)
                    lst.append(val)
                return table[key]

            for poly in polys:
                face = []
                for p, n, (reg, u, v) in poly:
                    pk = tuple(np.round(p, 4))
                    nk = tuple(np.round(n, 3))
                    uvv = atlas.uv(reg, u, v)
                    uk = (round(uvv[0], 5), round(uvv[1], 5))
                    face.append([idx(pi, P, pk, [round(float(x), 4) for x in pk]),
                                 idx(ni, N, nk, [round(float(x), 3) for x in nk]),
                                 idx(ui, U, uk, list(uk))])
                F.append(face)
            b["poly_mesh"] = {"normalized_uvs": True, "positions": P, "normals": N, "uvs": U, "polys": F}
        bones.append(b)
    return {"format_version": "1.16.0", "minecraft:geometry": [{
        "description": {"identifier": ident, "texture_width": atlas.size, "texture_height": atlas.size,
                        "visible_bounds_width": visible[0], "visible_bounds_height": visible[1],
                        "visible_bounds_offset": [0, 1, 0]},
        "bones": bones}]}


def stats(mesh):
    return {b: len(p) for b, p in mesh.parts.items()}
