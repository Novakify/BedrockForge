"""ModelCore rig: one in-memory model format for GeckoBridge (Bedrock geo) and FiguraBridge (.bbmodel, F1).

Units and axes are Bedrock's (pixels, degrees, Bedrock rotation signs), because both GeckoLib and Bedrock already
use them. Lossless: keys this module does not model are kept in `extra` and written back, so
load_bedrock_geo -> write_bedrock_geo reproduces the input (tested over every geo file of the test mods).

    from ModelCore.rig import load_bedrock_geo, write_bedrock_geo
    rigs = load_bedrock_geo(json_dict)          # list[Rig] (a file can hold several geometries)
    rig.identifier = "geometry.gb.mod.thing"
    out = write_bedrock_geo(rigs, "1.12.0")
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Any

GEO_VERSIONS = ("1.12.0", "1.14.0", "1.16.0", "1.19.30", "1.21.0", "1.21.20")  # GeckoLib 5.5.7 ModelFormatVersion
_CUBE_KEYS = ("origin", "size", "rotation", "pivot", "inflate", "mirror", "uv")
_BONE_KEYS = ("name", "parent", "pivot", "rotation", "mirror", "inflate", "cubes", "locators", "poly_mesh",
              "texture_meshes", "binding")


@dataclass
class Cube:
    origin: list
    size: list
    uv: Any = None                 # [u, v] box UV, or {face: {"uv", "uv_size", "uv_rotation"?, ...}} per-face
    rotation: list | None = None
    pivot: list | None = None
    inflate: float | None = None
    mirror: bool | None = None
    extra: dict = field(default_factory=dict)

    @property
    def per_face(self): return isinstance(self.uv, dict)


@dataclass
class Bone:
    name: str
    parent: str | None = None
    pivot: list | None = None
    rotation: list | None = None   # rest rotation, degrees
    mirror: bool | None = None
    inflate: float | None = None
    cubes: list = field(default_factory=list)
    locators: dict | None = None   # name -> [x,y,z] or {"offset", "rotation", ...}
    poly_mesh: dict | None = None  # Bedrock poly mesh; Figura .bbmodel meshes map here (F1)
    texture_meshes: list | None = None
    binding: str | None = None     # Molang (attachables / items)
    extra: dict = field(default_factory=dict)


@dataclass
class Rig:
    identifier: str
    texture_width: int = 64
    texture_height: int = 64
    bones: list = field(default_factory=list)
    description_extra: dict = field(default_factory=dict)   # visible_bounds_* etc.
    extra: dict = field(default_factory=dict)               # geometry-level keys besides description/bones
    source_format: str = "bedrock_geo"

    def bone(self, name):
        return next((b for b in self.bones if b.name == name), None)

    def children(self, name):
        return [b for b in self.bones if b.parent == name]

    def stats(self):
        return {"bones": len(self.bones), "cubes": sum(len(b.cubes) for b in self.bones),
                "meshes": sum(1 for b in self.bones if b.poly_mesh), "locators": sum(len(b.locators or {}) for b in self.bones)}


def _pick(d, keys):
    return {k: d[k] for k in keys if k in d}, {k: v for k, v in d.items() if k not in keys}


def load_bedrock_geo(geo: dict) -> list[Rig]:
    fv = geo.get("format_version", "")
    if "minecraft:geometry" not in geo:
        raise ValueError(f"not a 1.12+ Bedrock geometry (format {fv!r}); legacy 1.8.0 files are not read by GeckoLib either")
    rigs = []
    for g in geo["minecraft:geometry"]:
        desc = dict(g.get("description", {}))
        rig = Rig(identifier=desc.pop("identifier", "geometry.unknown"),
                  texture_width=desc.pop("texture_width", 64), texture_height=desc.pop("texture_height", 64),
                  description_extra=desc, extra={k: v for k, v in g.items() if k not in ("description", "bones")})
        for b in g.get("bones", []):
            known, extra = _pick(b, _BONE_KEYS)
            cubes = []
            for c in known.get("cubes", []):
                ck, cx = _pick(c, _CUBE_KEYS)
                cubes.append(Cube(origin=ck.get("origin"), size=ck.get("size"), uv=ck.get("uv"), rotation=ck.get("rotation"),
                                  pivot=ck.get("pivot"), inflate=ck.get("inflate"), mirror=ck.get("mirror"), extra=cx))
            known.pop("cubes", None)
            rig.bones.append(Bone(cubes=cubes, extra=extra, **known))
        rigs.append(rig)
    return rigs


def _put(d, k, v):
    if v is not None: d[k] = v


def write_bedrock_geo(rigs: list[Rig], format_version: str = "1.12.0") -> dict:
    geoms = []
    for r in rigs:
        desc = {"identifier": r.identifier, "texture_width": r.texture_width, "texture_height": r.texture_height, **r.description_extra}
        bones = []
        for b in r.bones:
            o = {"name": b.name}
            for k in ("parent", "pivot", "rotation", "mirror", "inflate", "binding"): _put(o, k, getattr(b, k))
            if b.cubes or "cubes" in b.extra:
                cl = []
                for c in b.cubes:
                    co = {}
                    for k in _CUBE_KEYS: _put(co, k, getattr(c, k))
                    co.update(c.extra); cl.append(co)
                o["cubes"] = cl
            for k in ("locators", "poly_mesh", "texture_meshes"): _put(o, k, getattr(b, k))
            o.update(b.extra); bones.append(o)
        g = {"description": desc, **r.extra}
        g["bones"] = bones
        geoms.append(g)
    return {"format_version": format_version, "minecraft:geometry": geoms}


def load_bbmodel(data: dict) -> Rig:
    """Figura / Blockbench .bbmodel -> Rig (cubes, meshes, groups). FiguraBridge F1."""
    raise NotImplementedError("load_bbmodel: FiguraBridge F1")


def _norm(x):
    """Equality helper for round-trip tests: ints and floats compare by value, key order ignored."""
    if isinstance(x, dict): return {k: _norm(v) for k, v in x.items()}
    if isinstance(x, list): return [_norm(v) for v in x]
    if isinstance(x, bool) or x is None or isinstance(x, str): return x
    return float(x)


def roundtrip_equal(geo: dict) -> bool:
    out = write_bedrock_geo(load_bedrock_geo(geo), geo.get("format_version", "1.12.0"))
    return _norm(out) == _norm(geo)
