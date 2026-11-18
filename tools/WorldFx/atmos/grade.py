"""Atmosphere colour grade for hand-painted base textures (by Novak).

Base art: Faithful 32x (https://faithfulpack.net, Faithful License v4 - credited in the pack). The grade keeps every
pixel the artists placed and only moves colour the way the atmosphere's light would: darks drift toward the shadow
hue, lights toward the light hue, a gentle contrast curve, saturation eased where it would go neon. Vectorised OKLab.
"""
from __future__ import annotations

import numpy as np

M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929], [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468], [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])
M1i = np.linalg.inv(M1)
M2i = np.linalg.inv(M2)


def to_lab(rgb):
    c = rgb.astype(float) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    lms = np.cbrt(lin @ M1.T)
    return lms @ M2.T


def to_rgb(lab):
    lms = (lab @ M2i.T) ** 3
    lin = np.clip(lms @ M1i.T, 0, 1)
    c = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * lin ** (1 / 2.4) - 0.055)
    return np.clip(np.round(c * 255), 0, 255).astype(np.uint8)


def grade(rgb, shadow_hue=292.0, light_hue=62.0, shadow_amt=0.030, light_amt=0.016, contrast=0.08, sat=1.0):
    lab = to_lab(rgb)
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    # tint: an additive push in the a/b plane, weighted by darkness / lightness (keeps low-chroma pixels honest)
    dark = np.clip((0.62 - L) / 0.45, 0, 1)
    light = np.clip((L - 0.55) / 0.40, 0, 1)
    sh = np.radians(shadow_hue); lh = np.radians(light_hue)
    a = a + np.cos(sh) * shadow_amt * dark + np.cos(lh) * light_amt * light
    b = b + np.sin(sh) * shadow_amt * dark + np.sin(lh) * light_amt * light
    # gentle S-curve around the texture's own mean (keeps its overall value)
    m = L.mean()
    L = m + (L - m) * (1 + contrast) - contrast * (L - m) ** 3 * 2.0
    C = np.hypot(a, b)
    k = np.where(C > 0.16, 0.16 / np.maximum(C, 1e-6) + (1 - 0.16 / np.maximum(C, 1e-6)) * 0.6, 1.0) * sat
    return to_rgb(np.stack([L, a * k, b * k], -1))
