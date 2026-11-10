"""3D held items from hand-painted icons (by Novak): every opaque pixel of the item's 32x icon becomes depth - runs of
pixels are merged into boxes, front/back faces show the icon itself, the sides take the colour of the edge pixel.
The silhouette and pixel art stay exactly the artists' (Faithful 32x), the item gains real thickness and catches the
Cloud Sea light. Held through Mojang's trident frame (see equip.py)."""
from __future__ import annotations

import numpy as np

PX = 0.5          # model units per icon pixel (32 px icon -> 16 units, vanilla sprite scale)
DEPTH = 1.0       # thickness in model units


def boxes_from_icon(a):
    """a: HxWx4. Returns list of (x0, y0, w) horizontal runs of opaque pixels (icon coords, y down)."""
    h, w = a.shape[:2]
    runs = []
    for y in range(h):
        x = 0
        while x < w:
            if a[y, x, 3] > 0:
                s = x
                while x < w and a[y, x, 3] > 0:
                    x += 1
                runs.append((s, y, x - s))
            else:
                x += 1
    return runs


def model(icon, ident, grip=(5.5, 26.5), tex_name=None):
    """geometry for the trident frame: bone 'wfx_item' (pivot 0,24,0, bound to the item slot) holding child bone
    'wfx_tilt' rotated 45 deg so the icon's diagonal (handle bottom-left -> tip top-right) stands along +y with the
    grip at the pivot."""
    h, w = icon.shape[:2]
    gx, gy = grip
    cubes = []
    for (x0, y, n) in boxes_from_icon(icon):
        ox = (x0 - gx) * PX
        oy = (gy - (y + 1)) * PX
        cube = {"origin": [ox, 24 + oy, -DEPTH / 2], "size": [n * PX, PX, DEPTH],
                "uv": {"north": {"uv": [x0 + n, y], "uv_size": [-n, 1]},     # back face mirrors the icon
                       "south": {"uv": [x0, y], "uv_size": [n, 1]},
                       "east": {"uv": [x0 + n - 1, y], "uv_size": [1, 1]},
                       "west": {"uv": [x0, y], "uv_size": [1, 1]},
                       "up": {"uv": [x0, y], "uv_size": [n, 1]},
                       "down": {"uv": [x0, y], "uv_size": [n, 1]}}}
        cubes.append(cube)
    bones = [{"name": "wfx_item", "pivot": [0, 24, 0], "binding": "q.item_slot_to_bone_name(c.item_slot)"},
             {"name": "wfx_tilt", "parent": "wfx_item", "pivot": [0, 24, 0], "rotation": [0, 0, -45], "cubes": cubes}]
    return {"format_version": "1.16.0", "minecraft:geometry": [{
        "description": {"identifier": ident, "texture_width": w, "texture_height": h,
                        "visible_bounds_width": 3, "visible_bounds_height": 4, "visible_bounds_offset": [0, 1, 0]},
        "bones": bones}]}


def material_alpha(icon, tier):
    """World FX material id in alpha for the actor shader: blade/head pixels of metal tiers -> metal (12), diamond ->
    gem (13); handle pixels (brown wood) keep id 0"""
    a = icon.copy()
    rgb = a[..., :3].astype(int)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    brown = (r > g) & (g > b) & ((r - b) > 30) & (r < 200) & (tier not in ("gold",))
    mid = {"iron": 12, "gold": 12, "netherite": 12, "diamond": 13}.get(tier, 0)
    op = a[..., 3] > 0
    a[..., 3] = np.where(op, np.where(brown, 255, 255 - 4 * mid), 0).astype(np.uint8)
    return a
