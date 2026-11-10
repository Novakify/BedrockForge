"""ModelCore: Blockbench / Figura .bbmodel -> Rig (FiguraBridge F1).

Reads the same files Figura 0.1.6 reads (BlockbenchV4Model + BlockbenchV5Model): Blockbench 4.x keeps groups
inline in the outliner, Blockbench 5 keeps them in a top-level "groups" list referenced by uuid. Both load here.

Output is ModelCore's Bedrock convention (pixels, degrees, Bedrock axis signs), using the same mapping as
Blockbench's own Bedrock exporter, so a Figura avatar and a GeckoBridge mob land in one rig format:
    bone pivot    = [-origin.x, origin.y, origin.z]       bone/cube rotation = [-r.x, -r.y, r.z]
    cube origin   = [-to.x, from.y, from.z]               cube size          = to - from   (inflate kept apart)
    face uv       = [u1, v1] + uv_size [u2-u1, v2-v1]     up/down faces flipped: [u2, v2] + [u1-u2, v1-v2]
    mesh          = Bedrock poly_mesh in model space (vertices rotated by the mesh rotation, + origin, x negated)

Figura-only data is kept on the side, not mixed into the geometry:
    bone.extra["figura_parent"]   ParentType the group name selects (Head, RightArm, Camera, Hud ...) - see
                                  figura_parent_type(); FiguraBridge uses it to follow / hide vanilla parts.
    rig.source_meta              textures, animations (raw, F2 reads them), resolution, format, source name.

    from ModelCore.bbmodel import load_bbmodel
    rig = load_bbmodel(json.load(open("avatar/model.bbmodel")), name="model")
"""
from __future__ import annotations
import math
from .rig import Bone, Cube, Rig

# Figura 0.1.6 org.figuramc.figura.model.ParentType, in enum order: (name, aliases...).
# ParentType.get(groupName): first entry whose name OR any alias is a PREFIX of the group name (String.startsWith).
# Order matters and is copied as-is, quirks included ("HeadsUpDisplay..." resolves to Head, like in Figura).
FIGURA_PARENT_TYPES = (
    ("None", "NONE"), ("Model", "MODEL"), ("Head", "HEAD"), ("Body", "BODY"),
    ("LeftArm", "LEFT_ARM"), ("RightArm", "RIGHT_ARM"),
    ("LeftLeggingPivot", "LEFT_LEGGING_PIVOT"), ("RightLeggingPivot", "RIGHT_LEGGING_PIVOT"),
    ("LeftLeg", "LEFT_LEG"), ("RightLeg", "RIGHT_LEG"),
    ("LeftElytraPivot", "LEFT_ELYTRA_PIVOT", "LeftWingPivot", "LEFT_WING_PIVOT"),
    ("RightElytraPivot", "RIGHT_ELYTRA_PIVOT", "RightWingPivot", "RIGHT_WING_PIVOT"),
    ("LeftElytra", "LEFT_ELYTRA", "LeftElytron", "LEFT_ELYTRON"),
    ("RightElytra", "RIGHT_ELYTRA", "RightElytron", "RIGHT_ELYTRON"),
    ("Cape", "CAPE"), ("World", "WORLD"), ("Hud", "HUD", "HeadsUpDisplay"),
    ("Gui", "GUI", "GraphicalUserInterface", "JraficalUserInterface"),
    ("Camera", "CAMERA"), ("Billboard", "BILLBOARD"), ("Skull", "SKULL"), ("Portrait", "PORTRAIT"),
    ("Arrow", "ARROW"), ("Trident", "TRIDENT"), ("Item", "ITEM"),
    ("LeftItemPivot", "LEFT_ITEM_PIVOT"), ("RightItemPivot", "RIGHT_ITEM_PIVOT"),
    ("LeftSpyglassPivot", "LEFT_SPYGLASS_PIVOT"), ("RightSpyglassPivot", "RIGHT_SPYGLASS_PIVOT"),
    ("LeftParrotPivot", "LEFT_PARROT_PIVOT"), ("RightParrotPivot", "RIGHT_PARROT_PIVOT"),
    ("HelmetItemPivot", "HELMET_ITEM_PIVOT"), ("HelmetPivot", "HELMET_PIVOT"),
    ("ChestplatePivot", "CHESTPLATE_PIVOT"), ("ChestplateBodyPivot", "CHESTPLATE_BODY_PIVOT"),
    ("LeftShoulderPivot", "LEFT_SHOULDER_PIVOT"), ("RightShoulderPivot", "RIGHT_SHOULDER_PIVOT"),
    ("LeggingsPivot", "LEGGINGS_PIVOT"), ("BeltPivot", "BELT_PIVOT"),
    ("LeftBootPivot", "LEFT_BOOT_PIVOT"), ("RightBootPivot", "RIGHT_BOOT_PIVOT"),
)

