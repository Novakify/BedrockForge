#!/usr/bin/env python3
"""Re-tag an atmosphere pack's block textures with the current material ids (make_material_pack.CLASSES), in place.

    python3 -I tools/WorldFx/atmos/retag_living.py packs/WorldFxCloudSea

Opaque textures get alpha = 255 - 4 * id everywhere; cut-out classes (leaves, plants, flowers, lamps) only on their
opaque texels. Only touches textures whose class is known; prints what changed. (Faster than rebuilding the whole
pack and leaves its animated flipbooks alone.)
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from make_material_pack import CLASSES, CUTOUT, STEP, classify  # noqa: E402

pack = Path(sys.argv[1])
tex = pack / "textures" / "blocks"
changed = {}
for f in sorted(tex.rglob("*")):
    if f.suffix.lower() not in (".png", ".tga") or f.stem.endswith(("_mers", "_mer", "_normal", "_heightmap")):
        continue
    rel = f.relative_to(tex).with_suffix("").as_posix()
    cid = classify(rel)
    if not cid:
        continue
    im = Image.open(f)
    rgba = np.array(im.convert("RGBA"))
    a = rgba[..., 3]
    want = 255 - STEP * cid
    if CLASSES[cid - 1][1] in CUTOUT:
        if ((a > 0) & (a < 128)).any() and a.max() < 255 and (a != want).any() and not ((a == 0) | (a >= 128)).all():
            continue                     # soft-edged art: leave it
        new = np.where(a >= 128, want, 0).astype(np.uint8)
    else:
        if a.min() < 128 and cid not in (20,):
            continue                     # has holes: not an opaque block texture
        new = np.full_like(a, want)
    if (new != a).any():
        rgba[..., 3] = new
        Image.fromarray(rgba, "RGBA").save(f)
        changed.setdefault(CLASSES[cid - 1][1], []).append(f.name)
for k, v in changed.items():
    print(f"{k:8s} {len(v):3d}  {', '.join(v[:8])}{' ...' if len(v) > 8 else ''}")
