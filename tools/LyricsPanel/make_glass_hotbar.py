#!/usr/bin/env python3
"""Generate the LyricsFlow glass hotbar resource pack (Bedrock): a clean see-through rounded bar with a soft top sheen,
thin bright edge, faint slot dividers and a glowing selected-slot frame (look: LyricsFlow 2.1 "glass hotbar" + the
Blueberry-client style Fernando asked for). Textures keep the vanilla sizes x4 (hotbar_0..8 20x22, caps 1x22,
selected_hotbar_slot 24x24), so the game lays them out exactly like vanilla.
usage: python3 tools/LyricsPanel/make_glass_hotbar.py <out pack dir> [preview.png over this background]"""
import json
import pathlib
import sys
import uuid

import numpy as np
from PIL import Image

S = 4  # texture px per UI px


def sdf_round_rect(w, h, x0, y0, x1, y1, r):
    """Signed distance (texture px) to a rounded rect, negative inside."""
    ys, xs = np.mgrid[0:h, 0:w] + 0.5
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = np.abs(xs - cx) - hx, np.abs(ys - cy) - hy
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def cover(d, soft=0.75):
    return np.clip(0.5 - d / soft, 0, 1)


def bar():
    """The whole 180 x 22 UI px bar (slots 0..8), rounded, glass."""
    W, H = 180 * S, 22 * S
    d = sdf_round_rect(W, H, 0.5 * S, 1 * S, W - 0.5 * S, H - 1 * S, 6 * S)
    inside = cover(d)
    ys = (np.mgrid[0:H, 0:W][0] + 0.5) / H
    img = np.zeros((H, W, 4), np.float32)
    # body: dark see-through glass, a touch lighter at the top (sheen), slightly purple-tinted
    body_a = 0.40 + 0.06 * (1 - ys)
    body = np.stack([np.full_like(ys, 0.06), np.full_like(ys, 0.06), np.full_like(ys, 0.10)], -1)
    sheen = np.clip(1 - (ys - 0.08) / 0.38, 0, 1) ** 2 * 0.10  # white sheen on the upper third
    rgb = body + sheen[..., None]
    a = body_a * inside
    # thin bright edge (inner 1 UI px ring) + softer outer edge
    edge = cover(np.abs(d + 0.5 * S) - 0.5 * S, 0.9) * inside
    rgb = rgb * (1 - edge[..., None] * 0.65) + np.array([1, 1, 1]) * edge[..., None] * 0.65
    a = np.maximum(a, edge * (0.16 + 0.08 * (1 - ys)))
    # faint dividers between slots
    xs = np.mgrid[0:H, 0:W][1]
    for k in range(1, 9):
        x = k * 20 * S
        m = (np.abs(xs - x) < 0.5 * S) & (ys > 0.22) & (ys < 0.78)
        rgb[m] = rgb[m] * 0.7 + 0.3
        a[m] = np.maximum(a[m], 0.10 * inside[m])
    img[..., :3] = rgb
    img[..., 3] = a
    return img


def selected():
    """24 x 24 UI px frame: rounded bright outline with a soft glow, faint inner light."""
    W = H = 24 * S
    d = sdf_round_rect(W, H, 2 * S, 2 * S, W - 2 * S, H - 2 * S, 5 * S)
    ring = cover(np.abs(d) - 0.55 * S, 0.9)
    glow = np.clip(1 - np.abs(d) / (2.2 * S), 0, 1) ** 2 * 0.45
    fill = cover(d) * 0.10
    img = np.zeros((H, W, 4), np.float32)
    lav = np.array([0.82, 0.76, 1.0])
    a = np.clip(ring * 0.95 + glow + fill, 0, 1)
    col = (np.array([1, 1, 1]) * ring[..., None] + lav * glow[..., None] + np.array([1, 1, 1]) * fill[..., None]) / np.maximum(a, 1e-4)[..., None]
    img[..., :3] = np.clip(col, 0, 1)
    img[..., 3] = a
    return img


def to_png(a):
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8), 'RGBA')


def main():
    out = pathlib.Path(sys.argv[1])
    ui = out / 'textures/ui'
    ui.mkdir(parents=True, exist_ok=True)
    b = bar()
    for k in range(9):
        to_png(b[:, k * 20 * S:(k + 1) * 20 * S]).save(ui / f'hotbar_{k}.png')
    for cap in ('hotbar_start_cap', 'hotbar_end_cap'):  # ends are rounded inside slots 0 / 8: caps are invisible
        to_png(np.zeros((22 * S, 1 * S, 4), np.float32)).save(ui / f'{cap}.png')
        (ui / f'{cap}.json').write_text(json.dumps({"nineslice_size": [0, 0, 0, 0], "base_size": [1, 22]}, indent=2))
    to_png(selected()).save(ui / 'selected_hotbar_slot.png')
    man = out / 'manifest.json'
    if not man.exists():
        man.write_text(json.dumps({
            "format_version": 2,
            "header": {"name": "LyricsFlow Glass Hotbar", "description": "See-through glass hotbar (LyricsFlow 2.1 port, personal build)",
                       "uuid": str(uuid.uuid4()), "version": [1, 0, 0], "min_engine_version": [1, 21, 0]},
            "modules": [{"type": "resources", "uuid": str(uuid.uuid4()), "version": [1, 0, 0]}]}, indent=2))
    icon = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    icon.alpha_composite(to_png(selected()).resize((128, 128)))
    icon.save(out / 'pack_icon.png')
    print('wrote', out)

    if len(sys.argv) > 2:  # preview over a screenshot, centred at the bottom, GUI scale like 1080p (x3)
        bg = Image.open(sys.argv[2]).convert('RGBA')
        g = 3
        barimg = to_png(b).resize((180 * g, 22 * g), Image.LANCZOS)
        sel = to_png(selected()).resize((24 * g, 24 * g), Image.LANCZOS)
        x0, y0 = (bg.width - barimg.width) // 2, bg.height - barimg.height - 6
        bg.alpha_composite(barimg, (x0, y0))
        bg.alpha_composite(sel, (x0 + 2 * 20 * g - 2 * g, y0 - 1 * g))
        bg.convert('RGB').save(sys.argv[3] if len(sys.argv) > 3 else 'glass_preview.png')


if __name__ == '__main__':
    main()