# Figura ParentType -> Bedrock player geometry bone (geometry.humanoid.custom). FiguraBridge F1 attaches here.
BEDROCK_PLAYER_BONE = {"Head": "head", "Body": "body", "LeftArm": "leftArm", "RightArm": "rightArm",
                       "LeftLeg": "leftLeg", "RightLeg": "rightLeg", "Cape": "cape"}

FACES = ("north", "east", "south", "west", "up", "down")


def figura_parent_type(group_name: str | None) -> str:
    if not group_name: return "None"
    for entry in FIGURA_PARENT_TYPES:
        if any(group_name.startswith(k) for k in entry): return entry[0]
    return "None"


def _v3(x, d=0.0):
    x = list(x) if x is not None else [d, d, d]
    return [float(v) for v in (x + [d, d, d])[:3]]


def _bone_rot(r):
    r = _v3(r)
    return None if r == [0.0, 0.0, 0.0] else [-r[0], -r[1], r[2]]


def _clean(v):  # -0.0 -> 0.0, whole floats stay floats (JSON-safe, stable tests)
    return [0.0 if abs(x) < 1e-12 else round(x, 6) for x in v]


def _rot_matrix(r):
    """Blockbench element rotation (degrees, XYZ euler, Three.js order 'ZYX' applied as Rz*Ry*Rx)."""
    x, y, z = (math.radians(a) for a in _v3(r))
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    ry = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rz = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))
    mul = lambda a, b: tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))
    return mul(mul(rz, ry), rx)


def _apply(m, v): return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]


def _cube(el, box_uv_default, tex_w, tex_h, uv_scale):
    frm, to = _v3(el.get("from")), _v3(el.get("to"))
    origin = _v3(el.get("origin"))
    c = Cube(origin=_clean([-to[0], frm[1], frm[2]]), size=_clean([t - f for f, t in zip(frm, to)]))
    if el.get("inflate"): c.inflate = float(el["inflate"])
    rot = _bone_rot(el.get("rotation"))
    if rot:
        c.rotation = _clean(rot)
        c.pivot = _clean([-origin[0], origin[1], origin[2]])
    box = el.get("box_uv", box_uv_default)
    if box:
        c.uv = [float(v) * s for v, s in zip(el.get("uv_offset", [0, 0]), uv_scale)]
        if el.get("mirror_uv"): c.mirror = True
    else:
        faces = {}
        for name in FACES:
            f = (el.get("faces") or {}).get(name)
            if not f or f.get("texture") is None or f.get("enabled") is False: continue
            u1, v1, u2, v2 = (float(a) * uv_scale[i % 2] for i, a in enumerate(f.get("uv", [0, 0, 0, 0])))
            if name in ("up", "down"):
                fo = {"uv": [u2, v2], "uv_size": [u1 - u2, v1 - v2]}
            else:
                fo = {"uv": [u1, v1], "uv_size": [u2 - u1, v2 - v1]}
            if f.get("rotation"): fo["uv_rotation"] = int(f["rotation"])
            fo = {k: (_clean(v) if isinstance(v, list) else v) for k, v in fo.items()}
            faces[name] = fo
        c.uv = faces
    c.extra["bb_uuid"] = el.get("uuid")
    if el.get("name"): c.extra["bb_name"] = el["name"]
    if el.get("visibility") is False: c.extra["bb_visible"] = False
    return c


