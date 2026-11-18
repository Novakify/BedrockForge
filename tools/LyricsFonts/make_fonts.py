#!/usr/bin/env python3
"""LyricsFlow port - font atlas generator (original code for Bedrock Forge).

Builds the glyph atlases the RF glyph shader samples, plus msdf-atlas-gen-style JSON metrics that
src/lyrics/core/Glyphs.cpp reads (FontSet::loadFromJson). Two kinds:

  * SDF (single channel, in the PNG's grey/red channel): for the LyricsFlow TTFs (script/brush/sans). Rendered with
    FreeType (Pillow) at `--em * --ss` px, exact Euclidean distance transform (scipy) inside and outside, box-filtered
    down to `--em` px per em. 0.5 = the outline, `--range` atlas px of distance span 0..1 (msdf-atlas-gen convention,
    so the shader's `(v - 0.5) * pxRange` gives atlas-pixel distance). Good glow/outline at any size.
  * bitmap (hard 0/255 mask, nearest-sampled): the Minecraft-style pixel preset. Built from an OFL pixel font (Pixelify
    Sans) in the cloud; on the PC, `--default8 <path to the game's font/default8.png>` builds the real Minecraft
    look from the installed game instead (that file is Mojang's - generate it locally, never commit it).

True MSDF (sharper corners when glyphs are huge) can replace any SDF atlas later: run msdf-atlas-gen with
`-type msdf -format png -json <id>.json -imageout <id>.png -pxrange 6 -size 48 -yorigin top` and Glyphs.cpp picks the
"msdf" type up automatically (the shader takes the median of RGB).

usage:
  python3 tools/LyricsFonts/make_fonts.py --ttf-dir third_party/lyricsflow/fonts --out resources/lyricsflow/fonts \
      --fonts lyricsflow_script greatvibes kaushan outfit outfit_bold poppins pacifico dancing \
      --pixel minecraft=third_party/lyricsflow/fonts/PixelifySans.ttf
  python3 tools/LyricsFonts/make_fonts.py --out <dir> --default8 "<game>/data/resource_packs/vanilla/font/default8.png"
"""
import argparse
import json
import math
import pathlib

import numpy as np
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

# Latin (ASCII, Latin-1, Latin Extended-A) + typographic punctuation + music notes; only what the font has is used.
CHARSET = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x180)) +
           [0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026, 0x2022, 0x266A, 0x266B, 0x266C])


class Packer:
    """Shelf packer, fixed width, grows downward."""
    def __init__(self, width):
        self.w, self.x, self.y, self.shelf = width, 1, 1, 0

    def place(self, w, h):
        if self.x + w + 1 > self.w:
            self.x, self.y, self.shelf = 1, self.y + self.shelf + 1, 0
        pos = (self.x, self.y)
        self.x += w + 1
        self.shelf = max(self.shelf, h)
        return pos

    def height(self):
        return self.y + self.shelf + 1


def cmap_of(path):
    tt = TTFont(path, lazy=True)
    cm = tt.getBestCmap() or {}
    os2 = tt['OS/2'] if 'OS/2' in tt else None
    upem = tt['head'].unitsPerEm
    cap = getattr(os2, 'sCapHeight', 0) if os2 else 0
    xh = getattr(os2, 'sxHeight', 0) if os2 else 0
    return set(cm.keys()), upem, cap, xh


