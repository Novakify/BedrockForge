#!/usr/bin/env python3
"""Render a lyrics_preview frame sequence (f0000.json ...) to an MP4 over a simple blocky world: grass ground,
stone walls/blocks from the dump, sky gradient, distance fog. Glyphs use render_frame.render (same maths as
RF's glyph.hlsl, BF camera estimate). Lyrics are drawn on top of the world, no depth test (like the game pass).
usage: python3 tools/LyricsPreview/render_clip.py <frames dir> <out.mp4> [width=640] [--sky night|day|dusk] [--still N]"""
import json
import math
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'LyricsFonts'))
import render_frame as rf  # noqa: E402

SKIES = {  # top, horizon
    'day': ((0.35, 0.55, 0.95), (0.70, 0.82, 1.0)),
    'dusk': ((0.22, 0.12, 0.35), (0.85, 0.55, 0.62)),
    'night': ((0.03, 0.05, 0.12), (0.12, 0.22, 0.35)),
}


def world(d, W, sky):
    H = int(round(W / d['aspect']))
    r, u, f = rf.camera(d['yaw'], d['pitch'])
    t = math.tan(math.radians(d['fov']) / 2)
    xs = (np.arange(W) + 0.5) / W * 2 - 1
    ys = 1 - (np.arange(H) + 0.5) / H * 2
    X, Y = np.meshgrid(xs * t * d['aspect'], ys * t)
    D = f[None, None, :] + X[..., None] * r + Y[..., None] * u
    D /= np.linalg.norm(D, axis=-1, keepdims=True)
    eye = np.array(d['eye'])
    top, hor = (np.array(c, np.float32) for c in SKIES[sky])
    k = np.clip(D[..., 1], 0, 1)[..., None] ** 0.6
    img = (hor * (1 - k) + top * k).astype(np.float32)
    best = np.full((H, W), np.inf)
    col = np.zeros((H, W, 3), np.float32)
    # ground plane (top of the y < ground blocks)
    with np.errstate(divide='ignore', invalid='ignore'):
        tg = (d['ground'] - eye[1]) / D[..., 1]
    hit = (tg > 0) & np.isfinite(tg)
    P = eye + D * np.where(hit, tg, 0)[..., None]
    chk = (np.floor(P[..., 0]) + np.floor(P[..., 2])) % 2
    grass = np.where(chk[..., None] > 0, [0.36, 0.60, 0.25], [0.32, 0.55, 0.22])
    m = hit & (tg < best)
    col[m], best[m] = grass[m], tg[m]
    # boxes (inclusive block ranges)
    for b in d.get('boxes', []):
        lo = np.array([b[0], b[2], b[4]], float)
        hi = np.array([b[1] + 1, b[3] + 1, b[5] + 1], float)
        with np.errstate(divide='ignore', invalid='ignore'):
            t1 = (lo - eye) / D
            t2 = (hi - eye) / D
        tn = np.nanmax(np.minimum(t1, t2), -1)
        tf = np.nanmin(np.maximum(t1, t2), -1)
        h = (tn <= tf) & (tf > 0) & (tn > 0) & (tn < best)
        if not h.any():
            continue
        axis = np.argmax(np.minimum(t1, t2), -1)
        P = eye + D * tn[..., None]
        shade = np.choose(axis, [0.78, 1.0, 0.88])
        cx = (np.floor(P[..., 0] + 1e-4) + np.floor(P[..., 1] + 1e-4) + np.floor(P[..., 2] + 1e-4)) % 2
        stone = np.where(cx[..., None] > 0, [0.55, 0.55, 0.56], [0.48, 0.48, 0.50]) * shade[..., None]
        col[h], best[h] = stone[h], tn[h]
    solid = np.isfinite(best)
    fog = np.clip(best / 60.0, 0, 1)[..., None]
    img[solid] = (col * (1 - fog) + hor * fog)[solid]
    return img


def main():
    args = sys.argv[1:]
    sky = 'day'
    still = None
    if '--sky' in args:
        i = args.index('--sky'); sky = args[i + 1]; del args[i:i + 2]
    if '--still' in args:
        i = args.index('--still'); still = int(args[i + 1]); del args[i:i + 2]
    src, out = pathlib.Path(args[0]), args[1]
    W = int(args[2]) if len(args) > 2 else 640
    frames = sorted(src.glob('f*.json'))
    if still is not None:
        d = json.loads(frames[still].read_text())
        img = rf.render(d, W, world(d, W, sky))
        Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(out)
        print('wrote', out)
        return
    H = None
    proc = None
    for fp in frames:
        d = json.loads(fp.read_text())
        img = rf.render(d, W, world(d, W, sky))
        if proc is None:
            H = img.shape[0]
            fps = 20
            proc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(fps), '-i', '-',
                                     '-c:v', 'libx264', '-preset', 'veryfast', '-pix_fmt', 'yuv420p', '-crf', '22', out], stdin=subprocess.PIPE)
        proc.stdin.write((np.clip(img, 0, 1) * 255).astype(np.uint8).tobytes())
    proc.stdin.close()
    proc.wait()
    print('wrote', out, f'({len(frames)} frames)')


if __name__ == '__main__':
    main()