def _mesh(el, tex_w, tex_h, uv_scale):
    """Blockbench mesh -> Bedrock poly_mesh (model space, normalized uvs), one poly per face (tri or quad)."""
    m, org = _rot_matrix(el.get("rotation")), _v3(el.get("origin"))
    verts = el.get("vertices", {})
    world = {k: (lambda p: [-p[0], p[1], p[2]])([a + b for a, b in zip(_apply(m, _v3(v)), org)]) for k, v in verts.items()}
    pos_i, positions, normals, uvs, polys = {}, [], [], [], []
    for k, p in world.items():
        pos_i[k] = len(positions); positions.append(_clean(p))
    faces = []  # (face, ordered vertex keys, raw area-weighted normal)
    for fk, f in (el.get("faces") or {}).items():
        vk = list(f.get("vertices", []))
        if len(vk) < 3 or f.get("texture") is None: continue
        if len(vk) == 4: vk = _sort_quad(vk, world)
        a, b, c = (world[x] for x in vk[:3])
        faces.append((f, vk, _cross([b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)])))
    # [BF] opt-in smooth shading for organic meshes (imported models): per-corner normals averaged over the faces that
    # meet at that position within bf_smooth_angle degrees (auto-smooth). Unset = one flat normal per face, exactly as
    # Blockbench exports it, so existing conversions are unchanged.
    smooth = el.get("bf_smooth_angle")
    if smooth:
        cos_t = math.cos(math.radians(float(smooth)))
        unit = [(lambda n, ln: [x / ln for x in n])(n, math.sqrt(sum(x * x for x in n)) or 1.0) for _, _, n in faces]
        q = lambda p: tuple(round(x, 4) for x in p)
        at = {}
        for fi, (_, vk, _) in enumerate(faces):
            for x in vk: at.setdefault(q(world[x]), []).append(fi)
        n_cache = {}
    for fi, (f, vk, n) in enumerate(faces):
        if not smooth:
            ln = math.sqrt(sum(x * x for x in n)) or 1.0
            n_i = len(normals); normals.append(_clean([x / ln for x in n]))
        poly = []
        for x in vk:
            if smooth:
                s = [0.0, 0.0, 0.0]
                for fj in at[q(world[x])]:
                    if sum(p * r for p, r in zip(unit[fi], unit[fj])) >= cos_t:
                        s = [s[i] + faces[fj][2][i] for i in range(3)]  # area-weighted
                ln = math.sqrt(sum(v * v for v in s)) or 1.0
                key = tuple(_clean([v / ln for v in s]))
                n_i = n_cache.get(key)
                if n_i is None: n_i = n_cache[key] = len(normals); normals.append(list(key))
            u, v = (float(t) * s for t, s in zip(f.get("uv", {}).get(x, [0, 0]), uv_scale))
            uv_i = len(uvs); uvs.append(_clean([u / tex_w, 1 - v / tex_h]))
            poly.append([pos_i[x], n_i, uv_i])
        if len(poly) == 3: poly.append(list(poly[2]))  # Bedrock "quad_list": triangles repeat the last vertex
        polys.append(poly)
    return {"normalized_uvs": True, "positions": positions, "normals": normals, "uvs": uvs, "polys": polys}


def _cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _sort_quad(vk, world):
    """Blockbench MeshFace.getSortedVertices: order the 4 vertices around the face so the quad isn't twisted.
    The x flip mirrors the mesh, so winding is reversed afterwards to keep faces pointing outward."""
    a, b, c, d = (world[x] for x in vk)
    def _side(p, q, r, s):  # do r and s lie on the same side of edge p-q?
        e = [q[i] - p[i] for i in range(3)]
        n1 = _cross(e, [r[i] - p[i] for i in range(3)]); n2 = _cross(e, [s[i] - p[i] for i in range(3)])
        return sum(x * y for x, y in zip(n1, n2)) > 0
    if not _side(a, b, c, d): order = [vk[0], vk[2], vk[1], vk[3]]
    elif not _side(b, c, d, a): order = [vk[0], vk[1], vk[3], vk[2]]
    else: order = list(vk)
    return order[::-1]


