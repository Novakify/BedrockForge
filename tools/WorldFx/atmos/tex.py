"""World FX atmospheres: small texture toolkit (by Novak).

Every texture is painted the way a pixel artist would: a tileable SHAPE field (height / shade, 0..1) is built from
real structure (stitches, boards, stones, facets), then mapped onto a short hand-tuned colour RAMP (5-8 colours,
hue-shifted: shadows lean to the atmosphere's shadow hue, highlights to its light hue). No smooth gradients, no
free-floating noise: that is what keeps it from looking generated.
"""
from __future__ import annotations

import math

import numpy as np
from PIL import Image

# ------------------------------------------------------------------ OKLab / OKLCh (perceptual colour maths)


def _lin(c):
    c = np.asarray(c, float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def _srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055) * 255.0


def rgb_to_oklch(rgb):
    r, g, b = _lin(rgb)
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = np.cbrt(l), np.cbrt(m), np.cbrt(s)
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return float(L), float(math.hypot(a, bb)), float(math.degrees(math.atan2(bb, a)) % 360)


def oklch_to_rgb(L, C, H):
    a, b = C * math.cos(math.radians(H)), C * math.sin(math.radians(H))
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s
    bb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    return tuple(int(round(v)) for v in _srgb(np.array([r, g, bb])))


def hue_toward(h, target, amt):
    d = ((target - h + 540) % 360) - 180
    return (h + d * amt) % 360


def ramp(base, n=6, lo=-0.24, hi=0.13, shadow_hue=292.0, light_hue=62.0, shadow_shift=0.22, light_shift=0.10,
         chroma_lo=0.85, chroma_hi=0.80):
    """n colours dark->light around `base` (sRGB). Darks drift toward shadow_hue, lights toward light_hue; chroma
    eases off at both ends so the extremes never look neon or muddy."""
    L, C, H = rgb_to_oklch(base)
    out = []
    for i in range(n):
        t = i / (n - 1)
        dl = lo + (hi - lo) * t
        k = (t - 0.5) * 2  # -1 dark .. +1 light
        if k < 0:
            hh = hue_toward(H, shadow_hue, shadow_shift * -k)
            c = C * (1 - (1 - chroma_lo) * -k) + (0.008 * -k if C < 0.03 else 0)
        else:
            hh = hue_toward(H, light_hue, light_shift * k)
            c = C * (1 - (1 - chroma_hi) * k)
        out.append(oklch_to_rgb(min(max(L + dl, 0.02), 0.985), max(c, 0.0), hh))
    return out


# ------------------------------------------------------------------ tileable fields


def rng(seed):
    return np.random.default_rng(seed)


def value_noise(size, cells, seed, octaves=1, persist=0.5):
    """tileable smooth value noise in 0..1, `cells` lattice cells across the tile at octave 0"""
    out = np.zeros((size, size))
    amp, tot = 1.0, 0.0
    r = rng(seed)
    for o in range(octaves):
        n = cells * (2 ** o)
        g = r.random((n, n))
        y, x = np.mgrid[0:size, 0:size] / size * n
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a = g[y0 % n, x0 % n]
        b = g[y0 % n, (x0 + 1) % n]
        c = g[(y0 + 1) % n, x0 % n]
        d = g[(y0 + 1) % n, (x0 + 1) % n]
        out += amp * (a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy)
        tot += amp
        amp *= persist
    return out / tot


def voronoi(size, points):
    """tileable Voronoi: returns (f1, f2, index) for each pixel; points in tile pixel coords"""
    pts = np.asarray(points, float)
    y, x = np.mgrid[0:size, 0:size] + 0.5
    best1 = np.full((size, size), 1e9)
    best2 = np.full((size, size), 1e9)
    idx = np.zeros((size, size), int)
    for i, (px, py) in enumerate(pts):
        dx = np.abs(x - px)
        dy = np.abs(y - py)
        dx = np.minimum(dx, size - dx)
        dy = np.minimum(dy, size - dy)
        d = np.sqrt(dx * dx + dy * dy)
        closer = d < best1
        best2 = np.where(closer, best1, np.minimum(best2, d))
        idx = np.where(closer, i, idx)
        best1 = np.where(closer, d, best1)
    return best1, best2, idx


