#!/usr/bin/env python3
"""Contact sheet of every mob in a GeckoBridge RP (offline check before the game): default texture, posed by the
first frames of its controllers' initial-state animations (Bedrock rules, constants only).

    python -I tools/GeckoBridge/sheet.py "OUT/GB Mod RP" sheet.png   (needs THREE=/path/three.min.js, Node + Playwright)
"""
import json, os, re, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import preview, gbanim  # noqa: E402

rp = Path(sys.argv[1]).resolve(); out = Path(sys.argv[2]).resolve(); t = float(os.environ.get("T", "0.6"))
geos, anims, acs, rcs = {}, {}, {}, {}
for p in rp.rglob("*.json"):
    d = json.loads(p.read_text())
    if not isinstance(d, dict): continue
    for g in d.get("minecraft:geometry", []): geos[g["description"]["identifier"]] = g
    if "animations" in d and "minecraft:client_entity" not in d: anims.update(d["animations"])
    acs.update(d.get("animation_controllers", {})); rcs.update(d.get("render_controllers", {}))
tiles = []
tmpd = Path(tempfile.mkdtemp())
for p in sorted((rp / "entity").rglob("*.json")):
    d = json.loads(p.read_text())["minecraft:client_entity"]["description"]
    rc = rcs.get(d["render_controllers"][0], {})
    def last_key(expr, kind):
        ks = re.findall(kind + r"\.(\w+)", expr or ""); return ks[-1] if ks else None
    gk = last_key(rc.get("geometry"), "Geometry") or next(iter(d["geometry"])); tk = last_key((rc.get("textures") or [""])[0], "Texture") or next(iter(d["textures"]))
    geo = geos.get(d["geometry"][gk]); tex = rp / (d["textures"][tk] + ".png")
    play = []
    for s in d.get("scripts", {}).get("animate", []):
        a = d["animations"].get(s)
        if a and a in acs:
            st = acs[a]["states"][acs[a]["initial_state"]]
            play += [d["animations"][x] for x in st.get("animations", []) if d["animations"].get(x) in anims]
        elif a in anims: play.append(a)
    pose = preview.bedrock_pose(anims, play, t) if play else {}
    pose = {b: {k: [x if isinstance(x, (int, float)) else 0 for x in v] for k, v in ch.items()} for b, ch in pose.items()}
    # camera from the model's bounds
    ys = [c["origin"][1] + c["size"][1] for b in geo.get("bones", []) for c in b.get("cubes", [])] or [16]
    xs = [abs(c["origin"][i]) + c["size"][i] for b in geo.get("bones", []) for c in b.get("cubes", []) for i in (0, 2)] or [8]
    h = max(ys); wdt = max(xs)
    job = {"geo": geo, "texture": str(tex), "glow": None, "frames": [{"label": d["identifier"].split(":")[1][:26], "pose": pose}],
           "cols": 1, "view": [0.7, -1], "dist": max(h, wdt * 1.6) * 2.4 + 10, "center": [0, h / 2, 0]}
    jp = tmpd / (d["identifier"].replace(":", "_") + ".json"); jp.write_text(json.dumps(job)); tiles.append(jp)
pngs = []
for jp in tiles:
    png = jp.with_suffix(".png")
    subprocess.run(["node", str(Path(__file__).parent / "preview.js"), str(jp), str(png)], env=dict(os.environ, SIZE="200"), capture_output=True,
                   cwd=os.environ.get("NODE_DIR", "."))
    if png.exists(): pngs.append(png)
from PIL import Image
ims = [Image.open(p).convert("RGB") for p in pngs]
if not ims: sys.exit("no tiles rendered")
cols = 8; W, H = ims[0].size; sheet = Image.new("RGB", (cols * W, ((len(ims) + cols - 1) // cols) * H), (17, 17, 17))
for i, im in enumerate(ims): sheet.paste(im, ((i % cols) * W, (i // cols) * H))
sheet.save(out); print(f"{len(ims)} mobs -> {out}")