def load_bbmodel(data: dict, name: str = "model") -> Rig:
    meta = data.get("meta", {})
    res = data.get("resolution", {})
    tex_w, tex_h = int(res.get("width", 64)), int(res.get("height", 64))
    box_default = bool(meta.get("box_uv", False))
    # Blockbench < 4 stored per-face UVs on a 16x16 grid; 4.x+ stores texels of the project resolution.
    fv = str(meta.get("format_version", "4.0"))
    old = fv.split(".")[0].isdigit() and int(fv.split(".")[0]) < 4
    uv_scale = (tex_w / 16.0, tex_h / 16.0) if old and not box_default else (1.0, 1.0)

    elements = {e.get("uuid"): e for e in data.get("elements", [])}
    groups_v5 = {g.get("uuid"): g for g in data.get("groups", []) if isinstance(g, dict)}

    rig = Rig(identifier=f"geometry.figura.{name}", texture_width=tex_w, texture_height=tex_h, source_format="bbmodel")
    vb = data.get("visible_box")  # [width, height, y] in blocks, written by Blockbench's Bedrock exporter as visible_bounds_*
    if isinstance(vb, list) and len(vb) == 3:
        rig.description_extra.update({"visible_bounds_width": vb[0], "visible_bounds_height": vb[1],
                                      "visible_bounds_offset": [0, vb[2], 0]})
    used = set()

    def unique(n):
        base, i = n or "group", 1
        while n in used or not n: n = f"{base}_{i}"; i += 1
        used.add(n); return n

    def add_group(g, parent):
        if isinstance(g, str): g = {**groups_v5.get(g, {}), "uuid": g}   # v5: outliner holds {uuid, children}
        elif "uuid" in g and g["uuid"] in groups_v5 and "origin" not in g:
            g = {**groups_v5[g["uuid"]], **{k: v for k, v in g.items() if k == "children"}}
        bname = unique(g.get("name", "group"))
        o = _v3(g.get("origin"))
        b = Bone(name=bname, parent=parent, pivot=_clean([-o[0], o[1], o[2]]))
        r = _bone_rot(g.get("rotation"))
        if r: b.rotation = _clean(r)
        if g.get("visibility") is False: b.extra["bb_visible"] = False
        b.extra["bb_uuid"] = g.get("uuid")
        b.extra["figura_parent"] = figura_parent_type(g.get("name"))
        rig.bones.append(b)
        meshes = []
        for ch in g.get("children", []):
            if isinstance(ch, dict) and ("children" in ch or ch.get("uuid") in groups_v5):
                add_group(ch, bname); continue
            uid = ch if isinstance(ch, str) else ch.get("uuid")
            if uid in groups_v5: add_group({"uuid": uid, "children": (ch.get("children") if isinstance(ch, dict) else None) or groups_v5[uid].get("children", [])}, bname); continue
            el = elements.get(uid)
            if not el or el.get("export") is False: continue
            t = el.get("type", "cube")
            if t == "cube": b.cubes.append(_cube(el, box_default, tex_w, tex_h, uv_scale))
            elif t == "mesh": meshes.append(el)
            elif t in ("locator", "null_object"):
                p = _v3(el.get("position", el.get("from")))
                b.locators = {**(b.locators or {}), unique(el.get("name", "locator")): _clean([-p[0], p[1], p[2]])}
            # texture_mesh / text / unknown: kept out, like Figura's UnknownElement
        for i, el in enumerate(meshes):  # one Bedrock bone holds one poly_mesh -> extra meshes get child bones
            pm = _mesh(el, tex_w, tex_h, uv_scale)
            if i == 0 and b.poly_mesh is None: b.poly_mesh = pm
            else:
                mb = Bone(name=unique(f"{bname}_{el.get('name', 'mesh')}"), parent=bname, pivot=list(b.pivot), poly_mesh=pm)
                mb.extra["figura_parent"] = "None"; rig.bones.append(mb)

    outliner = data.get("outliner", [])
    loose = [x for x in outliner if isinstance(x, str) and x not in groups_v5]
    for item in outliner:
        if isinstance(item, str) and item not in groups_v5: continue
        add_group(item, None)
    if loose:  # elements at the root of the outliner: Figura puts them on the model root
        add_group({"name": "root_elements", "origin": [0, 0, 0], "children": loose}, None)

    rig.source_meta = {
        "name": name, "format_version": fv, "model_format": meta.get("model_format"), "box_uv": box_default,
        "textures": [{k: t.get(k) for k in ("name", "id", "uuid", "width", "height", "uv_width", "uv_height", "render_mode", "relative_path")
                      if t.get(k) is not None} for t in data.get("textures", [])],
        "animations": data.get("animations", []),
    }
    return rig
