"""Java animated textures (.png.mcmeta strips) -> Bedrock.

Java plays a vertical strip of frames listed in the .mcmeta (any order, per-frame times). Bedrock entities have no
flipbook, but a render controller can scroll the UVs ("uv_anim"): Bedrock maps the model's pixel UVs over the whole
image, so with the strip in play order a scale of 1/N and an offset of frame/N shows one frame at a time.

    strip(img, meta)        -> (png bytes in play order, frames, ticks per frame) or None if not animated
    first_frame(img, meta)  -> png bytes of frame 0 (item icons: Bedrock items cannot animate)
    uv_anim(rows)           -> render-controller uv_anim for [(when, frames, ticks)] picked like the textures
"""
import io
import json
from math import gcd

from PIL import Image

MAX_H = 8192        # keep repacked strips a sane size (duplicated frames for uneven frame times)


def parse(img_bytes, meta_bytes):
    try:
        m = json.loads(meta_bytes.decode("utf-8", "replace")).get("animation")
    except Exception:
        return None
    if m is None: return None
    im = Image.open(io.BytesIO(img_bytes))
    w, h = im.size
    fw = int(m.get("width", w)); fh = int(m.get("height", fw if "width" in m else w))
    if fh <= 0 or h < 2 * fh: return None
    n = h // fh
    base = max(1, int(m.get("frametime", 1)))
    seq = []
    for f in (m.get("frames") or list(range(n))):
        if isinstance(f, dict): seq.append((int(f.get("index", 0)), max(1, int(f.get("time", base)))))
        else: seq.append((int(f), base))
    seq = [(i, t) for i, t in seq if 0 <= i < n]
    if len(seq) < 2: return None
    return im, fw, fh, n, seq


def strip(img_bytes, meta_bytes):
    p = parse(img_bytes, meta_bytes)
    if not p: return None
    im, fw, fh, n, seq = p
    unit = 0
    for _, t in seq: unit = gcd(unit, t)
    order = [i for i, t in seq for _ in range(t // unit)]
    if len(order) * fh > MAX_H:            # too many repeats: one slot per listed frame at the base time
        order = [i for i, _ in seq]; unit = seq[0][1]
    if order == list(range(n)) and im.size[0] == fw:
        return img_bytes, n, unit          # already a plain strip in play order
    out = Image.new("RGBA", (fw, fh * len(order)))
    src = im.convert("RGBA")
    for k, i in enumerate(order):
        out.paste(src.crop((0, i * fh, fw, (i + 1) * fh)), (0, k * fh))
    b = io.BytesIO(); out.save(b, "PNG")
    return b.getvalue(), len(order), unit


def first_frame(img_bytes, meta_bytes):
    p = parse(img_bytes, meta_bytes)
    if not p: return img_bytes
    im, fw, fh, n, seq = p
    b = io.BytesIO(); im.convert("RGBA").crop((0, seq[0][0] * fh, fw, (seq[0][0] + 1) * fh)).save(b, "PNG")
    return b.getvalue()


def _frame(n, t):
    return f"math.floor(math.mod(q.life_time * {20 / t:g}, {n}))"


def uv_anim(rows):
    """rows: [(when, frames, ticks)] in the render controller's texture order (last 'when' == '1' is the default).
    -> {"offset": [...], "scale": [...]} or None when nothing animates."""
    if not any(n > 1 for _, n, _ in rows): return None
    def pick(fn):
        default = next((r for r in rows if r[0] == "1"), rows[-1])
        expr = fn(default)
        for r in reversed([r for r in rows if r[0] != "1" and r is not default]):
            v = fn(r)
            if v != expr: expr = f"(({r[0]}) ? {v} : {expr})"
        return expr
    scale = pick(lambda r: "1.0" if r[1] <= 1 else f"{1 / r[1]:.6f}")
    offset = pick(lambda r: "0.0" if r[1] <= 1 else f"({_frame(r[1], r[2])} / {r[1]})")
    return {"offset": [0.0, offset], "scale": [1.0, scale]}
