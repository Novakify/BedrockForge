"""3D held items (GeoItemRenderer / AzItemRenderer, item model parent "builtin/entity") -> Bedrock attachables.

Library-agnostic: an item whose Java model is builtin/entity and that has a Bedrock geo with its name
(geo/<id>, geo/item/<id>, or the id without a _material suffix) is a 3D item. The geometry gets a root bone
bound to the holding hand (q.item_slot_to_bone_name), and the Java display transforms (thirdperson/firstperson
righthand) become two looping animations on that root. The inventory icon is rendered from the model with
the Java "gui" display angle (preview.js), so the item isn't an empty square.
"""
import json
import struct
import subprocess
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
THREE = "/home/claude/tools/node/node_modules/three/build/three.min.js"


def _png_size(b): return struct.unpack(">II", b[16:24])


def find_geo(b, iid):
    cands = [iid]
    if "_" in iid: cands.append(iid.rsplit("_", 1)[0])
    for c in cands:
        for d in ("geo/", "geo/item/", "geo/items/"):
            n = f"assets/{b.modid}/{d}{c}.geo.json"
            if n in b.names: return n, c
    return None, None


def find_tex(b, iid, base, size):
    for c in (iid, base):
        for d in ("item/", "items/", "weapon/", "weapons/", "item/geo/", "geo/", "entity/item/", "tools/"):
            n = f"assets/{b.modid}/textures/{d}{c}.png"
            if n in b.names and (size is None or tuple(_png_size(b.z.read(n))) == tuple(size)): return n
    return None


def anim_for(disp, kind):
    """Java display transform (degrees, 1/16-block translation, scale) -> Bedrock bone rotation/position/scale.
    Bedrock bones: x mirrored, so x translation and y/z rotations flip sign (same rule as the geometry converter)."""
    d = disp.get(kind) or {}
    r = d.get("rotation", [0, 0, 0]); t = d.get("translation", [0, 0, 0]); s = d.get("scale", [1, 1, 1])
    bone = {"rotation": [r[0], -r[1], -r[2]], "position": [-t[0], t[1], t[2]], "scale": s}
    return {"loop": True, "bones": {"gb_item": bone}}


def render_icon(geo_bones, desc, tex_path, out_png, gui):
    """Icon from the model with the Java gui angle, via preview.js (headless three.js)."""
    import tempfile
    job = {"geo": {"description": desc, "bones": geo_bones}, "texture": tex_path, "glow": None,
           "frames": [{"label": "", "pose": {}}], "cols": 1,
           "view": [0, 1], "dist": 60, "center": [0, 0, 0], "dark": False, "title": "", "icon": True, "gui": gui or {}}
    with tempfile.TemporaryDirectory() as td:
        jp = Path(td) / "job.json"; jp.write_text(json.dumps(job))
        env = {**os.environ, "THREE": THREE, "NODE_PATH": "/opt/npm-tools/node_modules", "SIZE": "64"}
        r = subprocess.run(["node", str(HERE / "preview.js"), str(jp), str(out_png)], env=env, capture_output=True, text=True, timeout=120)
        return r.returncode == 0 and Path(out_png).exists()


def convert(b):
    md = f"assets/{b.modid}/models/item/"
    done = 0
    for n in sorted(x for x in b.names if x.startswith(md) and x.endswith(".json")):
        iid = Path(n).stem
        try: m = json.loads(b.z.read(n).decode("utf-8-sig"))
        except Exception: continue
        if m.get("parent") != "builtin/entity": continue
        gpath, base = find_geo(b, iid)
        if not gpath: continue
        geo = json.loads(b.z.read(gpath).decode("utf-8-sig"))
        g = geo["minecraft:geometry"][0]
        tw, th = g["description"].get("texture_width", 16), g["description"].get("texture_height", 16)
        tsrc = find_tex(b, iid, base, (tw, th))
        if not tsrc: b.report["warnings"].append(f"3D item {iid}: no texture"); continue
        tex = f"textures/items/{b.ns}/3d/{iid}"
        b.write(b.rp, tex + ".png", b.z.read(tsrc))
        gid = f"geometry.gb.{b.ns}.item.{iid}"
        bones = []
        for bn in g.get("bones", []):
            nb = dict(bn)
            if not nb.get("parent"): nb["parent"] = "gb_item"
            bones.append(nb)
        bones.insert(0, {"name": "gb_item", "pivot": [0, 0, 0], "binding": "q.item_slot_to_bone_name(c.item_slot)"})
        desc = {**g["description"], "identifier": gid}
        b.write(b.rp, f"models/entity/{b.ns}/items/{iid}.geo.json", {"format_version": "1.16.0", "minecraft:geometry": [{"description": desc, "bones": bones}]})
        disp = m.get("display") or {}
        a1, a3 = f"animation.gb.{b.ns}.item.{iid}.first_person", f"animation.gb.{b.ns}.item.{iid}.third_person"
        b.write(b.rp, f"animations/{b.ns}/items/{iid}.animation.json", {"format_version": "1.8.0", "animations": {
            a1: anim_for(disp, "firstperson_righthand"), a3: anim_for(disp, "thirdperson_righthand")}})
        b.write(b.rp, f"attachables/{b.ns}/{iid}.json", {"format_version": "1.10.0", "minecraft:attachable": {"description": {
            "identifier": f"{b.ns}:{iid}", "materials": {"default": "entity_alphatest", "enchanted": "entity_alphatest_glint"},
            "textures": {"default": tex, "enchanted": "textures/misc/enchanted_item_glint"}, "geometry": {"default": gid},
            "animations": {"first": a1, "third": a3},
            "scripts": {"animate": [{"first": "c.is_first_person"}, {"third": "!c.is_first_person"}]},
            "render_controllers": ["controller.render.item_default"]}}})
        # icon: render the model (gui display angle) unless the item already has a 2D icon
        icon_key = f"{b.ns}_{iid}"
        ip = b.rp / f"textures/items/{b.ns}/{iid}.png"
        if not ip.exists():
            ip.parent.mkdir(parents=True, exist_ok=True)
            src_png = b.rp / (tex + ".png")
            if render_icon(bones[1:], desc, str(src_png), ip, disp.get("gui")):
                b.item_tex[icon_key] = {"textures": f"textures/items/{b.ns}/{iid}"}
        done += 1
    b.report["items_3d"] = done
    return done
