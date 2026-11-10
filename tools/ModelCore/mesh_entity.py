#!/usr/bin/env python3
"""ModelCore mesh_entity: an imported 3D model (mesh JSON) + baked textures -> private Bedrock RP + BP for one mob.

    python -I tools/ModelCore/mesh_entity.py MESH.json OUT --id purgatory:bon --name "Bon" --albedo bon_albedo.png
        [--mer bon_mer.png] [--icon icon.png] [--width 0.8] [--height 2.4] [--smooth 60] [--credits "..."]
        [--target-height 4]   (resize the model; set --height/--width for the hitbox to match)
        [--emit mask.png [--emit-gain 1.4]]   (glowing parts: fullbright via entity_emissive_alpha, Fancy + VV)

MESH.json is what an importer exports (tools for glTF/OBJ via Blender): {"resolution": [w, h],
"vertices": {id: [x, y, z]} in Blockbench pixel space (16 = 1 block, y up), "faces": {id: {"vertices": [...],
"uv": {vertex_id: [u, v]} in texels (v from the top), "texture": 0}}}. It is wrapped as a Blockbench .bbmodel
(free/mesh format) and converted by ModelCore like any other model -> Bedrock poly_mesh geometry, so the same
code path serves Blockbench, Figura, GeckoLib and imported models. The .bbmodel is written next to the packs so
the model can be opened and fixed in Blockbench.

The mob is a first-test statue: it renders, has a hitbox and a spawn egg, and does nothing else. Behaviour and
animation come from MobAI / GeckoBridge in later passes. Private personal use only (see CREDITS.txt).
"""
from __future__ import annotations
import argparse, json, shutil, sys, uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ModelCore.bbmodel import load_bbmodel          # noqa: E402
from ModelCore.rig import write_bedrock_geo         # noqa: E402


def u5(*parts):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "bedrockforge/mesh_entity/" + "/".join(parts)))


def to_bbmodel(mesh: dict, name: str, smooth: float | None) -> dict:
    tw, th = mesh["resolution"]
    xs, ys, zs = zip(*mesh["vertices"].values())
    span = max(max(xs) - min(xs), max(zs) - min(zs)) / 16.0
    height = (max(ys) - min(ys)) / 16.0
    el = {"type": "mesh", "uuid": f"{name}_mesh", "name": name, "origin": [0, 0, 0], "rotation": [0, 0, 0],
          "vertices": mesh["vertices"], "faces": mesh["faces"]}
    if smooth: el["bf_smooth_angle"] = float(smooth)
    return {"meta": {"format_version": "4.5", "model_format": "free", "box_uv": False}, "name": name,
            "resolution": {"width": tw, "height": th},
            # whole model must stay drawn when its origin (the feet) leaves the screen
            "visible_box": [round(span + 1.0, 2), round(height + 1.0, 2), round(min(ys) / 16.0 + height / 2, 2)],
            "elements": [el], "outliner": [{"name": name, "origin": [0, 0, 0], "uuid": f"{name}_grp", "children": [el["uuid"]]}],
            "textures": [{"name": name, "id": "0", "width": tw, "height": th, "uv_width": tw, "uv_height": th}]}


def write(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, (dict, list)): path.write_text(json.dumps(data, indent=1), encoding="utf-8")
    else: path.write_text(data, encoding="utf-8")


