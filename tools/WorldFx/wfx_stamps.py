"""World FX tag stamps ("WFX1") - the invisible fingerprints the tag test pack puts in block textures.

Renderer Foundation's World FX probe (src/WorldFx.cpp, findStamps) searches the game's terrain atlas for them, so
World FX knows exactly where glass, team wool, gens ... sit in the atlas even under other resource packs.
Keep this file and WorldFx.cpp in sync.

A stamp is 4 texels in a row. Each texel's RGB low 3 bits spell a 9-bit code (r << 6 | g << 3 | b):
  top-left     texels (0..3, 0):         A=342 (5,2,6)  B=241 (3,6,1)  id  check   alpha kept      ("soft")
  bottom-right texels (w-4..w-1, h-1):   A=405 (6,2,5)  B=115 (1,6,3)  id  check   alpha forced 255 ("hard")
check = (id * 37 + 11) & 511. Colour changes by at most 7/255 per channel: invisible.
Soft vs hard answers whether the game keeps RGB exact in texels with alpha < 255 (stained glass).
"""
from __future__ import annotations

import numpy as np

TL_A, TL_B = 342, 241
BR_A, BR_B = 405, 115
MAX_ID = 511


def check(i: int) -> int:
    return (i * 37 + 11) & 511


def _set_code(px: np.ndarray, code: int) -> None:
    """px: RGBA uint8 view of one texel; set the RGB low 3 bits to `code`."""
    bits = ((code >> 6) & 7, (code >> 3) & 7, code & 7)
    for k in range(3):
        px[k] = (int(px[k]) & ~7) | bits[k]


def stamp(img: np.ndarray, tex_id: int) -> np.ndarray:
    """Return a stamped copy of an RGBA uint8 image (h, w, 4). Needs w >= 4."""
    assert 1 <= tex_id <= MAX_ID
    out = img.copy()
    h, w = out.shape[:2]
    if w < 4:
        raise ValueError("texture narrower than 4 texels")
    for k, code in enumerate((TL_A, TL_B, tex_id, check(tex_id))):
        _set_code(out[0, k], code)
    for k, code in enumerate((BR_A, BR_B, tex_id, check(tex_id))):
        _set_code(out[h - 1, w - 4 + k], code)
        out[h - 1, w - 4 + k, 3] = 255
    return out


def codes(rgba: np.ndarray, bgra: bool = False) -> np.ndarray:
    """9-bit code of every texel of an (h, w, 4) uint8 array."""
    r = rgba[..., 2] if bgra else rgba[..., 0]
    g = rgba[..., 1]
    b = rgba[..., 0] if bgra else rgba[..., 2]
    return ((r.astype(np.int32) & 7) << 6) | ((g.astype(np.int32) & 7) << 3) | (b.astype(np.int32) & 7)


def find(rgba: np.ndarray, bgra: bool = False, slice_: int = 0) -> list[dict]:
    """Same search as Renderer Foundation's findStamps (row-major, skips 3 texels after a hit)."""
    c = codes(rgba, bgra)
    h, w = c.shape
    hits: list[dict] = []
    if w < 4:
        return hits
    a, b, i, k = c[:, :-3], c[:, 1:-2], c[:, 2:-1], c[:, 3:]
    chk = (i * 37 + 11) & 511
    tl = (a == TL_A) & (b == TL_B) & (k == chk)
    br = (a == BR_A) & (b == BR_B) & (k == chk)
    ys, xs = np.nonzero(tl | br)
    last_y, last_x = -1, -10
    for y, x in zip(ys.tolist(), xs.tolist()):
        if y == last_y and x <= last_x + 3:  # the C++ loop jumps 3 texels after a hit
            continue
        last_y, last_x = y, x
        hits.append({"id": int(i[y, x]), "x": x, "y": y, "slice": slice_, "corner": "br" if br[y, x] else "tl",
                     "alpha": [int(v) for v in rgba[y, x:x + 4, 3]]})
    return hits


def rects(hits: list[dict], ids: dict[int, dict]) -> dict[int, dict]:
    """Pair top-left / bottom-right hits into atlas rectangles per id. ids: id -> {name, w, h}."""
    out: dict[int, dict] = {}
    for hit in hits:
        e = out.setdefault(hit["id"], {"tl": [], "br": []})
        e[hit["corner"]].append((hit["x"], hit["y"], hit["slice"], hit["alpha"]))
    for tid, e in out.items():
        info = ids.get(tid, {})
        e["name"] = info.get("name", f"unknown id {tid}")
        found = []
        for (x0, y0, s0, _a) in e["tl"]:
            for (x1, y1, s1, _b) in e["br"]:
                w, h = x1 + 4 - x0, y1 + 1 - y0
                if s0 == s1 and w > 0 and h > 0 and (not info or (w == info["w"] and h == info["h"])):
                    found.append({"x": x0, "y": y0, "w": w, "h": h, "slice": s0})
        e["rects"] = found
        if not found and e["tl"] and info:  # hard stamp lost: the pack's own size still gives the rect
            e["rects_from_size"] = [{"x": x0, "y": y0, "w": info["w"], "h": info["h"], "slice": s0} for (x0, y0, s0, _a) in e["tl"]]
    return out
