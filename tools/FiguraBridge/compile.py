#!/usr/bin/env python3
"""FiguraBridge compiler: Figura avatar folder -> Figura avatar NBT (as JSON) + rig contract + Bedrock resource pack.

    python3 -I tools/FiguraBridge/compile.py AVATAR_DIR OUT_DIR --vanilla BEDROCK_SAMPLES/resource_pack [--uuid U]

Outputs
    OUT_DIR/avatar/figura_avatar.json   the avatar NBT exactly as Figura 0.1.6 builds it (figura_nbt.py), byte
                                        arrays as {"__file": relpath} next to it (textures/, sounds/, resources/)
    OUT_DIR/rig.json                    bridge contract read by the native runtime (parts, textures, sounds, vars)
    OUT_DIR/rp/                         Bedrock resource pack replacing the player model; every Figura part is a
                                        bone driven by Molang variables v.fb<i>* (see VARS_DOC)

Bedrock conventions used (Blockbench's Bedrock exporter, mirrored on X):
    bone pivot = [-piv.x, piv.y, piv.z]; NO bind rotation anywhere (rotation comes from the drive animation)
    cube origin = [-to.x, from.y, from.z], size = to - from, inflate as is
    face uv (n/e/s/w) = [u1, v1] + [u2-u1, v2-v1]; up/down = [u2, v2] + [u1-u2, v1-v2]
    mesh = poly_mesh, positions unrotated (Figura already stores origin + vertex), x negated, winding reversed
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import shutil
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import figura_nbt as fn  # noqa: E402
from avatar_pack import SKELETON  # noqa: E402

GEO_FORMAT = "1.21.0"
FACE_NAMES = {"n": "north", "s": "south", "w": "west", "e": "east", "u": "up", "d": "down"}
TEX_TYPES = ("d", "e", "n", "s")
PACK_NS = uuid.UUID("6f1b0e1e-4c1a-4f0b-9d1e-f16a00b1d6e0")  # same namespace as avatar_pack.py

VARS_DOC = {
    "v.fb<i>rx / ry / rz": "bone rotation of part i, Bedrock bone convention (degrees). Default = static Figura rot "
                           "converted to [-rx, -ry, rz]. Native code writes the final value (Figura rot + anim + Lua).",
    "v.fb<i>px / py / pz": "bone position offset of part i (Bedrock pixels, Bedrock axis signs). Default 0.",
    "v.fb<i>sx / sy / sz": "bone scale of part i. Default 1.",
    "v.fb<i>v": "EFFECTIVE visibility of part i (1/0) = own visible AND all ancestors. Default from the NBT 'vsb' chain.",
    "v.fb<i>t": "primary texture override of part i: index into rig.json textureFiles; < 0 (default -1) = the part's "
                "own Figura texture set ('d' file of each face's set).",
    "v.fb<i>e": "secondary (emissive) texture override of part i: textureFiles index; < 0 (default -1) = own set's 'e'.",
    "v.fbvan": "1 = also render the vanilla player (Mojang render controllers). Default 0 (hidden, like Figura).",
    "render controllers": "controller.render.player.figura_p<f> (primary, Material.fbd = entity_alphablend) and "
                          "figura_s<f> (secondary, Material.fbe = entity_emissive_alpha) for every d/e texture file f, "
                          "texture key fbt<f>; third person only (!variable.is_first_person).",
}


def num(x) -> float:
    x = float(x)
    if x == 0 or abs(x) < 1e-9:
        return 0.0
    return float(fn.f32_repr(fn.f32(x))) if math.isfinite(x) else x


def molang_num(x) -> str:
    s = repr(num(x))
    return s[:-2] if s.endswith(".0") else s


def read_vec(tag: dict, key: str, default=(0.0, 0.0, 0.0)):
    v = tag.get(key)
    return tuple(float(c) for c in v[:3]) if isinstance(v, list) and len(v) >= 3 else default


def read_vec4(tag: dict, key: str):
    v = tag.get(key)
    return tuple(float(c) for c in v[:4]) if isinstance(v, list) and len(v) >= 4 else (0.0, 0.0, 0.0, 0.0)


def slugify(s: str) -> str:
    return "".join(c.lower() if c.isalnum() else "_" for c in s).strip("_") or "avatar"


# ----------------------------------------------------------------------------------------------------------------
# textures
# ----------------------------------------------------------------------------------------------------------------

def build_textures(nbt: dict):
    """textureFiles (one per d/e/n/s image) + textureSets (Figura's textures.data order = global texture ids)."""
    tex = nbt.get("textures", {"src": {}, "data": []})
    src, data = tex.get("src", {}), tex.get("data", [])
    files, by_path, sets = [], {}, []
    for si, entry in enumerate(data):
        s = {"i": si, "name": "", "d": None, "e": None, "n": None, "s": None, "uvW": -1, "uvH": -1}
        for t in TEX_TYPES:
            path = entry.get(t)
            if path is None:
                continue
            if path not in by_path:
                rep = src.get(path)
                if isinstance(rep, fn.FileRef):
                    w, h = fn.png_dimensions(rep.data)
                    f = {"i": len(files), "figuraName": path, "set": si, "type": t, "file": "avatar/" + rep.relpath,
                         "w": w, "h": h, "_data": rep.data}
                elif isinstance(rep, list):  # ignoredTextures: Figura makes a blank texture of this size
                    f = {"i": len(files), "figuraName": path, "set": si, "type": t, "file": None,
                         "w": rep[0], "h": rep[1], "_data": None}
                else:
                    raise fn.FiguraCompileError(f"texture {path} has no source")
                by_path[path] = f["i"]
                files.append(f)
            s[t] = by_path[path]
        # AvatarRenderer.getTextureName
        for t in TEX_TYPES:
            p = entry.get(t)
            if p:
                s["name"] = p if t == "d" else p[:-2]
                break
        # FiguraTextureSet.getWidth/Height: first non-null texture of (d, e, s, n) - used to normalise UVs
        for t in ("d", "e", "s", "n"):
            if s[t] is not None:
                s["uvW"], s["uvH"] = files[s[t]]["w"], files[s[t]]["h"]
                break
        sets.append(s)
    return files, sets


# ----------------------------------------------------------------------------------------------------------------
# parts
# ----------------------------------------------------------------------------------------------------------------

class Part:
    def __init__(self, i, tag, parent, path, vis_eff, smooth):
        self.i, self.tag, self.parent, self.path = i, tag, parent, path
        self.name = tag.get("name", "")
        self.vis_eff = vis_eff
        self.smooth = smooth
        cd, md = tag.get("cube_data"), tag.get("mesh_data")
        # FiguraModelPartReader.hasCubeData / hasMeshData: present AND non-empty compound
        self.type = "CUBE" if isinstance(cd, dict) and cd else "MESH" if isinstance(md, dict) and md else "GROUP"
        self.piv = read_vec(tag, "piv")
        self.rot = read_vec(tag, "rot")
        pt = tag.get("pt", "None")
        self.pt = pt if pt in fn.PARENT_TYPE_NAMES else "None"  # ParentType.valueOf, invalid ignored
        self.vsb = tag.get("vsb", True) is not False
        self.bone = f"p{i}"
        self.geo: dict[int, dict] = {}  # texture set -> {"cubes": [...]} or {"poly_mesh": {...}}
        self.children: list[Part] = []


def rgba_png(data: bytes) -> bytes:
    """Bedrock ignores a palette PNG's tRNS chunk (transparent texels render with their palette colour), so every
    texture goes into the pack as 8-bit RGBA. Figura (Java NativeImage) reads all PNG types, so this changes nothing."""
    try:
        import io
        from PIL import Image
        im = Image.open(io.BytesIO(data))
        if im.mode == "RGBA" and im.info.get("transparency") is None:
            return data
        buf = io.BytesIO()
        im.convert("RGBA").save(buf, "PNG")
        return buf.getvalue()
    except ImportError:
        print("[FiguraBridge] Pillow missing: textures copied as is (palette PNG transparency breaks on Bedrock)", file=sys.stderr)
        return data


DYN_SLOTS = 4      # runtime texture slots (see build)
DYN_SIZE = 64      # their placeholder size (same-size uploads are updated in place)
DUPS = 1           # spare geometry copies per part for ModelPart:copy() (see build)
BONE_LIMIT = 250   # per geometry; Bedrock breaks somewhere past 255 (see chunk_bones)


def chunk_bones(bones: list[dict], skeleton: set[str], limit: int = BONE_LIMIT) -> list[list[dict]]:
    """Split one bone list (parents before children) into geometries of <= limit bones. Each chunk = vanilla skeleton +
    whole ancestor chains of the geometry bones it holds; bones with no geometry below them are dropped."""
    by = {b["name"]: b for b in bones}
    has_geo = {n: bool(b.get("cubes") or b.get("poly_mesh")) for n, b in by.items()}
    for b in reversed(bones):  # children come after parents: propagate "has geometry below" upwards
        par = b.get("parent")
        if par in has_geo and has_geo[b["name"]]:
            has_geo[par] = True
    def chain(n):
        out = []
        while n is not None and n not in skeleton:
            out.append(n)
            n = by[n].get("parent")
        return out
    base = [b["name"] for b in bones if b["name"] in skeleton]
    chunks, cur = [], set()
    for b in bones:
        n = b["name"]
        if n in skeleton or not has_geo[n] or not (b.get("cubes") or b.get("poly_mesh")):
            continue
        need = set(chain(n)) - cur
        if cur and len(base) + len(cur) + len(need) > limit:
            chunks.append(cur)
            cur = set()
            need = set(chain(n))
        if len(base) + len(need) > limit:
            raise fn.FiguraCompileError(f"bone {n} is nested deeper than one geometry allows ({len(need)} ancestors)")
        cur |= need
    if cur or not chunks:
        chunks.append(cur)
    keep = set(base)
    return [[b for b in bones if b["name"] in keep or b["name"] in c] for c in chunks]


def primary_material(f: dict) -> str:
    """Bedrock's entity_alphablend draws without depth writes, so a whole avatar on it sorts wrong (verified in game:
    CatMaid's head and body cubes vanished behind their own layers); entity_alphatest (cutout) is right for every
    texture whose alpha is only 0 or 255. Textures with real partial alpha keep blending."""
    try:
        import io
        from PIL import Image
        a = Image.open(io.BytesIO(f["_data"])).convert("RGBA").getchannel("A")
        partial = sum(a.histogram()[1:255]) > 0
        return "Material.fbb" if partial else "Material.fbd"
    except Exception:  # noqa: BLE001
        return "Material.fbd"


UV_METHODS = r"(?:setUV|uv|setUVPixels|uvPixels|setUVMatrix|uvMatrix)"


def uv_dynamic_paths(script_text: str) -> set[str]:
    """Model part paths a script moves the UVs of (ModelPart:setUV / setUVPixels / setUVMatrix and their aliases).
    Catches `models.a.b:setUV(...)` and `X = models.a.b ... X:setUV(...)` (also `X.Y = models...`)."""
    import re
    found = set(re.findall(r"\b(models(?:\.[A-Za-z_]\w*)+)\s*:\s*" + UV_METHODS + r"\s*\(", script_text))
    alias = {}
    for name, path in re.findall(r"([A-Za-z_][\w.]*)\s*=\s*(models(?:\.[A-Za-z_]\w*)+)\b(?!\s*[:(])", script_text):
        alias.setdefault(name, set()).add(path)
    for name in re.findall(r"([A-Za-z_][\w.]*)\s*:\s*" + UV_METHODS + r"\s*\(", script_text):
        found |= alias.get(name, set())
    return found


def blank_png(w: int, h: int) -> bytes:
    """fully transparent RGBA PNG (stdlib only)"""
    import struct
    import zlib
    raw = b"".join(b"\x00" + b"\x00\x00\x00\x00" * w for _ in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def kinds_for(f: dict):
    """render controller kinds for a texture file: a primary ('d') file only ever shows as a primary texture, an
    emissive ('e') file as a secondary one - one full geometry pass per controller, so no unused pairs"""
    if f["type"] == "e":
        return (("s", "e", "Material.fbe", "e"),)
    return (("p", "t", primary_material(f), "d"),)


def collect_parts(models: dict) -> list[Part]:
    parts: list[Part] = []

    def visit(tag, parent: Part | None, prefix: str, vis_parent: bool, smooth: bool):
        path = (prefix + "." if prefix else "") + tag.get("name", "")
        if "smo" in tag:  # FiguraModelPartReader: 'smo' overrides the inherited smoothNormals
            smooth = bool(tag["smo"])
        vsb = tag.get("vsb", True) is not False
        p = Part(len(parts), tag, parent, path, vis_parent and vsb, smooth)
        parts.append(p)
        if parent:
            parent.children.append(p)
        for c in tag.get("chld", []) or []:
            visit(c, p, path, p.vis_eff, smooth)

    visit(models, None, "", True, False)
    return parts


def cube_geometry(p: Part, sets, geo_w, geo_h):
    tag = p.tag
    frm, to = read_vec(tag, "f"), read_vec(tag, "t")
    inf = float(tag.get("inf", 0.0))
    for key in ("n", "s", "w", "e", "u", "d"):
        face = tag["cube_data"].get(key)
        if face is None:
            continue
        sid = int(face["tex"])
        u1, v1, u2, v2 = read_vec4(face, "uv")
        s = sets[sid] if 0 <= sid < len(sets) else None
        # Figura's NBT uv is in texels of the set's real image; Bedrock per-face uv is in geometry texture units
        kx = geo_w / s["uvW"] if s and s["uvW"] > 0 else 1.0
        ky = geo_h / s["uvH"] if s and s["uvH"] > 0 else 1.0
        u1, u2, v1, v2 = u1 * kx, u2 * kx, v1 * ky, v2 * ky
        if key in ("u", "d"):
            fo = {"uv": [num(u2), num(v2)], "uv_size": [num(u1 - u2), num(v1 - v2)]}
        else:
            fo = {"uv": [num(u1), num(v1)], "uv_size": [num(u2 - u1), num(v2 - v1)]}
        rot = int(float(face.get("rot", 0.0)) / 90) % 4  # FiguraModelPartReader: (int)(rot / 90f)
        if rot:
            fo["uv_rotation"] = rot * 90
        g = p.geo.setdefault(sid, {"cubes": [{"origin": [num(-to[0]), num(frm[1]), num(frm[2])],
                                               "size": [num(to[0] - frm[0]), num(to[1] - frm[1]), num(to[2] - frm[2])],
                                               "uv": {}}]})
        if inf:
            g["cubes"][0]["inflate"] = num(inf)
        g["cubes"][0]["uv"][FACE_NAMES[key]] = fo


def mesh_geometry(p: Part, sets):
    md = p.tag["mesh_data"]
    verts, uvs, tex, fac = md.get("vtx", []), md.get("uvs", []), md.get("tex", []), md.get("fac", [])
    best = 2 if len(verts) > 32767 * 3 else 1 if len(verts) > 255 * 3 else 0  # FiguraModelPartReader.readMesh

    def vid(k):
        v = int(fac[k])
        return v & 0xFF if best == 0 else v & 0xFFFF if best == 1 else v

    faces = []  # (set, [(pos, uv)], normal)
    vi = uvi = 0
    for packed in tex:
        packed = int(packed)
        sid, n = packed >> 4, packed & 0xF
        pts = []
        for j in range(n):
            k = vid(vi + j)
            pts.append(((float(verts[3 * k]), float(verts[3 * k + 1]), float(verts[3 * k + 2])),
                        (float(uvs[uvi + 2 * j]), float(uvs[uvi + 2 * j + 1]))))
        # Figura normal: (p3 - p2) x (p1 - p2), normalised
        a, b, c = pts[0][0], pts[1][0], pts[2][0]
        nrm = fn._cross(fn._sub(c, b), fn._sub(a, b))
        ln = math.sqrt(fn._dot(nrm, nrm)) or 1.0
        faces.append((sid, pts, tuple(x / ln for x in nrm)))
        vi += n
        uvi += 2 * n

    smooth = {}
    if p.smooth:  # FiguraModelPartReader.smoothfy: average normals of vertices sharing a position (per part)
        for _, pts, nrm in faces:
            for pos, _ in pts + ([pts[2]] if len(pts) == 3 else []):
                acc = smooth.setdefault(pos, [0.0, 0.0, 0.0])
                for q in range(3):
                    acc[q] += nrm[q]

    for sid, pts, nrm in faces:
        s = sets[sid] if 0 <= sid < len(sets) else None
        uw = s["uvW"] if s and s["uvW"] > 0 else 1
        uh = s["uvH"] if s and s["uvH"] > 0 else 1
        g = p.geo.setdefault(sid, {"poly_mesh": {"normalized_uvs": True, "positions": [], "normals": [], "uvs": [],
                                                  "polys": []}})["poly_mesh"]
        poly = []
        for pos, uv in reversed(pts):  # x mirror -> reverse winding to keep faces outward
            g["positions"].append([num(-pos[0]), num(pos[1]), num(pos[2])])
            nn = nrm
            if p.smooth:
                acc = smooth[pos]
                l2 = math.sqrt(sum(x * x for x in acc)) or 1.0
                nn = tuple(x / l2 for x in acc)
            g["normals"].append([num(-nn[0]), num(nn[1]), num(nn[2])])
            g["uvs"].append([num(uv[0] / uw), num(1 - uv[1] / uh)])
            k = len(g["positions"]) - 1
            poly.append([k, k, k])
        if len(poly) == 3:
            poly.append(list(poly[2]))  # quads only: triangles repeat the last vertex
        g["polys"].append(poly)


# ----------------------------------------------------------------------------------------------------------------
# main build
# ----------------------------------------------------------------------------------------------------------------

def build(avatar_dir: Path, out: Path, vanilla_rp: Path, owner_uuid: str) -> dict:
    avatar_dir = avatar_dir.resolve()
    compiled = fn.compile_avatar(avatar_dir, owner_uuid)
    nbt = compiled.nbt
    meta_name = nbt["metadata"]["name"]
    slug = slugify(avatar_dir.name)

    if out.exists():
        shutil.rmtree(out)
    (out / "avatar").mkdir(parents=True)

    # 1. avatar NBT
    text, side = fn.nbt_to_json(nbt)
    (out / "avatar/figura_avatar.json").write_text(text, encoding="utf-8")
    for rel, data in side.items():
        dst = out / "avatar" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)

    # 2. rig
    files, sets = build_textures(nbt)
    parts = collect_parts(nbt["models"])
    # geometry texture size = size of the set used by most cube faces (its per-face uvs then need no rescale);
    # meshes use normalized uvs so they don't care
    face_count = {}
    for p in parts:
        if p.type == "CUBE":
            for face in p.tag["cube_data"].values():
                face_count[int(face["tex"])] = face_count.get(int(face["tex"]), 0) + 1
    geo_w, geo_h = 16, 16
    for s in sorted(sets, key=lambda s: -face_count.get(s["i"], 0)):
        if s["uvW"] > 0:
            geo_w, geo_h = s["uvW"], s["uvH"]
            break
    for p in parts:
        if p.type == "CUBE":
            cube_geometry(p, sets, geo_w, geo_h)
        elif p.type == "MESH":
            mesh_geometry(p, sets)

    # dup bones: Bedrock can't create bones at runtime, so every part with geometry gets DUPS spare free-floating
    # copies of its geometry (driven in model space by the runtime's bone solve) for parts scripts create with
    # ModelPart:copy(). Only when a script calls copy().
    script_text = "\n".join(v if isinstance(v, str) else v.decode("utf-8", "replace") if isinstance(v, bytes) else ""
                            for v in (nbt.get("scripts") or {}).values())
    n_dups = DUPS if ":copy(" in script_text else 0
    dups = []
    for _ in range(n_dups):
        for src in [p for p in parts if p.geo]:
            d = Part(len(parts) + len(dups), src.tag, None, src.path + "#dup", False, src.smooth)
            d.geo = copy.deepcopy(src.geo)
            d.rot = [0.0, 0.0, 0.0]
            d.source = src
            dups.append(d)

    # parts whose UVs scripts move (eyes, mouths, blink frames): drawn by their own render controllers with a Molang
    # uv_anim offset/scale (v.fb<i>uu/uv/usu/usv); a UV matrix also applies to the part's descendants
    uv_roots = uv_dynamic_paths(script_text)
    uv_parts = set()
    for p in parts:
        q = p
        while q is not None:
            if q.path in uv_roots:
                uv_parts.add(p.i)
                break
            q = q.parent
    for d in dups:
        if d.source.i in uv_parts:
            uv_parts.add(d.i)

    # bones: vanilla skeleton (no cubes) + one bone per part (+ per-texture-set sub-bones)
    bones = [{"name": n, **({"parent": par} if par else {}), "pivot": [float(c) for c in pv]} for n, par, pv in SKELETON]
    geo_bones = {}  # bone name -> (part, set or None)
    for p in parts + dups:
        b = {"name": p.bone, "pivot": [num(-p.piv[0]), num(p.piv[1]), num(p.piv[2])]}
        if p.parent:
            b["parent"] = p.parent.bone
        bones.append(b)
        geo_bones[p.bone] = (p, None)
        if not p.geo:
            continue
        split = len(p.geo) > 1 or bool(p.children)  # geometry bones stay leaves (see report: part_visibility)
        p.subs = []
        for sid in sorted(p.geo):
            if split:
                sb = {"name": f"{p.bone}s{sid}", "parent": p.bone, "pivot": list(b["pivot"]), **p.geo[sid]}
                bones.append(sb)
                geo_bones[sb["name"]] = (p, sid)
                p.subs.append({"bone": sb["name"], "set": sid})
            else:
                b.update(p.geo[sid])
                geo_bones[p.bone] = (p, sid)
                p.subs.append({"bone": p.bone, "set": sid})

    # First person (Figura renderMode FIRST_PERSON): Bedrock only draws first-person arms from bones under its vanilla
    # rightArm / leftArm (animated by the first-person animations). Every subtree rooted at an arm-typed part (no
    # arm-typed ancestor) gets a copy "f<i>" parented to that vanilla bone, driven by the SAME v.fb<i>* variables
    # (the runtime computes them in FIRST_PERSON mode, where arm parts get no vanilla offsets).
    fp_bones = {}   # bone name -> (part, set or None)
    fp_roots = []   # (part, 'rightArm'|'leftArm')
    def arm_root(p):
        q = p.parent
        while q is not None:
            if q.pt in ("RightArm", "LeftArm"):
                return False
            q = q.parent
        return p.pt in ("RightArm", "LeftArm")
    def copy_subtree(p, parent_bone):
        b = {"name": f"f{p.i}", "parent": parent_bone, "pivot": [num(-p.piv[0]), num(p.piv[1]), num(p.piv[2])]}
        bones.append(b)
        fp_bones[b["name"]] = (p, None)
        if p.geo:
            split = len(p.geo) > 1 or bool(p.children)
            for sid in sorted(p.geo):
                if split:
                    sb = {"name": f"f{p.i}s{sid}", "parent": b["name"], "pivot": list(b["pivot"]), **json.loads(json.dumps(p.geo[sid]))}
                    bones.append(sb)
                    fp_bones[sb["name"]] = (p, sid)
                else:
                    b.update(json.loads(json.dumps(p.geo[sid])))
                    fp_bones[b["name"]] = (p, sid)
        for c in p.children:
            copy_subtree(c, b["name"])
    for p in parts:
        if arm_root(p):
            van = "rightArm" if p.pt == "RightArm" else "leftArm"
            fp_roots.append((p, van))
            copy_subtree(p, van)

    names = [b["name"] for b in bones]
    if len(set(names)) != len(names):
        raise fn.FiguraCompileError("duplicate bone names")

    # Bedrock stops applying animations / part_visibility to a geometry with more than ~255 bones (verified in game:
    # CatMaid at 326 bones froze in its bind pose with every part shown; trimmed to 230 it posed correctly). Big avatars
    # are split into several geometries ("chunks", each <= BONE_LIMIT bones) rendered together. A chunk carries the
    # full ancestor chain of its geometry bones; ancestors repeated in several chunks keep the same names, so the one
    # drive animation moves them identically. Bones whose subtree has no geometry are dropped.
    chunks = chunk_bones(bones, {n for n, _, _ in SKELETON})
    geo_ids = [f"geometry.figura.{slug}" + (f".c{k}" if k else "") for k in range(len(chunks))]
    geometry = {"format_version": GEO_FORMAT, "minecraft:geometry": [{
        "description": {"identifier": geo_ids[k], "texture_width": geo_w, "texture_height": geo_h,
                        "visible_bounds_width": 4, "visible_bounds_height": 5, "visible_bounds_offset": [0, 2, 0]},
        "bones": ch} for k, ch in enumerate(chunks)]}
    chunk_names = [{b["name"] for b in ch} for ch in chunks]

    # drive animation
    anim_bones = {}
    for p in parts + dups:
        i = p.bone[1:]
        d = (-p.rot[0], -p.rot[1], p.rot[2])
        anim_bones[p.bone] = {
            "rotation": [f"v.fb{i}r{a} ?? {molang_num(d[k])}" for k, a in enumerate("xyz")],
            "position": [f"v.fb{i}p{a} ?? 0" for a in "xyz"],
            "scale": [f"v.fb{i}s{a} ?? 1" for a in "xyz"],
        }
    for bname, (p, sid) in fp_bones.items():
        if sid is None:
            anim_bones[bname] = anim_bones[p.bone]
    animation = {"format_version": "1.8.0", "animations": {"animation.figura.drive": {"loop": True, "bones": anim_bones}}}

    # held items follow Figura's RightItemPivot / LeftItemPivot parts: Bedrock draws held items on the skeleton bones
    # rightItem / leftItem. In third person this animation (played after the vanilla ones, override_previous_animation)
    # puts the vanilla skeleton back at rest and drives the item bones from v.fbir* / v.fbil* (model space, solved by
    # the runtime). First person keeps Bedrock's own arms and items.
    item_pivots = {}
    for side, pt, bone in (("right", "RightItemPivot", "rightItem"), ("left", "LeftItemPivot", "leftItem")):
        if any(p.pt == pt for p in parts):
            bpiv = next(pv for n, _, pv in SKELETON if n == bone)
            item_pivots[side] = {"bone": bone, "pivot": [num(-bpiv[0]), num(bpiv[1]), num(bpiv[2])]}
    if item_pivots:
        ib = {n: {"rotation": [0, 0, 0], "position": [0, 0, 0]} for n in ("root", "waist", "body", "rightArm", "leftArm")}
        for side, d in item_pivots.items():
            v = "fbir" if side == "right" else "fbil"
            ib[d["bone"]] = {"rotation": [f"v.{v}r{a} ?? 0" for a in "xyz"], "position": [f"v.{v}p{a} ?? 0" for a in "xyz"],
                             "scale": [f"v.{v}s{a} ?? 1" for a in "xyz"]}
        animation["animations"]["animation.figura.items"] = {"loop": True, "override_previous_animation": True, "bones": ib}

    # render controllers: one primary + one secondary per d/e texture file
    # dynamic texture slots: textures scripts create (textures:newTexture / copy / read / fromVanilla) and show with
    # setPrimaryTexture("CUSTOM", tex) are uploaded at runtime (native TextureGroup upload) over a placeholder PNG the
    # pack ships, so the render controllers already hold a handle to that path
    import re as _re
    if _re.search(r"textures\s*[:.]\s*(?:newTexture|copy|read|fromVanilla)\s*\(", script_text):
        blank = blank_png(DYN_SIZE, DYN_SIZE)
        for k in range(DYN_SLOTS):
            files.append({"i": len(files), "figuraName": f"__dyn{k}", "set": -1, "type": "d", "file": None,
                          "w": DYN_SIZE, "h": DYN_SIZE, "_data": blank, "dyn": k})
    rc_files = [f for f in files if f["type"] in ("d", "e") and f["_data"] is not None]
    controllers = {}

    def vis_expr(p: Part):
        return f"(v.fb{p.i}v ?? {1 if p.vis_eff else 0})"

    # which texture files each chunk's parts use by default; a file nobody uses by default (swap targets like
    # Blue_e, runtime slots) may be put on any part by a script, so it gets a controller in every chunk
    chunk_files = [set() for _ in chunk_names]
    default_files = set()
    for bname, (p, sid) in geo_bones.items():
        if sid is None or not (0 <= sid < len(sets)):
            continue
        for key in ("d", "e"):
            if sets[sid][key] is not None:
                default_files.add(sets[sid][key])
                for k, c in enumerate(chunk_names):
                    if bname in c:
                        chunk_files[k].add(sets[sid][key])
    for f, (k, cnames) in ((f, kc) for f in rc_files for kc in enumerate(chunk_names)):
        if f["i"] in default_files and f["i"] not in chunk_files[k]:
            continue
        for kind, var, mat, key in kinds_for(f):
            pv = [{"*": False}]
            for bname, (p, sid) in geo_bones.items():
                if bname not in cnames:
                    continue
                if sid is None:
                    # bone without geometry (group, or a split part's own bone): only matters if Bedrock's
                    # part_visibility were hierarchical; keep it on with the part's visibility so it is safe both ways
                    pv.append({bname: vis_expr(p)})
                    continue
                if p.i in uv_parts:
                    continue
                own = sets[sid][key] if 0 <= sid < len(sets) else None
                dflt = 1 if own == f["i"] else 0
                pv.append({bname: f"{vis_expr(p)} && ((v.fb{p.i}{var} ?? -1) < 0 ? {dflt} : (v.fb{p.i}{var} == {f['i']}))"})
            if len(pv) == 1:
                continue
            controllers[f"controller.render.player.figura_{kind}{f['i']}" + (f"c{k}" if k else "")] = {
                "geometry": f"Geometry.figura{k or ''}", "materials": [{"*": mat}], "textures": [f"Texture.fbt{f['i']}"],
                "part_visibility": pv}

    # UV-animated parts: one controller per (part, texture file it can show), restricted to that part's bones
    files_by_i = {f["i"]: f for f in rc_files}
    for bname, (p, sid) in geo_bones.items():
        if sid is None or p.i not in uv_parts:
            continue
        k = next(n for n, c in enumerate(chunk_names) if bname in c)
        uv = {"offset": [f"v.fb{p.i}uu ?? 0", f"v.fb{p.i}uv ?? 0"], "scale": [f"v.fb{p.i}usu ?? 1", f"v.fb{p.i}usv ?? 1"]}
        for kind, var, key in (("p", "t", "d"), ("s", "e", "e")):
            own = sets[sid][key] if 0 <= sid < len(sets) else None
            if own not in files_by_i:
                continue
            f = files_by_i[own]
            # uv_anim only works on materials built with USE_UV_ANIM (materials/entity.material, verified in game)
            mat = {"Material.fbd": "Material.fbu", "Material.fbb": "Material.fbub"}[primary_material(f)] if kind == "p" else "Material.fbue"
            controllers[f"controller.render.player.figura_uv_{bname}_{kind}{f['i']}"] = {
                "geometry": f"Geometry.figura{k or ''}", "materials": [{"*": mat}], "textures": [f"Texture.fbt{f['i']}"],
                "uv_anim": uv,
                "part_visibility": [{"*": False}, {bname: f"{vis_expr(p)} && ((v.fb{p.i}{var} ?? -1) < 0 ? 1 : (v.fb{p.i}{var} == {f['i']}))"}]}

    # held items are positioned from the vanilla geometry's rightItem/leftItem, and Bedrock only poses a geometry some
    # active render controller uses (verified in game): keep Geometry.default in an all-hidden controller
    if item_pivots:
        controllers["controller.render.player.figura_itembones"] = {
            "geometry": "Geometry.default", "materials": [{"*": "Material.default"}], "textures": ["Texture.default"],
            "part_visibility": [{"*": False}]}

    # first-person controllers: same rules over the f-bones, plus Bedrock's own empty-hand conditions
    FP_RIGHT = "(query.get_equipped_item_name(0, 1) == '' || query.get_equipped_item_name(0, 1) == 'filled_map')"
    FP_LEFT = ("((query.get_equipped_item_name(0, 1) == 'filled_map' && query.get_equipped_item_name('off_hand') != 'shield') || "
               "(query.get_equipped_item_name('off_hand') == 'filled_map' && !query.item_is_charged) || "
               "(!query.item_is_charged && (variable.item_use_normalized > 0 && variable.item_use_normalized < 1.0)))")
    root_of = {}
    for rp_, van in fp_roots:
        stack = [rp_]
        while stack:
            q = stack.pop()
            root_of[q.i] = van
            stack.extend(q.children)
    fp_controllers = {}
    if fp_bones:
        for f, (k, cnames) in ((f, kc) for f in rc_files for kc in enumerate(chunk_names)):
            for kind, var, mat, key in kinds_for(f):
                pv = [{"*": False}]
                for bname, (p, sid) in fp_bones.items():
                    if bname not in cnames:
                        continue
                    hand = FP_RIGHT if root_of.get(p.i) == "rightArm" else FP_LEFT
                    if sid is None:
                        pv.append({bname: f"{vis_expr(p)} && {hand}"})
                        continue
                    own = sets[sid][key] if 0 <= sid < len(sets) else None
                    dflt = 1 if own == f["i"] else 0
                    pv.append({bname: f"{vis_expr(p)} && {hand} && ((v.fb{p.i}{var} ?? -1) < 0 ? {dflt} : (v.fb{p.i}{var} == {f['i']}))"})
                if len(pv) == 1:
                    continue
                fp_controllers[f"controller.render.player.figura_fp_{kind}{f['i']}" + (f"c{k}" if k else "")] = {
                    "geometry": f"Geometry.figura{k or ''}", "materials": [{"*": mat}], "textures": [f"Texture.fbt{f['i']}"],
                    "part_visibility": pv}
        controllers.update(fp_controllers)

    # sounds
    sound_map, sound_defs, sound_files = {}, {}, {}
    for sname, ref in (nbt.get("sounds") or {}).items():
        low = sname.lower()
        ev = f"figura.{slug}.{low}"
        fname = "".join(c if c.isalnum() or c in "_-" else "_" for c in low)
        sound_map[sname] = ev
        sound_defs[ev] = {"category": "player", "sounds": [{"name": f"sounds/figura/{slug}/{fname}"}]}
        sound_files[f"sounds/figura/{slug}/{fname}.ogg"] = ref.data

    # 3. resource pack
    rp = out / "rp"
    for d in ("models/entity", f"textures/figura/{slug}", "entity", "render_controllers", "animations", "sounds"):
        (rp / d).mkdir(parents=True, exist_ok=True)
    (rp / f"models/entity/figura_{slug}.geo.json").write_text(json.dumps(geometry, indent=1))
    (rp / "animations/figura_drive.animation.json").write_text(json.dumps(animation, indent=1))
    (rp / "render_controllers/figura_player.render_controllers.json").write_text(
        json.dumps({"format_version": "1.10.0", "render_controllers": controllers}, indent=1))
    for f in rc_files:
        f["bedrockKey"] = f"fbt{f['i']}"
        f["rpTexture"] = f"textures/figura/{slug}/t{f['i']}"
        (rp / (f["rpTexture"] + ".png")).write_bytes(rgba_png(f["_data"]))
    for rel, data in sound_files.items():
        (rp / rel).parent.mkdir(parents=True, exist_ok=True)
        (rp / rel).write_bytes(data)
    if sound_defs:
        (rp / "sounds/sound_definitions.json").write_text(
            json.dumps({"format_version": "1.14.0", "sound_definitions": sound_defs}, indent=1))

    ent = json.loads((vanilla_rp / "entity/player.entity.json").read_text(encoding="utf-8"))
    desc = ent["minecraft:client_entity"]["description"]
    for k, gid in enumerate(geo_ids):
        desc["geometry"][f"figura{k or ''}"] = gid
    desc["materials"]["fbd"] = "entity_alphatest"    # cutout: what nearly every Figura texture is
    desc["materials"]["fbb"] = "entity_alphablend"   # textures with partial alpha (blush, glass...); depth write is off
    desc["materials"]["fbe"] = "entity_emissive_alpha"
    if uv_parts:
        desc["materials"]["fbu"] = "figura_uv"
        desc["materials"]["fbub"] = "figura_uv_blend"
        desc["materials"]["fbue"] = "figura_uv_emissive"
        (rp / "materials").mkdir(parents=True, exist_ok=True)
        (rp / "materials/entity.material").write_text(json.dumps({"materials": {
            "version": "1.0.0",
            "figura_uv:entity_alphatest": {"+defines": ["USE_UV_ANIM"]},
            "figura_uv_blend:entity_alphablend": {"+defines": ["USE_UV_ANIM"]},
            "figura_uv_emissive:entity_emissive_alpha": {"+defines": ["USE_UV_ANIM"]}}}, indent=1))
    for f in rc_files:
        desc["textures"][f["bedrockKey"]] = f["rpTexture"]
    desc["animations"]["figura_drive"] = "animation.figura.drive"
    desc["scripts"].setdefault("animate", []).append("figura_drive")
    if item_pivots:
        desc["animations"]["figura_items"] = "animation.figura.items"
        desc["scripts"]["animate"].append({"figura_items": "!variable.is_first_person"})
    gated = []
    for r in desc.get("render_controllers", []):  # vanilla stays available, hidden unless v.fbvan
        if isinstance(r, str):
            gated.append({r: "(v.fbvan ?? 0)"})
        else:
            k, cond = next(iter(r.items()))
            gated.append({k: f"({cond}) && (v.fbvan ?? 0)"})
    for k in controllers:
        gated.append({k: "variable.is_first_person" if k in fp_controllers else "!variable.is_first_person"})
    desc["render_controllers"] = gated
    (rp / "entity/player.entity.json").write_text(json.dumps(ent, indent=1))

    manifest = {"format_version": 2,
                "header": {"name": f"FiguraBridge: {meta_name}",
                           "description": "Figura avatar as the player model (FiguraBridge, private use)",
                           "uuid": str(uuid.uuid5(PACK_NS, slug + ":header")), "version": [0, 2, 0],
                           "min_engine_version": [1, 21, 0]},
                "modules": [{"type": "resources", "uuid": str(uuid.uuid5(PACK_NS, slug + ":module")),
                             "version": [0, 2, 0]}]}
    (rp / "manifest.json").write_text(json.dumps(manifest, indent=1))
    lic = avatar_dir / "LICENSE"
    if lic.is_file():
        shutil.copy(lic, rp / "AVATAR_LICENSE.txt")

    rig = {
        "avatar": meta_name, "slug": slug, "figuraVersion": fn.FIGURA_VERSION,
        "nbt": "avatar/figura_avatar.json",
        "geometry": {"identifier": geo_ids[0], "chunks": geo_ids, "textureWidth": geo_w, "textureHeight": geo_h},
        "skeleton": [n for n, _, _ in SKELETON],
        "parts": [],
        "textureFiles": [{k: v for k, v in f.items() if k != "_data"} for f in files],
        "textureSets": sets,
        "sounds": sound_map,
        "vars": VARS_DOC,
        "warnings": compiled.warnings,
    }
    for p in parts:
        e = {"i": p.i, "parent": p.parent.i if p.parent else -1, "path": p.path, "name": p.name, "type": p.type,
             "piv": [num(c) for c in p.piv], "rot": [num(c) for c in p.rot], "pt": p.pt, "vsb": p.vsb,
             "vsbEffective": p.vis_eff, "bone": p.bone}
        if getattr(p, "subs", None):
            if len(p.subs) > 1 or p.subs[0]["bone"] != p.bone:
                e["subs"] = [{**s, "emissive": sets[s["set"]]["e"] is not None} for s in p.subs]
            else:
                e["set"] = p.subs[0]["set"]
        rig["parts"].append(e)
    rig["dups"] = [{"i": d.i, "source": d.source.i} for d in dups]
    rig["uvParts"] = sorted(uv_parts)
    rig["itemPivots"] = item_pivots
    (out / "rig.json").write_text(json.dumps(rig, indent=1), encoding="utf-8")
    return {"out": str(out), "parts": len(parts), "bones": len(bones), "textureFiles": len(files),
            "textureSets": len(sets), "renderControllers": len(controllers), "sounds": len(sound_map),
            "animations": len(nbt.get("animations", [])), "scripts": len(nbt.get("scripts", {})),
            "warnings": compiled.warnings}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("avatar")
    ap.add_argument("out")
    ap.add_argument("--vanilla", required=True, help="bedrock-samples resource_pack folder")
    ap.add_argument("--uuid", default="00000000-0000-0000-0000-000000000000",
                    help="owner UUID written to metadata.uuid (Figura uses the player's UUID)")
    a = ap.parse_args(argv)
    r = build(Path(a.avatar), Path(a.out).resolve(), Path(a.vanilla).resolve(), a.uuid)
    print(json.dumps(r, indent=1))


if __name__ == "__main__":
    main()
