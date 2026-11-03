#!/usr/bin/env python3
"""Fitted armor for the FiguraBridge stick figure (Animator vs. Animation style avatar).

Vanilla armor is drawn on the vanilla player bones, which an avatar hides - so FiguraBridge hides it on the avatar's
player (attachables/fb_armor). This makes armor of its own for the stick figure instead: segmented plates that hug
each limb, on geometries that copy the stick's exact bone hierarchy (same names, parents, pivots). The player entity
draws them with its own render controllers, so the animation that moves the stick (animation.figura.drive) moves the
armor the same way - it cannot drift off the bones.

One geometry per slot (head / chest / legs / feet), one texture per material, picked per slot from what the player
wears (query.is_item_name_any on slot.armor.*). Only on the avatar's player (v.fbnoarmor), only while the stick is the
installed avatar (v.fbstickarmor is written into its player.entity.json only).

usage: stick_armor.py STICK_GEO [PLAYER_ENTITY] OUT_DIR
  STICK_GEO: the stick's geometry (figura_stick.geo.json, or the trimmed stick_skeleton.json)
  PLAYER_ENTITY: optional - without it only the geometry, render controllers and textures are written
  writes OUT_DIR/models/entity/fb_armor_stick.geo.json, OUT_DIR/render_controllers/fb_stick_armor.render_controllers.json,
  OUT_DIR/textures/figura/fb_armor/*.png and OUT_DIR/entity/player.entity.json (the given one, patched; idempotent)
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

SLOTS = ("head", "chest", "legs", "feet")
# material index (1-based, 0 = nothing worn) -> (name, base, dark, highlight, accent)
MATERIALS = [
    ("leather", (139, 90, 55), (78, 48, 28), (190, 138, 90), (236, 196, 140)),
    ("chainmail", (118, 120, 128), (60, 62, 70), (178, 180, 190), (225, 228, 240)),
    ("iron", (196, 200, 206), (112, 118, 128), (240, 242, 246), (255, 255, 255)),
    ("golden", (236, 188, 40), (150, 100, 10), (255, 236, 130), (255, 252, 210)),
    ("diamond", (52, 214, 208), (14, 118, 128), (168, 255, 248), (236, 255, 255)),
    ("netherite", (62, 54, 62), (28, 24, 30), (108, 94, 108), (196, 92, 255)),  # neon purple accent
    ("copper", (204, 112, 70), (128, 62, 38), (242, 162, 112), (255, 214, 170)),
    ("turtle", (70, 162, 72), (34, 94, 40), (132, 212, 112), (196, 255, 168)),
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
SLOT_NAME = {"head": "slot.armor.head", "chest": "slot.armor.chest", "legs": "slot.armor.legs", "feet": "slot.armor.feet"}
VAR = {"head": "fbah", "chest": "fbac", "legs": "fbal", "feet": "fbaf"}
TEX = 32


# ---------------------------------------------------------------- textures: a bevelled panel per material
def panel(base, dark, hl, accent, chain=False):
    img = np.zeros((TEX, TEX, 4), np.uint8)
    b, d, h, a = (np.array(c, float) for c in (base, dark, hl, accent))
    for y in range(TEX):
        t = y / (TEX - 1)
        col = h * (1 - t) * 0.35 + b * (1 - 0.35 * (1 - t)) if t < 0.5 else b * (1 - (t - 0.5) * 0.5) + d * (t - 0.5) * 0.5
        img[y, :, :3] = np.clip(col, 0, 255)
    if chain:  # chainmail: rings
        for y in range(3, TEX - 3, 4):
            for x in range(3 + (y // 4 % 2) * 2, TEX - 3, 4):
                img[y, x, :3] = h
                img[y + 1, x, :3] = d
    img[:2, :, :3] = h                    # top bevel
    img[:, :2, :3] = h * 0.8 + b * 0.2     # left bevel
    img[-2:, :, :3] = d                    # bottom bevel
    img[:, -2:, :3] = d                    # right bevel
    img[0, :, :3] = d * 0.6
    img[-1, :, :3] = d * 0.6
    img[:, 0, :3] = d * 0.6
    img[:, -1, :3] = d * 0.6               # outline
    img[TEX // 2 - 1:TEX // 2 + 1, 5:TEX - 5, :3] = a  # neon accent line
    for cx in (5, TEX - 6):               # rivets
        img[5:7, cx:cx + 2, :3] = a
        img[TEX - 7:TEX - 5, cx:cx + 2, :3] = a
    img[..., 3] = 255
    return Image.fromarray(img, "RGBA")


# ---------------------------------------------------------------- geometry
def fit_line(P):
    """centre line of a rod mesh: x(y), z(y) least squares"""
    A = np.c_[P[:, 1], np.ones(len(P))]
    kx = np.linalg.lstsq(A, P[:, 0], rcond=None)[0]
    kz = np.linalg.lstsq(A, P[:, 2], rcond=None)[0]
    return lambda y: (kx[0] * y + kx[1], kz[0] * y + kz[1])


def box(x0, y0, z0, x1, y1, z1):
    faces = {f: {"uv": [0, 0], "uv_size": [TEX, TEX]} for f in ("north", "south", "east", "west", "up", "down")}
    return {"origin": [round(x0, 4), round(y0, 4), round(z0, 4)],
            "size": [round(x1 - x0, 4), round(y1 - y0, 4), round(z1 - z0, 4)], "uv": faces}


def build_cubes(bones):
    mesh = {n: np.array(b["poly_mesh"]["positions"]) for n, b in bones.items() if "poly_mesh" in b}
    C = {s: {} for s in SLOTS}

    def add(slot, bone, cube):
        C[slot].setdefault(bone, []).append(cube)

    def band(slot, bone, rodmesh, y0, y1, half_w, half_d=None, dz=0.0):
        cx, cz = fit_line(mesh[rodmesh])((y0 + y1) / 2)
        half_d = half_d if half_d is not None else half_w
        add(slot, bone, box(cx - half_w, y0, cz - half_d + dz, cx + half_w, y1, cz + half_d + dz))

    R = 0.75  # rod half thickness
    # ---- head: on the head ball's own bone (p17). Its parent p16 is a Figura "Camera" part = a billboard that turns to
    # face the viewer, so the helmet must ride p17 (not the Head group p4) to stay snug from every side.
    HEAD_BONE = "p17"
    # A band that hugs the head ball (p17: 6.6 wide, 3.5 deep, centre y 29.9 z -1.87) just above its middle,
    # clear of the camera-facing outline disc behind it (p6, z 0.85), + a small fin and a front gem.
    P = mesh["p17"]
    hx = float(P[:, 0].mean())
    zf, zb = float(P[:, 2].min()), max(float(P[:, 2].max()), 0.85)  # front of the ball, back past the disc
    y0, y1 = 30.55, 31.45
    hw = float(np.ptp(P[:, 0])) / 2 + 0.35   # half width of the band
    t = 0.45
    add("head", HEAD_BONE, box(hx - hw + 0.8, y0, zf - 0.4 - t, hx + hw - 0.8, y1, zf - 0.4))          # front
    add("head", HEAD_BONE, box(hx - hw + 0.8, y0, zb + 0.45, hx + hw - 0.8, y1, zb + 0.45 + t))        # back
    for sx in (-1, 1):
        xo = hx + sx * hw
        add("head", HEAD_BONE, box(min(xo, xo + sx * t), y0, zf + 0.35, max(xo, xo + sx * t), y1, zb + 0.1))  # side
        xc = hx + sx * (hw - 0.45)
        add("head", HEAD_BONE, box(xc - 0.55, y0 - 0.05, zf - 0.4 - t - 0.05, xc + 0.55, y1 + 0.05, zf + 0.6))  # front corner
        add("head", HEAD_BONE, box(xc - 0.55, y0 - 0.05, zb - 0.15, xc + 0.55, y1 + 0.05, zb + 0.45 + t + 0.05))  # back corner
    zc = (zf + float(P[:, 2].max())) / 2
    add("head", HEAD_BONE, box(hx - 0.25, 32.5, zc - 0.9, hx + 0.25, 33.9, zc + 0.9))                 # fin on top
    add("head", HEAD_BONE, box(hx - 0.25, 33.4, zc - 1.5, hx + 0.25, 34.3, zc - 0.5))                 # fin tip, forward
    add("head", HEAD_BONE, box(hx - 0.5, y0 - 0.25, zf - 0.4 - t - 0.25, hx + 0.5, y1 + 0.35, zf - 0.4 - t))  # front gem

    # ---- chest: vest + raised breastplate + collar + abdomen bands on the torso; pauldrons, arm bands, bracers
    band("chest", "p22", "p22", 21.6, 26.4, R + 0.8, R + 0.65)
    cx, cz = fit_line(mesh["p22"])(24.0)
    add("chest", "p22", box(cx - 1.25, 22.2, cz - R - 0.65 - 0.3, cx + 1.25, 25.9, cz - R - 0.65))  # breastplate
    band("chest", "p22", "p22", 26.4, 27.1, R + 0.5, R + 0.45)                       # collar
    band("chest", "p22", "p22", 20.0, 21.0, R + 0.45, R + 0.4)                       # abdomen
    band("chest", "p22", "p22", 18.6, 19.6, R + 0.4, R + 0.35)
    for up, lo in (("p30", "p32"), ("p37", "p39")):
        band("chest", up, up, 24.7, 26.7, R + 0.55, R + 0.5)     # pauldron
        band("chest", up, up, 26.7, 27.1, R + 0.35, R + 0.3)     # pauldron cap
        band("chest", up, up, 21.4, 22.7, R + 0.32)               # upper arm band
        band("chest", lo, lo, 15.4, 16.8, R + 0.42)               # bracer (wrist)
        band("chest", lo, lo, 17.1, 18.3, R + 0.32)               # bracer

    # ---- legs: belt + buckle, thigh plates, knee guards
    band("legs", "p22", "p22", 16.45, 17.8, R + 0.85, R + 0.7)
    cx, cz = fit_line(mesh["p22"])(17.1)
    add("legs", "p22", box(cx - 0.55, 16.65, cz - R - 0.7 - 0.25, cx + 0.55, 17.6, cz - R - 0.7))
    for th, sh in (("p44", "p46"), ("p52", "p54")):
        band("legs", th, th, 15.3, 16.9, R + 0.42)
        band("legs", th, th, 13.2, 14.8, R + 0.38)
        band("legs", th, th, 11.1, 12.7, R + 0.34)
        band("legs", sh, sh, 8.0, 9.4, R + 0.48)                  # knee guard
        cx, cz = fit_line(mesh[sh])(8.7)
        add("legs", sh, box(cx - 0.6, 8.2, cz - R - 0.48 - 0.3, cx + 0.6, 9.6, cz - R - 0.48))  # knee cap

    # ---- feet: shin guards + boots around each foot
    for sh, ft in (("p46", "p48"), ("p54", "p56")):
        band("feet", sh, sh, 4.7, 6.1, R + 0.36)
        band("feet", sh, sh, 3.1, 4.4, R + 0.42)                   # boot cuff
        P = mesh[ft]
        lo, hi = P.min(0), P.max(0)
        add("feet", ft, box(lo[0] - 0.3, 0.45, lo[2] - 0.3, hi[0] + 0.3, hi[1] + 0.25, hi[2] + 0.3))
    return C


def build(stick_geo: Path, player_entity: Path, out: Path):
    g = json.loads(stick_geo.read_text())["minecraft:geometry"][0]
    bones = {b["name"]: b for b in g["bones"]}
    cubes = build_cubes(bones)
    geos = []
    for s in SLOTS:
        bl = []
        for b in g["bones"]:
            nb = {k: v for k, v in b.items() if k in ("name", "parent", "pivot", "rotation")}
            if b["name"] in cubes[s]:
                nb["cubes"] = cubes[s][b["name"]]
            bl.append(nb)
        geos.append({"description": {"identifier": f"geometry.fb_armor.stick.{s}", "texture_width": TEX, "texture_height": TEX,
                                     "visible_bounds_width": 4, "visible_bounds_height": 5, "visible_bounds_offset": [0, 2, 0]},
                     "bones": bl})
    (out / "models/entity").mkdir(parents=True, exist_ok=True)
    (out / "models/entity/fb_armor_stick.geo.json").write_text(
        json.dumps({"format_version": "1.12.0", "minecraft:geometry": geos}, indent=1))

    (out / "textures/figura/fb_armor").mkdir(parents=True, exist_ok=True)
    for name, base, dark, hl, acc in MATERIALS:
        panel(base, dark, hl, acc, chain=name == "chainmail").save(out / f"textures/figura/fb_armor/{name}.png")

    arr = [f"Texture.fbam_{m[0]}" for m in MATERIALS]
    rcs = {}
    for s in SLOTS:
        rcs[f"controller.render.player.fb_armor_{s}"] = {
            "arrays": {"textures": {"Array.fbam": arr}},
            "geometry": f"Geometry.fbarmor_{s}",
            "materials": [{"*": "Material.fbam"}],
            "textures": [f"Array.fbam[v.{VAR[s]} - 1]"],
        }
    (out / "render_controllers").mkdir(parents=True, exist_ok=True)
    (out / "render_controllers/fb_stick_armor.render_controllers.json").write_text(
        json.dumps({"format_version": "1.10.0", "render_controllers": rcs}, indent=1))

    if player_entity is None:
        return cubes
    ent = json.loads(player_entity.read_text())
    d = ent["minecraft:client_entity"]["description"]
    for s in SLOTS:
        d["geometry"][f"fbarmor_{s}"] = f"geometry.fb_armor.stick.{s}"
    for m in MATERIALS:
        d["textures"][f"fbam_{m[0]}"] = f"textures/figura/fb_armor/{m[0]}"
    d["materials"]["fbam"] = "entity_alphatest"
    pre = d["scripts"].setdefault("pre_animation", [])
    pre[:] = [l for l in pre if not l.startswith(("variable.fbstickarmor", *(f"variable.{VAR[s]} " for s in SLOTS)))]
    if not any(l.startswith("variable.fbnoarmor") for l in pre):
        pre.append("variable.fbnoarmor = (v.fbvan ?? 1) ? 0.0 : 1.0;")
    pre.append("variable.fbstickarmor = 1.0;")
    for s in SLOTS:
        terms = " + ".join(f"q.is_item_name_any('{SLOT_NAME[s]}', 0, 'minecraft:{it}') * {i + 1}"
                           for i, it in enumerate(ITEMS[s]))
        pre.append(f"variable.{VAR[s]} = v.fbnoarmor ? ({terms}) : 0.0;")
    rc = d["render_controllers"]
    rc[:] = [r for r in rc if not (isinstance(r, dict) and any(k.startswith("controller.render.player.fb_armor_") for k in r))]
    for s in SLOTS:
        rc.append({f"controller.render.player.fb_armor_{s}": f"!variable.is_first_person && (v.fbstickarmor ?? 0) && (v.{VAR[s]} ?? 0) > 0"})
    (out / "entity").mkdir(parents=True, exist_ok=True)
    (out / "entity/player.entity.json").write_text(json.dumps(ent, indent=1))
    return cubes


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2:
        c = build(Path(a[0]), None, Path(a[1]))
    else:
        c = build(Path(a[0]), Path(a[1]), Path(a[2]))
    print({s: sum(len(v) for v in c[s].values()) for s in SLOTS}, "cubes per slot")
