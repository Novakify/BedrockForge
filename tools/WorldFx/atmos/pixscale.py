"""Pixel-art-aware upscaling (by Novak): Scale2x / EPX applied repeatedly. Keeps every edge hard (no blur, no
smear), rounds staircase corners of pixel clusters - the vanilla structure, redrawn crisply at 2x/4x/8x."""
from __future__ import annotations

import numpy as np


def scale2x(a):
    """a: HxWxC uint8 (tileable: neighbours wrap)"""
    A = np.roll(a, 1, 0); D = np.roll(a, -1, 0)      # up, down
    C = np.roll(a, 1, 1); B = np.roll(a, -1, 1)      # left, right
    eq = lambda p, q: np.all(p == q, axis=-1)
    e0 = np.where((eq(C, A) & ~eq(C, D) & ~eq(A, B))[..., None], A, a)
    e1 = np.where((eq(A, B) & ~eq(A, C) & ~eq(B, D))[..., None], B, a)
    e2 = np.where((eq(D, C) & ~eq(D, B) & ~eq(C, A))[..., None], C, a)
    e3 = np.where((eq(B, D) & ~eq(B, A) & ~eq(D, C))[..., None], D, a)
    h, w = a.shape[:2]
    out = np.zeros((h * 2, w * 2, a.shape[2]), a.dtype)
    out[0::2, 0::2] = e0; out[0::2, 1::2] = e1; out[1::2, 0::2] = e2; out[1::2, 1::2] = e3
    return out


def upscale(a, factor):
    while factor > 1:
        a = scale2x(a)
        factor //= 2
    return a
