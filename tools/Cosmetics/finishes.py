"""Knife finishes (by Novak): every knife ships in 4 finishes, made from its own painted atlas so each keeps its
real detail (Damascus layers, stonewash, grinds, wear) and only the coating changes - like the real coatings:

  0 Factory   the knife as designed (knives.py)
  1 Blackout  black DLC on the steel; the sharpened edge and high spots stay raw steel; dark handles and hardware
  2 Gold      titanium-nitride gold on the steel (TiN keeps the pattern under it); handles unchanged
  3 Fade      anodised heat fade along the blade: straw gold at the handle -> violet -> deep blue at the tip

Order = Cosmetics.cpp kFinishes and the texture array in build_pack.py (index (knife - 1) * 4 + finish).
Alpha (World FX material id) is never touched.
"""
from __future__ import annotations

import numpy as np

NAMES = ("factory", "blackout", "gold", "fade")

# atlas regions by role, per knife (region names from knives.py *_atlas)
STEEL = {"karambit": ("face", "plate", "spine", "rim", "ringface"),
         "balisong": ("face", "plate", "spine", "rim", "steel"),
         "dagger": ("face", "plate", "spine", "rim", "steel")}
HANDLE = {"karambit": ("scales", "liner"), "balisong": (), "dagger": ()}
HARDWARE = {"karambit": ("brass",), "balisong": ("bronze",), "dagger": ("brass", "knurl", "grip")}
# karambit: the ring + rim wrap round, so they take the handle-end colour instead of a seam
FADE_LEN = {"karambit": ("face", "plate", "spine"), "balisong": ("face", "plate", "spine", "rim"),
            "dagger": ("face", "plate", "spine", "rim")}


def lum(rgb):
    return (rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114) / 255.0


def _blackout_steel(rgb):
    L = lum(rgb)
    base = 12 + 84 * L ** 1.8
    edge = np.clip((L - 0.8) / 0.2, 0, 1) ** 1.5 * 110       # raw steel on the sharpened edge / high spots
    out = (base + edge)[..., None] * np.array([1.0, 1.0, 1.06])
    return out


def _blackout_dark(rgb, k=0.42):
    L = lum(rgb)
    return (10 + 120 * k * L)[..., None] * np.array([1.0, 0.98, 0.96])


def _gold(rgb):
    L = lum(rgb)
    tin = np.array([214, 170, 84.0]) / 255
    return np.clip((0.22 + 1.05 * L)[..., None] * tin * 255, 0, 255)


def _fade(rgb, u):
    L = lum(rgb)
    stops = np.array([[0.0, 222, 176, 92], [0.42, 178, 92, 176], [0.72, 92, 92, 206], [1.0, 58, 108, 214]])
    col = np.stack([np.interp(u, stops[:, 0], stops[:, i]) for i in (1, 2, 3)], -1) / 255
    return np.clip((0.24 + 1.0 * L)[..., None] * col * 255 + (np.clip(L - 0.8, 0, 1) * 300)[..., None], 0, 255)


def apply(kind, atlas_img, rects, finish):
    """-> RGBA uint8 copy of the knife's atlas with finish (index or name) applied"""
    f = NAMES.index(finish) if isinstance(finish, str) else int(finish)
    img = atlas_img.copy()
    if f == 0:
        return img
    rgbf = img[..., :3].astype(float)

    def each(names, fn):
        for n in names:
            if n not in rects:
                continue
            x, y, w, h = rects[n]
            reg = rgbf[y:y + h, x:x + w]
            u = np.broadcast_to(np.linspace(0, 1, w)[None, :], (h, w))
            rgbf[y:y + h, x:x + w] = fn(reg, u)

    if f == 1:
        each(STEEL[kind], lambda r, u: _blackout_steel(r))
        each(HANDLE[kind], lambda r, u: _blackout_dark(r))
        each(HARDWARE[kind], lambda r, u: _blackout_dark(r, 0.55))
    elif f == 2:
        each(STEEL[kind], lambda r, u: _gold(r))
    elif f == 3:
        each(FADE_LEN[kind], _fade)
        each(tuple(n for n in STEEL[kind] if n not in FADE_LEN[kind]), lambda r, u: _fade(r, np.full_like(u, 0.1)))
    img[..., :3] = np.clip(rgbf, 0, 255).astype(np.uint8)
    return img