def sdf_glyph(font, ch, em, ss, rng):
    """Returns (array uint8 HxW in atlas px, plane bounds l,b,r,t in em) or None for blank glyphs."""
    S = em * ss
    l, t, r, b = font.getbbox(ch, anchor='ls')
    if r - l <= 0 or b - t <= 0:
        return None
    pad = int(math.ceil(rng / 2 + 1)) * ss
    W = int(math.ceil((r - l + 2 * pad) / ss)) * ss
    H = int(math.ceil((b - t + 2 * pad) / ss)) * ss
    img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(img).text((pad - l, pad - t), ch, font=font, fill=255, anchor='ls')
    inside = np.asarray(img) >= 128
    if not inside.any():
        return None
    d_out = ndimage.distance_transform_edt(~inside)
    d_in = ndimage.distance_transform_edt(inside)
    sd = np.where(inside, d_in - 0.5, -(d_out - 0.5))           # hi-res px, + inside
    sd = sd.reshape(H // ss, ss, W // ss, ss).mean(axis=(1, 3)) / ss  # atlas px
    v = np.clip(0.5 + sd / rng, 0.0, 1.0)
    arr = (v * 255.0 + 0.5).astype(np.uint8)
    left = (l - pad) / S
    top = -(t - pad) / S
    return arr, (left, top - H / S, left + W / S, top)


def bitmap_glyph(font, ch, em):
    l, t, r, b = font.getbbox(ch, anchor='ls')
    if r - l <= 0 or b - t <= 0:
        return None
    pad = 1
    W, H = r - l + 2 * pad, b - t + 2 * pad
    img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(img).text((pad - l, pad - t), ch, font=font, fill=255, anchor='ls')
    arr = np.where(np.asarray(img) >= 128, 255, 0).astype(np.uint8)
    if not arr.any():
        return None
    left, top = (l - pad) / em, -(t - pad) / em
    return arr, (left, top - H / em, left + W / em, top)


def build(font_id, path, out, em, ss, rng, kind, width=1024, variation=None):
    have, upem, cap, xh = cmap_of(path)
    size = em * ss if kind == 'sdf' else em
    font = ImageFont.truetype(str(path), size=size)
    try:  # variable fonts (Orbitron, Fredoka): render a bolder instance - the default is the thin end
        axes = font.get_variation_axes()
        font.set_variation_by_axes([min(a['maximum'], max(a['minimum'], 700 if a.get('name', b'') in (b'Weight', 'Weight') else a['default'])) for a in axes])
    except Exception:
        pass
    if variation is not None:
        try:
            font.set_variation_by_axes(variation)
        except Exception:
            pass
    asc, desc = font.getmetrics()
    glyphs, items = [], []
    for cp in CHARSET:
        if cp not in have:
            continue
        ch = chr(cp)
        adv = font.getlength(ch) / size
        g = sdf_glyph(font, ch, em, ss, rng) if kind == 'sdf' else bitmap_glyph(font, ch, em)
        items.append((cp, adv, g))
    pk = Packer(width)
    placed = []
    for cp, adv, g in sorted(items, key=lambda it: -(it[2][0].shape[0] if it[2] else 0)):
        if g is None:
            placed.append((cp, adv, None, None))
            continue
        h, w = g[0].shape
        placed.append((cp, adv, g, pk.place(w, h)))
    height = 1 << max(6, int(math.ceil(math.log2(pk.height()))))
    atlas = np.zeros((height, width), np.uint8)
    for cp, adv, g, pos in sorted(placed, key=lambda p: p[0]):
        entry = {'unicode': cp, 'advance': round(adv, 6)}
        if g is not None:
            arr, (pl, pb, pr, pt) = g
            x, y = pos
            h, w = arr.shape
            atlas[y:y + h, x:x + w] = arr
            entry['planeBounds'] = {'left': round(pl, 6), 'bottom': round(pb, 6), 'right': round(pr, 6), 'top': round(pt, 6)}
            entry['atlasBounds'] = {'left': x, 'top': y, 'right': x + w, 'bottom': y + h}
        glyphs.append(entry)
    cap_em = cap / upem if cap else (-font.getbbox('H', anchor='ls')[1] / size if 72 in have else 0.7)
    xh_em = xh / upem if xh else (-font.getbbox('x', anchor='ls')[1] / size if 120 in have else 0.5)
    meta = {
        'atlas': {'type': kind, 'distanceRange': rng if kind == 'sdf' else 0, 'size': em, 'width': width, 'height': height,
                  'yOrigin': 'top'},
        'metrics': {'emSize': 1, 'lineHeight': round((asc + desc) / size, 6), 'ascender': round(asc / size, 6),
                    'descender': round(-desc / size, 6), 'capHeight': round(cap_em, 6), 'xHeight': round(xh_em, 6)},
        'glyphs': glyphs,
        'generator': 'tools/LyricsFonts/make_fonts.py',
        'source': pathlib.Path(path).name,
    }
    out.mkdir(parents=True, exist_ok=True)
    Image.fromarray(atlas, 'L').save(out / f'{font_id}.png', optimize=True)
    (out / f'{font_id}.json').write_text(json.dumps(meta, separators=(',', ':')))
    print(f'{font_id}: {len(glyphs)} glyphs, atlas {width}x{height} ({kind})')


def from_default8(png, out, font_id='minecraft'):
    """Minecraft's own ASCII font sheet (16x16 cells): per-glyph width from the rightmost lit column, 1 px spacing."""
    img = np.asarray(Image.open(png).convert('RGBA'))[:, :, 3]
    cell = img.shape[1] // 16
    k = cell / 8.0  # default8 is 128px (8px cells); HD packs scale up
    width = 1024
    pk = Packer(width)
    glyphs, blits = [], []
    for cp in range(0x20, 0x7F):
        cy, cx = divmod(cp, 16)
        c = img[cy * cell:(cy + 1) * cell, cx * cell:(cx + 1) * cell] >= 128
        cols = np.where(c.any(axis=0))[0]
        if cp == 0x20 or len(cols) == 0:
            glyphs.append({'unicode': cp, 'advance': 4 / 8.0})
            continue
        w = int(cols.max()) + 1
        arr = np.pad(np.where(c[:, :w], 255, 0).astype(np.uint8), 1)
        x, y = pk.place(arr.shape[1], arr.shape[0])
        blits.append((x, y, arr))
        # baseline at cell row 7 (of 8): 1 px descender
        glyphs.append({'unicode': cp, 'advance': (w + k) / (8 * k),
                       'planeBounds': {'left': -1 / (8 * k), 'bottom': -(1 + 1 / k) / 8, 'right': (w + 1) / (8 * k), 'top': (7 + 1 / k) / 8},
                       'atlasBounds': {'left': x, 'top': y, 'right': x + arr.shape[1], 'bottom': y + arr.shape[0]}})
    height = 1 << max(6, int(math.ceil(math.log2(pk.height()))))
    atlas = np.zeros((height, width), np.uint8)
    for x, y, arr in blits:
        atlas[y:y + arr.shape[0], x:x + arr.shape[1]] = arr
    meta = {'atlas': {'type': 'bitmap', 'distanceRange': 0, 'size': cell, 'width': width, 'height': height, 'yOrigin': 'top'},
            'metrics': {'emSize': 1, 'lineHeight': 9 / 8, 'ascender': 7 / 8, 'descender': -1 / 8, 'capHeight': 7 / 8, 'xHeight': 5 / 8},
            'glyphs': glyphs, 'generator': 'tools/LyricsFonts/make_fonts.py --default8', 'source': 'default8.png (local game file, do not commit)'}
    out.mkdir(parents=True, exist_ok=True)
    Image.fromarray(atlas, 'L').save(out / f'{font_id}.png', optimize=True)
    (out / f'{font_id}.json').write_text(json.dumps(meta, separators=(',', ':')))
    print(f'{font_id}: {len(glyphs)} glyphs from default8 ({cell}px cells)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ttf-dir', type=pathlib.Path)
    ap.add_argument('--out', type=pathlib.Path, required=True)
    ap.add_argument('--fonts', nargs='*', default=[])
    ap.add_argument('--pixel', action='append', default=[], help='id=path.ttf : bitmap (nearest) atlas from a pixel TTF')
    ap.add_argument('--default8', type=pathlib.Path, help="the game's font/default8.png -> 'minecraft' bitmap atlas")
    ap.add_argument('--em', type=int, default=48, help='atlas pixels per em (SDF)')
    ap.add_argument('--ss', type=int, default=4, help='supersampling for the distance transform')
    ap.add_argument('--range', type=float, default=6.0, help='SDF distance range, atlas px')
    a = ap.parse_args()
    for fid in a.fonts:
        build(fid, a.ttf_dir / f'{fid}.ttf', a.out, a.em, a.ss, a.range, 'sdf')
    for spec in a.pixel:
        fid, path = spec.split('=', 1)
        build(fid, pathlib.Path(path), a.out, 32, 1, 0, 'bitmap', variation=[700])
    if a.default8:
        from_default8(a.default8, a.out)


if __name__ == '__main__':
    main()
