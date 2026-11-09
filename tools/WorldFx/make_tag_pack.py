#!/usr/bin/env python3
"""Build the World FX tag test resource pack (packs/WorldFxTagTest).

Vanilla block textures (Mojang bedrock-samples) with invisible WFX1 stamps (wfx_stamps.py) so Renderer Foundation's
World FX probe can find each of them in the game's terrain atlas. Looks exactly like vanilla in game.

v0.2 adds a second experiment: ALPHA TAGS. Team concrete gets its alpha channel set to 224..239 (one value per
colour). Opaque blocks ignore alpha, so it should look unchanged - and if the atlas keeps the value, a block shader
can read "what am I" straight from the texture it already samples (no atlas lookup table), and write it into the
frame's unused alpha channel for the screen pass. Team wool stays untouched as the control group.

    python3 -I tools/WorldFx/make_tag_pack.py                 # fetches bedrock-samples into ~/.cache if needed
    python3 -I tools/WorldFx/make_tag_pack.py --vanilla <bedrock-samples>/resource_pack/textures/blocks

Writes packs/WorldFxTagTest/ and tools/WorldFx/tag_ids.json (id -> texture, size, group; read by analyze_probe.py).
Pack UUIDs live in tools/WorldFx/pack_uuids.json so rebuilds update the same pack.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import uuid
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import wfx_stamps  # noqa: E402

ROOT = HERE.parent.parent
PACK = ROOT / "packs" / "WorldFxTagTest"
COLOURS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "silver", "cyan", "purple",
           "blue", "brown", "green", "red", "black"]

# group -> texture names (Bedrock texture file names under textures/blocks)
ALPHA_TAG_GROUP = "team_concrete"  # alpha = ALPHA_TAG_BASE + index in COLOURS
ALPHA_TAG_BASE = 224

GROUPS: dict[str, list[str]] = {
    "glass": ["glass"] + [f"glass_{c}" for c in COLOURS] + ["tinted_glass"],
    "glass_pane": ["glass_pane_top"] + [f"glass_pane_top_{c}" for c in COLOURS],
    "team_wool": [f"wool_colored_{c}" for c in COLOURS],
    "team_concrete": [f"concrete_{c}" for c in COLOURS],
    # Bed Wars / Sky Wars staples: bridge planks, end stone defence, obsidian, generator blocks
    "hive_blocks": ["planks_oak", "end_stone", "obsidian", "diamond_block", "emerald_block"],
}


def fetch_vanilla() -> Path:
    dst = Path.home() / ".cache" / "worldfx" / "bedrock-samples"
    blocks = dst / "resource_pack" / "textures" / "blocks"
    if (blocks / "glass.png").exists() and (blocks / "emerald_block.png").exists():
        return blocks
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--filter=blob:none", "--sparse",
                        "https://github.com/Mojang/bedrock-samples", str(dst)], check=True)
    pats = ["resource_pack/textures/blocks/" + p for p in
            ["glass*", "tinted_glass*", "wool_colored_*", "concrete_*", "planks_oak*", "end_stone*", "obsidian*",
             "diamond_block*", "emerald_block*"]]
    subprocess.run(["git", "-C", str(dst), "sparse-checkout", "set", "--no-cone", *pats], check=True)
    return blocks


def load_uuids() -> dict:
    f = HERE / "pack_uuids.json"
    if f.exists():
        return json.loads(f.read_text())
    u = {"header": str(uuid.uuid4()), "module": str(uuid.uuid4())}
    f.write_text(json.dumps(u, indent=2) + "\n")
    return u


def icon() -> Image.Image:
    """Frosted glass tile, neon purple + white outline (the Bedrock Forge client look)."""
    s = 256
    im = Image.new("RGBA", (s, s), (24, 14, 40, 255))
    d = ImageDraw.Draw(im)
    for y in range(s):  # soft violet -> deep blue gradient
        t = y / (s - 1)
        d.line([(0, y), (s, y)], fill=(int(70 - 40 * t), int(40 - 20 * t), int(110 + 30 * t), 255))
    glass = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    g = ImageDraw.Draw(glass)
    g.rounded_rectangle([40, 40, 216, 216], radius=26, fill=(210, 225, 255, 70))
    g.line([(70, 92), (110, 52)], fill=(255, 255, 255, 150), width=7)   # glass streaks
    g.line([(70, 124), (142, 52)], fill=(255, 255, 255, 90), width=5)
    g.line([(150, 204), (204, 150)], fill=(255, 255, 255, 110), width=6)
    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rounded_rectangle([40, 40, 216, 216], radius=26, outline=(190, 80, 255, 255), width=10)
    glow = glow.filter(ImageFilter.GaussianBlur(9))
    im = Image.alpha_composite(im, glow)
    im = Image.alpha_composite(im, glass)
    ImageDraw.Draw(im).rounded_rectangle([40, 40, 216, 216], radius=26, outline=(245, 235, 255, 255), width=4)
    return im.convert("RGB")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vanilla", type=Path, help="bedrock-samples resource_pack/textures/blocks")
    ap.add_argument("--out", type=Path, default=PACK)
    a = ap.parse_args()
    van = a.vanilla or fetch_vanilla()
    out = a.out
    (out / "textures" / "blocks").mkdir(parents=True, exist_ok=True)

    ids: dict[str, dict] = {}
    next_id = 1
    worst = 0
    for group, names in GROUPS.items():
        for name in names:
            src = van / f"{name}.png"
            if not src.exists():
                print(f"missing vanilla texture {src}", file=sys.stderr)
                return 1
            img = np.array(Image.open(src).convert("RGBA"))
            tagged = img.copy()
            alpha_tag = None
            if group == ALPHA_TAG_GROUP:
                if img[..., 3].min() != 255:
                    print(f"{name}: not fully opaque in vanilla, cannot carry an alpha tag", file=sys.stderr)
                    return 1
                alpha_tag = ALPHA_TAG_BASE + names.index(name)
                tagged[..., 3] = alpha_tag
            st = wfx_stamps.stamp(tagged, next_id)
            diff = np.abs(st[..., :3].astype(int) - img[..., :3].astype(int)).max()
            worst = max(worst, int(diff))
            Image.fromarray(st, "RGBA").save(out / "textures" / "blocks" / f"{name}.png", optimize=True)
            ids[str(next_id)] = {"name": name, "group": group, "w": int(img.shape[1]), "h": int(img.shape[0]),
                                 "min_alpha": int(tagged[..., 3].min()), "max_alpha": int(tagged[..., 3].max()),
                                 "alpha_tag": alpha_tag}
            next_id += 1

    u = load_uuids()
    manifest = {
        "format_version": 2,
        "header": {
            "name": "World FX Tag Test",
            "description": "by Novak - Bedrock Forge World FX test pack (vanilla look, invisible texture tags)",
            "uuid": u["header"],
            "version": [0, 2, 0],
            "min_engine_version": [1, 21, 0],
        },
        "modules": [{"type": "resources", "uuid": u["module"], "version": [0, 2, 0]}],
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    icon().save(out / "pack_icon.png")
    (HERE / "tag_ids.json").write_text(json.dumps({"stamp": "WFX1", "pack": "WorldFxTagTest", "ids": ids}, indent=1) + "\n")
    print(f"{len(ids)} textures stamped -> {out} (largest colour change {worst}/255), ids -> {HERE / 'tag_ids.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
