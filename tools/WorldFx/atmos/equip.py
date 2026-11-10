"""World FX equipment: box-model weapons and tools with per-tier finishes (by Novak).

A model is a list of parts (box in model units = 1/16 block, a material, a role). Faces are UV-mapped at D texels
per unit onto one atlas per item, each face painted by its material for its role and orientation. The atmosphere
module supplies the palettes; shapes are shared by every atmosphere (same formula, different finish).
"""
from __future__ import annotations

import numpy as np

from tex import ramp, rng, value_noise

D = 2  # texels per model unit
FACE_DIMS = {"north": (0, 1), "south": (0, 1), "east": (2, 1), "west": (2, 1), "up": (0, 2), "down": (0, 2)}


class Part:
    def __init__(self, name, origin, size, mat, role="body", seed=0):
        self.name, self.origin, self.size, self.mat, self.role, self.seed = name, list(origin), list(size), mat, role, seed


# ------------------------------------------------------------------ materials: face painters
def _rampf(f, pal):
    n = len(pal)
    i = np.clip((np.clip(f, 0, 0.9999) * n).astype(int), 0, n - 1)
    return np.array(pal, np.uint8)[i]


def paint(mat, role, face, w, h, seed, pal):
    """w x h texels (u across, v down). Returns RGBA uint8."""
    r = rng(seed)
    u, v = np.mgrid[0:h, 0:w][1].astype(float), np.mgrid[0:h, 0:w][0].astype(float)
    long_axis_v = h >= w
    f = np.full((h, w), 0.5)
    if mat in ("steel", "silver", "netherite", "gold"):
        # brushed metal: streaks along the long axis; bevel: the outermost texel column of a blade face is the
        # sharpened edge (bright), the next one in a touch darker (the bevel line)
        if long_axis_v:
            streak = value_noise(max(w, h), 4, seed, 1)[:1, :w].repeat(h, 0)
        else:
            streak = value_noise(max(w, h), 4, seed, 1)[:h, :1].repeat(w, 1)
        f = 0.48 + (streak - 0.5) * 0.25
        if mat == "netherite":
            ham = value_noise(max(w, h) + 4, 3, seed + 9, 2)[:h, :w]
            f = 0.42 + (ham - 0.5) * 0.35
        if role == "blade" and face in ("north", "south"):
            f = np.where((u == 0) | (u == w - 1), 0.93, f)
            f = np.where(((u == 1) | (u == w - 2)) & (w > 4), f - 0.16, f)
            if mat in ("steel", "silver") and w >= 6:
                mid = (u >= w // 2 - 1) & (u <= w // 2) & (v > 1) & (v < h - 4)
                f = np.where(mid, f - 0.14, f)          # the fuller: a shallow groove down the middle
        if role == "blade" and face in ("east", "west"):
            f = np.full((h, w), 0.90)                    # looking at the edge: it is the bright bit
        if role == "tip":
            f = np.where((u == 0) | (u == w - 1) | (v == 0), 0.93, f)
        if role == "edge":
            f = np.full((h, w), 0.93)
        if role == "head":
            f = np.where((v == 0), f + 0.25, f)
            f = np.where((v == h - 1), f - 0.18, f)
        if role in ("guard", "pommel"):
            f = np.where((v == 0), f + 0.25, f)
            f = np.where((v == h - 1), f - 0.22, f)
    elif mat == "wood":
        if long_axis_v:
            g = np.sin(u * 1.7 + (value_noise(max(w, h), 2, seed, 1)[:h, :w] - 0.5) * 4) * 0.5 + 0.5
        else:
            g = np.sin(v * 1.7 + (value_noise(max(w, h), 2, seed, 1)[:h, :w] - 0.5) * 4) * 0.5 + 0.5
        f = 0.45 + (g - 0.5) * 0.3
        if role in ("blade", "tip") and face in ("north", "south"):
            f = np.where((u == 0) | (u == w - 1), f + 0.20, f)   # worn, rounded edges catch light
    elif mat == "flint":
        # knapped: flat conchoidal scallops, each a slightly different tone, crisp ridge lines between
        cells = max(2, (w * h) // 10)
        px, py = r.random(cells) * w, r.random(cells) * h
        d = np.stack([np.hypot(u + 0.5 - x, v + 0.5 - y) for x, y in zip(px, py)])
        o = np.sort(d, 0)
        idx = np.argmin(d, 0)
        f = r.uniform(0.30, 0.62, cells)[idx] + (o[0] / 4.0) * 0.12
        f = np.where((o[1] - o[0]) < 0.7, 0.18, f)
        if face in ("north", "south"):
            f = np.where((u == 0) | (u == w - 1), 0.86, f)
    elif mat == "leather":
        f = 0.42 + ((v.astype(int) % 2) * 0.18)               # wrapped grip: alternating turns
        f = np.where((u == 0), f - 0.10, f)
    elif mat == "cord":
        f = 0.40 + (((u + v).astype(int) % 3 == 0) * 0.30)   # lashing: diagonal turns
    elif mat == "crystal":
        # cut gem: two big planes split along the long axis, light one / dark one, a hot edge line, inner glints
        if face in ("north", "south"):
            f = np.where(u < w / 2, 0.52, 0.30)
            f = np.where((u == 0) | (u == w - 1), 0.95, f)
            f = np.where((np.abs(u - (w - 1) / 2) < 0.6), 0.70, f)
            gl = (r.random((h, w)) < 0.05) & (u > 0) & (u < w - 1)
            f = np.where(gl, 0.98, f)
        elif face in ("east", "west"):
            f = np.full((h, w), 0.80)
        else:
            f = np.full((h, w), 0.62)
        if role == "tip":
            f = np.where(v == 0, 0.98, f)
    elif mat == "gem":
        f = np.full((h, w), 0.6)
        f[0, :] = 0.95
        f[:, 0] = np.maximum(f[:, 0], 0.85)
        f[-1, :] = 0.3
    rgb = _rampf(f, pal)
    # World FX material id in alpha (Actor AlphaTest shader): 0.5..1 survives the alpha test
    mid = {"steel": 12, "silver": 12, "gold": 12, "netherite": 12, "crystal": 13, "gem": 13}.get(mat, 0)
    a = np.full((h, w), 255 - 4 * mid, np.uint8)
    return np.dstack([rgb, a])


# ------------------------------------------------------------------ atlas + geometry
def build(parts, palettes, ident, tex_w=64):
    faces = []
    for p in parts:
        sx, sy, sz = p.size
        dims = {"north": (sx, sy), "south": (sx, sy), "east": (sz, sy), "west": (sz, sy), "up": (sx, sz), "down": (sx, sz)}
        for f, (a, b) in dims.items():
            w, h = max(1, int(round(a * D))), max(1, int(round(b * D)))
            faces.append((p, f, w, h))
    # shelf pack, tallest first
    order = sorted(range(len(faces)), key=lambda i: -faces[i][3])
    x = y = shelf = 0
    pos = {}
    for i in order:
        p, f, w, h = faces[i]
        if x + w > tex_w:
            x, y, shelf = 0, y + shelf, 0
        pos[i] = (x, y)
        x += w
        shelf = max(shelf, h)
    tex_h = 1
    while tex_h < y + shelf:
        tex_h *= 2
    atlas = np.zeros((tex_h, tex_w, 4), np.uint8)
    uvs = {}
    for i, (p, f, w, h) in enumerate(faces):
        px, py = pos[i]
        atlas[py:py + h, px:px + w] = paint(p.mat, p.role, f, w, h, p.seed * 31 + i, palettes[p.mat])
        uvs.setdefault(id(p), {})[f] = (px, py, w, h)
    return atlas, uvs, tex_w, tex_h


# held placement: Microsoft's official custom-item sample frame (minecraft-samples custom_items wrench): the bound
# bone pivots at the hand (0.1, 19, -6), rotated 125 deg about x; works in first and third person with no animation.
HAND = (0.1, 19.0, -6.0)
HAND_ROT = (125.0, 0.0, 0.0)
SWAP = {"north": "east", "south": "west", "east": "north", "west": "south", "up": "up", "down": "down"}


def geometry(parts, uvs, tex_w, tex_h, ident, scale=1.0, rot=None):
    """our parts: blade along +y, flat facing +-z. In the held frame the flat must face +-x (the side the player
    sees), so x and z swap on export."""
    cubes = []
    for p in parts:
        ox, oy, oz = p.origin
        sx, sy, sz = p.size
        o = [HAND[0] + oz * scale, HAND[1] + oy * scale, HAND[2] + ox * scale]
        sz2 = [sz * scale, sy * scale, sx * scale]
        uv = {}
        for f, (x, y, w, h) in uvs[id(p)].items():
            uv[SWAP[f]] = {"uv": [x, y], "uv_size": [w, h]}
        cubes.append({"origin": o, "size": sz2, "uv": uv})
    bones = [{"name": "wfx_item", "pivot": list(HAND), "rotation": list(rot or HAND_ROT),
              "binding": "q.item_slot_to_bone_name(context.item_slot)", "cubes": cubes}]
    return {"format_version": "1.16.0", "minecraft:geometry": [{
        "description": {"identifier": ident, "texture_width": tex_w, "texture_height": tex_h,
                        "visible_bounds_width": 3, "visible_bounds_height": 4, "visible_bounds_offset": [0, 1, 0]},
        "bones": bones}]}


def geometry_trident(parts, uvs, tex_w, tex_h, ident, grip_y=24.0):
    """Mojang's trident frame: bone pivot (0, 24, 0) bound to the item slot, the item standing along +y, flat facing
    +-z; held by the trident's own first/third person transforms (animation.wfx.hold_*). grip_y = where the grip
    centre sits on that pole."""
    cubes = []
    for p in parts:
        ox, oy, oz = p.origin
        uv = {f: {"uv": [x, y], "uv_size": [w, h]} for f, (x, y, w, h) in uvs[id(p)].items()}
        cubes.append({"origin": [ox, oy + grip_y, oz], "size": list(p.size), "uv": uv})
    bones = [{"name": "wfx_item", "pivot": [0, 24, 0], "binding": "q.item_slot_to_bone_name(c.item_slot)", "cubes": cubes}]
    return {"format_version": "1.16.0", "minecraft:geometry": [{
        "description": {"identifier": ident, "texture_width": tex_w, "texture_height": tex_h,
                        "visible_bounds_width": 3, "visible_bounds_height": 4, "visible_bounds_offset": [0, 1, 0]},
        "bones": bones}]}


HOLD_ANIM = {"format_version": "1.10.0", "animations": {
    "animation.wfx.hold_first_person": {"loop": True, "bones": {"wfx_item": {
        "position": [-7.0, -3.0, -2.0], "rotation": [152.0, -9.0, -5.0], "scale": 0.55}}},   # chosen in game 2026-11-10
    "animation.wfx.hold_third_person": {"loop": True, "bones": {"wfx_item": {
        "position": [1.5, -2.5, -10.5], "rotation": [97.0, -1.5, -49.0]}}}}}


def preview_cubes(parts, uvs):
    out = []
    for p in parts:
        out.append({"origin": p.origin, "size": p.size, "uv": uvs[id(p)]})
    return out


# ------------------------------------------------------------------ shapes (shared by every atmosphere)
def sword(tier):
    """blade along +y, edge planes facing +-x, flat of the blade facing +-z. Grip centred on the origin."""
    P = []
    if tier == "wood":
        P += [Part("pommel", [-1, -4, -1], [2, 1.5, 2], "wood", "pommel"),
              Part("grip", [-0.75, -2.5, -0.75], [1.5, 5, 1.5], "leather", "grip"),
              Part("guard", [-2.5, 2.5, -1], [5, 1.5, 2], "wood", "guard"),
              Part("blade", [-1.5, 4, -0.5], [3, 10, 1], "wood", "blade"),
              Part("tip", [-1, 14, -0.5], [2, 1.5, 1], "wood", "tip"),
              Part("tip2", [-0.5, 15.5, -0.5], [1, 1, 1], "wood", "tip")]
    elif tier == "stone":
        P += [Part("pommel", [-1, -4, -1], [2, 1.5, 2], "wood", "pommel"),
              Part("grip", [-0.75, -2.5, -0.75], [1.5, 5, 1.5], "wood", "grip"),
              Part("lash", [-1.25, 1.5, -1], [2.5, 2.5, 2], "cord", "guard"),
              Part("b1", [-1.75, 4, -0.75], [3.5, 3.5, 1.5], "flint", "blade", 1),
              Part("b2", [-1.25, 7.5, -0.75], [3, 3.5, 1.5], "flint", "blade", 2),
              Part("b3", [-1.5, 11, -0.75], [2.5, 2.5, 1.5], "flint", "blade", 3),
              Part("tip", [-1, 13.5, -0.5], [1.5, 2, 1], "flint", "tip", 4)]
    elif tier == "iron":
        P += [Part("pommel", [-1, -4.5, -1], [2, 2, 2], "steel", "pommel"),
              Part("grip", [-0.75, -2.5, -0.75], [1.5, 5, 1.5], "leather", "grip"),
              Part("guard", [-3.5, 2.5, -1], [7, 1.5, 2], "steel", "guard"),
              Part("blade", [-1.5, 4, -0.5], [3, 11, 1], "steel", "blade"),
              Part("tip", [-1, 15, -0.5], [2, 1.5, 1], "steel", "tip"),
              Part("tip2", [-0.5, 16.5, -0.5], [1, 1, 1], "steel", "tip")]
    elif tier == "gold":
        P += [Part("pommel", [-1.25, -4.5, -1.25], [2.5, 2, 2.5], "gold", "pommel"),
              Part("gem", [-0.75, -4.75, -0.75], [1.5, 0.5, 1.5], "gem", "gem"),
              Part("grip", [-0.75, -2.5, -0.75], [1.5, 5, 1.5], "leather", "grip"),
              Part("guard", [-3, 2.5, -1], [6, 1.5, 2], "gold", "guard"),
              Part("curl_l", [-4, 3, -1], [1, 2.5, 2], "gold", "guard"),
              Part("curl_r", [3, 3, -1], [1, 2.5, 2], "gold", "guard"),
              Part("blade", [-1.75, 4, -0.5], [3.5, 10, 1], "gold", "blade"),
              Part("tip", [-1.25, 14, -0.5], [2.5, 1.5, 1], "gold", "tip"),
              Part("tip2", [-0.75, 15.5, -0.5], [1.5, 1, 1], "gold", "tip")]
    elif tier == "diamond":
        P += [Part("pommel", [-1, -4.5, -1], [2, 2, 2], "silver", "pommel"),
              Part("grip", [-0.75, -2.5, -0.75], [1.5, 5, 1.5], "leather", "grip"),
              Part("guard", [-3.5, 2.5, -1.25], [7, 1.5, 2.5], "silver", "guard"),
              Part("chip", [-0.75, 2.25, -1.5], [1.5, 2, 3], "crystal", "gem"),
              Part("blade", [-1.5, 4, -0.5], [3, 11.5, 1], "crystal", "blade"),
              Part("ridge", [-0.5, 4, -0.8], [1, 11, 1.6], "crystal", "blade", 7),
              Part("tip", [-1, 15.5, -0.5], [2, 1.5, 1], "crystal", "tip"),
              Part("tip2", [-0.5, 17, -0.5], [1, 1, 1], "crystal", "tip")]
    elif tier == "netherite":
        P += [Part("pommel", [-1.25, -4.5, -1.25], [2.5, 2, 2.5], "netherite", "pommel"),
              Part("grip", [-0.75, -2.5, -0.75], [1.5, 5, 1.5], "leather", "grip"),
              Part("guard", [-3.5, 2.5, -1.25], [7, 2, 2.5], "netherite", "guard"),
              Part("blade", [-2, 4.5, -0.6], [4, 10.5, 1.2], "netherite", "blade"),
              Part("tip", [-1.5, 15, -0.6], [3, 1.5, 1.2], "netherite", "tip"),
              Part("tip2", [-0.75, 16.5, -0.6], [1.5, 1, 1.2], "netherite", "tip")]
    return P


HEAD_MAT = {"wood": "wood", "stone": "flint", "iron": "steel", "gold": "gold", "diamond": "crystal", "netherite": "netherite"}


def pickaxe(tier):
    """haft along +y (grip at the origin), head across x at the top; the head arcs down at the tips"""
    m = HEAD_MAT[tier]
    hm = "wood" if tier != "netherite" else "netherite"
    P = [Part("haft", [-0.75, -5, -0.75], [1.5, 17, 1.5], "wood" if tier in ("wood", "stone") else hm, "grip"),
         Part("wrap", [-1, -5.5, -1], [2, 4, 2], "leather", "grip"),
         Part("collar", [-1.25, 10, -1.25], [2.5, 3, 2.5], m if tier != "stone" else "cord", "guard"),
         Part("head", [-4.5, 11, -1], [9, 2, 2], m, "head"),
         Part("arm_l", [-7, 10, -0.75], [2.5, 1.5, 1.5], m, "head"),
         Part("arm_r", [4.5, 10, -0.75], [2.5, 1.5, 1.5], m, "head"),
         Part("tip_l", [-8.5, 8.5, -0.5], [1.5, 2, 1], m, "tip"),
         Part("tip_r", [7, 8.5, -0.5], [1.5, 2, 1], m, "tip")]
    return P


def axe(tier):
    m = HEAD_MAT[tier]
    hm = "wood" if tier != "netherite" else "netherite"
    P = [Part("haft", [-0.75, -5, -0.75], [1.5, 17, 1.5], "wood" if tier in ("wood", "stone") else hm, "grip"),
         Part("wrap", [-1, -5.5, -1], [2, 4, 2], "leather", "grip"),
         Part("socket", [-1.25, 8.5, -1.25], [2.5, 4, 2.5], m if tier != "stone" else "cord", "guard"),
         Part("cheek", [1.25, 8, -0.75], [2.5, 5, 1.5], m, "head"),
         Part("bit", [3.75, 6.5, -0.6], [2.5, 8, 1.2], m, "head"),
         Part("edge", [6.25, 6, -0.4], [0.75, 9, 0.8], m, "edge"),
         Part("bit_hi", [3.75, 14.5, -0.5], [2, 1, 1], m, "head"),
         Part("bit_lo", [3.75, 5.5, -0.5], [2, 1, 1], m, "head"),
         Part("poll", [-3, 9.5, -1], [1.75, 2.5, 2], m, "guard")]
    return P


# ------------------------------------------------------------------ pack export
SWORD_ITEMS = {"wood": ("wooden_sword", "wood_sword"), "stone": ("stone_sword", "stone_sword"),
               "iron": ("iron_sword", "iron_sword"), "gold": ("golden_sword", "gold_sword"),
               "diamond": ("diamond_sword", "diamond_sword"), "netherite": ("netherite_sword", "netherite_sword")}


TOOL_ITEMS = {
    "pickaxe": {"wood": ("wooden_pickaxe", "wood_pickaxe"), "stone": ("stone_pickaxe", "stone_pickaxe"),
                "iron": ("iron_pickaxe", "iron_pickaxe"), "gold": ("golden_pickaxe", "gold_pickaxe"),
                "diamond": ("diamond_pickaxe", "diamond_pickaxe"), "netherite": ("netherite_pickaxe", "netherite_pickaxe")},
    "axe": {"wood": ("wooden_axe", "wood_axe"), "stone": ("stone_axe", "stone_axe"), "iron": ("iron_axe", "iron_axe"),
            "gold": ("golden_axe", "gold_axe"), "diamond": ("diamond_axe", "diamond_axe"),
            "netherite": ("netherite_axe", "netherite_axe")},
}


FP_TEST = {"wood": [152, -9, 25], "stone": [152, -9, 55], "iron": [152, -9, -5], "gold": [152, 21, 25],
           "diamond": [152, -39, 25], "netherite": [122, -9, 25]}
TEST_GRIP = {}
TEST_ROT = {"wood": (125, 90, 0), "stone": (125, -90, 0), "iron": (125, 0, 90), "gold": (125, 0, -90),
            "diamond": (35, 0, 0), "netherite": (-55, 0, 0)}   # placement test: one candidate per sword tier


def export(pack, palettes, prefix="wfx"):
    import json
    from PIL import Image
    import preview3d as pv
    (pack / "attachables").mkdir(parents=True, exist_ok=True)
    (pack / "models" / "entity").mkdir(parents=True, exist_ok=True)
    (pack / "textures" / "worldfx").mkdir(parents=True, exist_ok=True)
    (pack / "textures" / "items").mkdir(parents=True, exist_ok=True)
    (pack / "animations").mkdir(parents=True, exist_ok=True)
    anim = json.loads(json.dumps(HOLD_ANIM))
    (pack / "animations" / "wfx_hold.animation.json").write_text(json.dumps(anim, indent=1))
    jobs = [("sword", t, it, ic, sword) for t, (it, ic) in SWORD_ITEMS.items()]
    jobs += [("pickaxe", t, it, ic, pickaxe) for t, (it, ic) in TOOL_ITEMS["pickaxe"].items()]
    jobs += [("axe", t, it, ic, axe) for t, (it, ic) in TOOL_ITEMS["axe"].items()]
    for kind, tier, item, icon, shape in jobs:
        parts = shape(tier)
        atlas, uvs, tw, th = build(parts, palettes, tier)
        name = f"{prefix}_{tier}_{kind}"
        Image.fromarray(atlas, "RGBA").save(pack / "textures" / "worldfx" / f"{name}.png")
        geo = geometry_trident(parts, uvs, tw, th, f"geometry.{name}", grip_y=TEST_GRIP.get(tier, 24.0))
        (pack / "models" / "entity" / f"{name}.geo.json").write_text(json.dumps(geo, indent=1))
        att = {"format_version": "1.10.0", "minecraft:attachable": {"description": {
            "identifier": f"minecraft:{item}",
            "materials": {"default": "entity_alphatest", "enchanted": "entity_alphatest_glint"},
            "textures": {"default": f"textures/worldfx/{name}", "enchanted": "textures/misc/enchanted_item_glint"},
            "geometry": {"default": f"geometry.{name}"},
            "animations": {"hold_first_person": "animation.wfx.hold_first_person",
                           "hold_third_person": "animation.wfx.hold_third_person"},
            "scripts": {"animate": [{"hold_first_person": "c.is_first_person"}, {"hold_third_person": "!c.is_first_person"}]},
            "render_controllers": ["controller.render.item_default"]}}}
        (pack / "attachables" / f"{item}.json").write_text(json.dumps(att, indent=1))
        ic = pv.render(preview_cubes(parts, uvs), atlas, size=32, yaw=0.0, pitch=0.0, roll=-0.785, alpha=True, flat=True)
        ic.save(pack / "textures" / "items" / f"{icon}.png")