def poisson_points(size, n, min_d, seed, tries=4000):
    r = rng(seed)
    pts = []
    for _ in range(tries):
        if len(pts) >= n:
            break
        p = r.random(2) * size
        ok = True
        for q in pts:
            dx = abs(p[0] - q[0]); dx = min(dx, size - dx)
            dy = abs(p[1] - q[1]); dy = min(dy, size - dy)
            if dx * dx + dy * dy < min_d * min_d:
                ok = False
                break
        if ok:
            pts.append(p)
    return pts


def shift_light(h, dx=1, dy=1):
    """baked light from the top-left: h minus h shifted toward the light (a cheap emboss), tileable"""
    return h - np.roll(np.roll(h, dy, 0), dx, 1)


# ------------------------------------------------------------------ mapping to a ramp


def quantize(field, colours, dither=0.0, seed=0, bias=0.0):
    """map a 0..1 field onto the ramp (index = field * len). `dither` adds a tiny ordered offset only where two
    shades meet, so transitions get a clean pixel-art edge instead of a smear."""
    n = len(colours)
    f = np.clip(field + bias, 0, 0.9999)
    if dither > 0:
        bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5
        s = field.shape[0]
        f = np.clip(f + np.tile(bayer, (s // 4 + 1, s // 4 + 1))[:s, :s] * dither / n, 0, 0.9999)
    idx = (f * n).astype(int)
    pal = np.array(colours, np.uint8)
    return pal[idx], idx


def clean_isolated(idx, colours):
    """remove single pixels whose 4 neighbours all share one other shade (pixel-art cleanup: no salt and pepper)"""
    out = idx.copy()
    n = [np.roll(idx, 1, 0), np.roll(idx, -1, 0), np.roll(idx, 1, 1), np.roll(idx, -1, 1)]
    same = (n[0] == n[1]) & (n[1] == n[2]) & (n[2] == n[3]) & (n[0] != idx)
    out[same] = n[0][same]
    pal = np.array(colours, np.uint8)
    return pal[out], out


def normalize(f):
    lo, hi = f.min(), f.max()
    return (f - lo) / (hi - lo + 1e-9)


def save(img_rgb, path, alpha=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    if alpha is None:
        Image.fromarray(img_rgb.astype(np.uint8), "RGB").save(path)
    else:
        a = np.clip(alpha, 0, 255).astype(np.uint8)
        Image.fromarray(np.dstack([img_rgb.astype(np.uint8), a]), "RGBA").save(path)


def sheet(items, path, zoom=6, tile=2, cols=8, label_h=0):
    """review sheet: each texture tiled `tile`x`tile` at `zoom`, on a mid-grey card"""
    ims = []
    for name, im in items:
        a = np.asarray(im)
        if a.ndim == 3 and a.shape[2] == 4:
            bg = np.zeros_like(a[..., :3]) + 0
            yy, xx = np.mgrid[0:a.shape[0], 0:a.shape[1]]
            bg[:] = np.where(((yy // 4 + xx // 4) % 2 == 0)[..., None], 150, 110)
            al = a[..., 3:4] / 255.0
            a = (a[..., :3] * al + bg * (1 - al)).astype(np.uint8)
        a = np.tile(a, (tile, tile, 1))
        ims.append(np.kron(a, np.ones((zoom, zoom, 1), np.uint8)))
    h, w = ims[0].shape[:2]
    rows = (len(ims) + cols - 1) // cols
    pad = 6
    out = np.full((rows * (h + pad) + pad, cols * (w + pad) + pad, 3), 70, np.uint8)
    for i, im in enumerate(ims):
        r, c = divmod(i, cols)
        out[pad + r * (h + pad):pad + r * (h + pad) + im.shape[0], pad + c * (w + pad):pad + c * (w + pad) + im.shape[1]] = im
    Image.fromarray(out).save(path)
