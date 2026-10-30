#!/usr/bin/env python3
"""Write poses.json for preview.js: the converted entity at some times, Bedrock side and (optional) GeckoLib side.

    python -I tools/GeckoBridge/preview.py RP_DIR ENTITY_ID poses.json --times 0,1.5,3,4.5 [--jar mod.jar --src-anim path/in/jar]

Bedrock row: the client entity's "animate" list, every animation evaluated with Bedrock's linear/pre-post rules
on the CONVERTED keyframes and summed (Bedrock plays them concurrently). GeckoLib row: the SOURCE keyframes
evaluated with GeckoLib's easing maths (gbanim.gl_eval). Same picture = the bake is faithful. Constants only.
"""
import argparse, json, sys, zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import gbanim  # noqa: E402


def local_t(anim, t):
    L = anim.get("animation_length")
    if not L: return t
    return t % L if anim.get("loop") is True else min(t, L)


def add(pose, bone, ch, v):
    p = pose.setdefault(bone, {})
    key = {"rotation": "rot", "position": "pos", "scale": "scl"}[ch]
    if key == "scl": p[key] = [a * b for a, b in zip(p.get(key, [1, 1, 1]), v)]
    else: p[key] = [a + b for a, b in zip(p.get(key, [0, 0, 0]), v)]


def bedrock_pose(anims, play, t):
    pose = {}
    for name in play:
        a = anims[name]; lt = local_t(a, t)
        for bone, chans in a.get("bones", {}).items():
            for ch, val in chans.items():
                if ch not in ("rotation", "position", "scale"): continue
                add(pose, bone, ch, [gbanim.bedrock_eval(val, lt, ax) for ax in range(3)])
    return pose


def gecko_pose(src, names, t):
    pose = {}
    for n in names:
        a = src[n]; lt = local_t(a, t)
        for bone, chans in gbanim.parse_animation(a).items():
            for ch, (kfs, single) in chans.items():
                add(pose, bone, ch, [gbanim.gl_eval(kfs, lt, ax) for ax in range(3)])
    return pose


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("rp"); ap.add_argument("entity"); ap.add_argument("out")
    ap.add_argument("--times", default="0,1,2,3"); ap.add_argument("--jar"); ap.add_argument("--src-anim")
    ap.add_argument("--src-names", help="comma list of source animation names (same order as animate)")
    ap.add_argument("--view", default="0.55,-1"); ap.add_argument("--dist", type=float, default=70)
    ap.add_argument("--center", default="0,16,0"); ap.add_argument("--dark", action="store_true"); ap.add_argument("--title", default="")
    a = ap.parse_args()
    rp = Path(a.rp).resolve()
    ent = json.loads((rp / f"entity/{a.entity}.entity.json").read_text())["minecraft:client_entity"]["description"]
    geo_id = ent["geometry"]["default"]
    geo = next(g for f in (rp / "models/entity").glob("*.json") for g in json.loads(f.read_text())["minecraft:geometry"]
               if g["description"]["identifier"] == geo_id)
    anims = {}
    for f in (rp / "animations").glob("*.json"): anims.update(json.loads(f.read_text())["animations"])
    play = [ent["animations"][s] for s in ent["scripts"]["animate"]]
    times = [float(x) for x in a.times.split(",")]
    frames = [{"label": f"Bedrock t={t:g}s", "pose": bedrock_pose(anims, play, t)} for t in times]
    if a.jar:
        src = json.loads(zipfile.ZipFile(a.jar).read(a.src_anim).decode("utf-8-sig"))["animations"]
        frames += [{"label": f"GeckoLib t={t:g}s", "pose": gecko_pose(src, a.src_names.split(","), t)} for t in times]
    tex = rp / (ent["textures"]["default"] + ".png")
    glow = rp / (ent["textures"]["glow"] + ".png") if "glow" in ent["textures"] else None
    Path(a.out).write_text(json.dumps({"geo": geo, "texture": str(tex), "glow": str(glow) if glow else None,
                                       "frames": frames, "cols": len(times), "view": [float(x) for x in a.view.split(",")],
                                       "dist": a.dist, "center": [float(x) for x in a.center.split(",")], "dark": a.dark,
                                       "title": a.title}))
    print(f"{len(frames)} frames -> {a.out}")