def build(a):
    ns, n = a.id.split(":")
    mesh = json.loads(Path(a.mesh).read_text(encoding="utf-8"))
    if a.target_height:  # resize the model itself (feet stay on the ground), so hitbox/bounds/geo all agree
        ys = [v[1] for v in mesh["vertices"].values()]
        k = a.target_height * 16.0 / (max(ys) - min(ys))
        mesh["vertices"] = {i: [v[0] * k, v[1] * k, v[2] * k] for i, v in mesh["vertices"].items()}
        print(f"scaled x{k:.3f} -> {a.target_height} blocks tall")
    out = Path(a.out); title = a.name
    rp, bp = out / f"{title} RP", out / f"{title} BP"
    for d in (rp, bp):
        if d.exists(): shutil.rmtree(d)

    bb = to_bbmodel(mesh, n, a.smooth)
    write(out / f"{n}.bbmodel", bb)
    rig = load_bbmodel(bb, name=n)
    rig.identifier = f"geometry.{ns}.{n}"
    geo = write_bedrock_geo([rig], "1.12.0")
    write(rp / f"models/entity/{ns}/{n}.geo.json", geo)

    tex = f"textures/entity/{ns}/{n}"
    (rp / tex).parent.mkdir(parents=True, exist_ok=True)
    if a.emit:
        # Glow, single pass: vanilla entity_emissive_alpha draws texels with alpha < 1 fullbright
        # (emissive = 1 - alpha). A second alpha-blend render controller does NOT draw on
        # poly_mesh entities in 1.26 / Renderer Foundation (tested in-game on bon), so the mask
        # goes into the base texture's alpha instead. VV also reads the _mer green channel.
        from PIL import Image, ImageFilter
        alb = Image.open(a.albedo).convert("RGB")
        mask = Image.open(a.emit).convert("L").resize(alb.size).filter(ImageFilter.MaxFilter(5))
        lit = alb.point(lambda v: min(255, int(v * a.emit_gain)))
        out_img = Image.composite(lit, alb, mask)
        out_img.putalpha(mask.point(lambda m: 255 - m))
        out_img.save(rp / f"{tex}.png")
    else:
        shutil.copyfile(a.albedo, rp / f"{tex}.png")
    if a.mer:
        shutil.copyfile(a.mer, rp / f"{tex}_mer.png")
        write(rp / f"{tex}.texture_set.json", {"format_version": "1.16.100", "minecraft:texture_set": {
            "color": n, "metalness_emissive_roughness": f"{n}_mer"}})

    rc = f"controller.render.{ns}.{n}"
    rcs = {rc: {"geometry": "Geometry.default", "materials": [{"*": "Material.default"}], "textures": ["Texture.default"]}}
    materials = {"default": "entity_emissive_alpha" if a.emit else "entity_alphatest"}
    textures, rc_list = {"default": tex}, [rc]
    write(rp / f"render_controllers/{ns}.{n}.rc.json", {"format_version": "1.8.0", "render_controllers": rcs})
    write(rp / f"entity/{ns}/{n}.entity.json", {"format_version": "1.10.0", "minecraft:client_entity": {"description": {
        "identifier": a.id, "materials": materials, "textures": textures,
        "geometry": {"default": rig.identifier}, "render_controllers": rc_list,
        "spawn_egg": {"base_color": "#2b3a4a", "overlay_color": "#8b1010"}}}})
    write(rp / "texts/languages.json", ["en_US"])
    write(rp / "texts/en_US.lang", f"entity.{a.id}.name={title}\nitem.spawn_egg.entity.{a.id}.name=Spawn {title}\n")

    write(bp / f"entities/{ns}/{n}.json", {"format_version": "1.21.0", "minecraft:entity": {
        "description": {"identifier": a.id, "is_spawnable": True, "is_summonable": True, "is_experimental": False},
        "components": {
            "minecraft:type_family": {"family": [ns, n, "monster", "mob"]},
            "minecraft:health": {"value": 100, "max": 100},
            "minecraft:collision_box": {"width": a.width, "height": a.height},
            "minecraft:physics": {}, "minecraft:persistent": {}, "minecraft:nameable": {},
            "minecraft:pushable": {"is_pushable": False, "is_pushable_by_piston": True},
            "minecraft:knockback_resistance": {"value": 1.0}}}})

    rp_id, bp_id, ver = u5(a.id, "rp"), u5(a.id, "bp"), [0, 1, 0]
    write(rp / "manifest.json", {"format_version": 2, "header": {"name": f"§c{title} §7(ModelCore test)", "description": f"{a.id} - mesh entity",
        "uuid": rp_id, "version": ver, "min_engine_version": [1, 21, 0]},
        "modules": [{"type": "resources", "uuid": u5(a.id, "rp_mod"), "version": ver}]})
    write(bp / "manifest.json", {"format_version": 2, "header": {"name": f"§c{title} §7(ModelCore test)", "description": f"{a.id} - mesh entity",
        "uuid": bp_id, "version": ver, "min_engine_version": [1, 21, 0]},
        "modules": [{"type": "data", "uuid": u5(a.id, "bp_mod"), "version": ver}],
        "dependencies": [{"uuid": rp_id, "version": ver}]})
    for d in (rp, bp):
        if a.icon: shutil.copyfile(a.icon, d / "pack_icon.png")
        if a.credits: write(d / "CREDITS.txt", a.credits + "\n\nPrivate personal use only. Not for redistribution.\n")
    pm = next(b.poly_mesh for b in rig.bones if b.poly_mesh)
    print(f"{a.id}: {len(pm['polys'])} polys, {len(pm['positions'])} positions, {len(pm['normals'])} normals, "
          f"bounds {rig.description_extra} -> {rp.name} + {bp.name}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mesh"); ap.add_argument("out")
    ap.add_argument("--id", required=True); ap.add_argument("--name", required=True)
    ap.add_argument("--albedo", required=True); ap.add_argument("--mer"); ap.add_argument("--icon")
    ap.add_argument("--width", type=float, default=0.8); ap.add_argument("--height", type=float, default=2.0)
    ap.add_argument("--smooth", type=float, default=60.0); ap.add_argument("--credits")
    ap.add_argument("--target-height", type=float, help="resize the model to this many blocks tall")
    ap.add_argument("--emit", help="greyscale emissive mask (white = glows); glowing texels drawn fullbright")
    ap.add_argument("--emit-gain", type=float, default=1.0, help="brighten the glowing pixels (e.g. 1.4)")
    build(ap.parse_args())
