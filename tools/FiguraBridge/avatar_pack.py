#!/usr/bin/env python3
"""FiguraBridge F1-lite: a Figura avatar folder -> Bedrock resource pack that makes it YOUR player model.

    python -I tools/FiguraBridge/avatar_pack.py AVATAR_DIR OUT_RP --vanilla BEDROCK_SAMPLES/resource_pack [--models main]

What it does (no Lua yet - that is F3 in the native runtime, see docs/PORT_FIGURA.md):
- loads the avatar's .bbmodel(s) with ModelCore (tools/ModelCore/bbmodel.py),
- builds one Bedrock geometry = vanilla player skeleton (geometry.humanoid.custom bones, NO cubes = vanilla
  player hidden, like Figura's "Hide Vanilla Player") + the avatar groups parented by Figura's ParentType:
  Head -> head, Body -> body, arms/legs -> arms/legs, Cape -> cape, root "None" groups -> root.
  So Bedrock's own player animations (walk, swing, sneak, look) move the avatar like Figura follows vanilla.
- non-body parent types (Hud, Gui, Camera, World, Skull, Portrait, Item/Arrow/Trident, *Pivot, Elytra) and
  groups/cubes hidden in Blockbench are left out (scripts toggle those; no scripts yet).
- overrides player.entity.json (copied from Mojang's bedrock-samples) with geometry.figura/texture.figura and
  first/third person render controllers that use them. Spectator + map views stay vanilla.
Personal/single-player test pack. Avatars belong to their makers: keep their LICENSE next to the pack.
"""
from __future__ import annotations
import argparse, base64, json, shutil, sys, uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ModelCore.bbmodel import load_bbmodel, BEDROCK_PLAYER_BONE  # noqa: E402
from ModelCore.rig import Bone, write_bedrock_geo  # noqa: E402

# geometry.humanoid.custom skeleton (bedrock-samples models/mobs.json): name, parent, pivot
SKELETON = [("root", None, [0, 0, 0]), ("waist", "root", [0, 12, 0]), ("body", "waist", [0, 24, 0]),
            ("head", "body", [0, 24, 0]), ("hat", "head", [0, 24, 0]), ("cape", "body", [0, 24, 3]),
            ("leftArm", "body", [5, 22, 0]), ("leftSleeve", "leftArm", [5, 22, 0]), ("leftItem", "leftArm", [6, 15, 1]),
            ("rightArm", "body", [-5, 22, 0]), ("rightSleeve", "rightArm", [-5, 22, 0]), ("rightItem", "rightArm", [-6, 15, 1]),
            ("leftLeg", "root", [1.9, 12, 0]), ("leftPants", "leftLeg", [1.9, 12, 0]),
            ("rightLeg", "root", [-1.9, 12, 0]), ("rightPants", "rightLeg", [-1.9, 12, 0]), ("jacket", "body", [0, 24, 0])]
FOLLOW = {**BEDROCK_PLAYER_BONE, "None": "root", "Model": "root"}

FP_RIGHT = "query.get_equipped_item_name(0, 1) == '' || query.get_equipped_item_name(0, 1) == 'filled_map'"
FP_LEFT = ("(query.get_equipped_item_name(0, 1) == 'filled_map' && query.get_equipped_item_name('off_hand') != 'shield') || "
           "(query.get_equipped_item_name('off_hand') == 'filled_map' && !query.item_is_charged) || "
           "(!query.item_is_charged && (variable.item_use_normalized > 0 && variable.item_use_normalized < 1.0))")


def texture_bytes(bb_json: dict, bb_path: Path, avatar_dir: Path):
    for t in bb_json.get("textures", []):
        src = t.get("source", "")
        if src.startswith("data:image/png;base64,"):
            return base64.b64decode(src.split(",", 1)[1]), t.get("name", "texture.png")
        for cand in (t.get("relative_path"), t.get("path"), t.get("name")):
            if not cand: continue
            for base in (bb_path.parent, avatar_dir):
                p = (base / cand)
                if p.is_file(): return p.read_bytes(), p.name
    return None, None


def build(avatar_dir: Path, out: Path, vanilla_rp: Path, models: list[str] | None):
    name = avatar_dir.name
    slug = "".join(c.lower() if c.isalnum() else "_" for c in name).strip("_")
    bbs = sorted(avatar_dir.rglob("*.bbmodel"))
    bbs = [b for b in bbs if not any(p.startswith(".") for p in b.relative_to(avatar_dir).parts)]
    if models: bbs = [b for b in bbs if b.stem in models]
    if not bbs: sys.exit(f"no .bbmodel in {avatar_dir}")

    bones = [Bone(name=n, parent=p, pivot=[float(x) for x in pv]) for n, p, pv in SKELETON]
    report = {"avatar": name, "models": [], "skipped_groups": [], "warnings": []}
    tex, tex_name, tw, th = None, None, 64, 64
    for bb in bbs:
        data = json.loads(bb.read_text(encoding="utf-8-sig"))
        rig = load_bbmodel(data, bb.stem)
        if tex is None:
            tex, tex_name = texture_bytes(data, bb, avatar_dir); tw, th = rig.texture_width, rig.texture_height
        elif (rig.texture_width, rig.texture_height) != (tw, th):
            report["warnings"].append(f"{bb.name}: resolution {rig.texture_width}x{rig.texture_height} != {tw}x{th}")
        if len(data.get("textures", [])) > 1:
            report["warnings"].append(f"{bb.name}: {len(data['textures'])} textures, only the first is used (one texture per entity for now)")
        by = {b.name: b for b in rig.bones}
        drop = set()
        for b in rig.bones:  # drop hidden groups, non-body parent types (with their subtree)
            par = by.get(b.parent)
            if par and par.name in drop: drop.add(b.name); continue
            pt = b.extra.get("figura_parent", "None")
            top_typed = par is None or par.extra.get("figura_parent") in (None, "None", "Model")
            if b.extra.get("bb_visible") is False or (top_typed and pt not in FOLLOW and pt != "None"):
                drop.add(b.name); report["skipped_groups"].append(f"{bb.stem}:{b.name} ({pt})")
        kept = 0
        for b in rig.bones:
            if b.name in drop: continue
            nb = Bone(name=f"fig_{bb.stem}_{b.name}", pivot=b.pivot, rotation=b.rotation, locators=b.locators, poly_mesh=b.poly_mesh,
                      cubes=[c for c in b.cubes if c.extra.get("bb_visible") is not False])
            for c in nb.cubes: c.extra = {}
            if b.parent and b.parent in by:
                nb.parent = f"fig_{bb.stem}_{b.parent}"
            else:
                nb.parent = FOLLOW.get(b.extra.get("figura_parent", "None"), "root")
            bones.append(nb); kept += len(nb.cubes)
        report["models"].append({"file": str(bb.relative_to(avatar_dir)), **rig.stats(), "cubes_kept": kept})

    if tex is None: sys.exit("no texture found in the .bbmodel (embedded or next to it)")
    geo_id = f"geometry.figura.{slug}"
    from ModelCore.rig import Rig
    geo = write_bedrock_geo([Rig(identifier=geo_id, texture_width=tw, texture_height=th, bones=bones,
                                 description_extra={"visible_bounds_width": 3, "visible_bounds_height": 4, "visible_bounds_offset": [0, 1.5, 0]})],
                            "1.21.0")

    if out.exists(): shutil.rmtree(out)
    for d in ("models/entity", "textures/figura", "entity", "render_controllers"): (out / d).mkdir(parents=True)
    (out / f"models/entity/figura_{slug}.geo.json").write_text(json.dumps(geo, indent=1))
    (out / f"textures/figura/{slug}.png").write_bytes(tex)

    ent = json.loads((vanilla_rp / "entity/player.entity.json").read_text())
    d = ent["minecraft:client_entity"]["description"]
    d["geometry"]["figura"] = geo_id
    d["textures"]["figura"] = f"textures/figura/{slug}"
    rc = []
    for r in d["render_controllers"]:
        k = next(iter(r))
        rc.append({k.replace("controller.render.player.", "controller.render.player.figura_"): r[k]}
                  if k in ("controller.render.player.first_person", "controller.render.player.third_person") else r)
    d["render_controllers"] = rc
    (out / "entity/player.entity.json").write_text(json.dumps(ent, indent=1))

    def subtree(root):
        s, kids = [], {}
        for b in bones: kids.setdefault(b.parent, []).append(b.name)
        st = [root]
        while st:
            n = st.pop(); s.append(n); st += kids.get(n, [])
        return s
    fp_vis = [{"*": False}] + [{n: FP_RIGHT} for n in subtree("rightArm")] + [{n: FP_LEFT} for n in subtree("leftArm")]
    controllers = {"format_version": "1.8.0", "render_controllers": {
        "controller.render.player.figura_third_person": {"geometry": "Geometry.figura", "materials": [{"*": "Material.default"}],
                                                         "textures": ["Texture.figura"], "part_visibility": [{"*": True}]},
        "controller.render.player.figura_first_person": {"geometry": "Geometry.figura", "materials": [{"*": "Material.default"}],
                                                         "textures": ["Texture.figura"], "part_visibility": fp_vis}}}
    (out / "render_controllers/figura_player.render_controllers.json").write_text(json.dumps(controllers, indent=1))

    ns = uuid.UUID("6f1b0e1e-4c1a-4f0b-9d1e-f16a00b1d6e0")  # stable UUIDs per avatar -> reinstall replaces, no duplicates
    manifest = {"format_version": 2, "header": {"name": f"FiguraBridge: {name}", "description": "Figura avatar as the player model (FiguraBridge F1 test, private use)",
                                                "uuid": str(uuid.uuid5(ns, slug + ":header")), "version": [0, 1, 0], "min_engine_version": [1, 21, 0]},
                "modules": [{"type": "resources", "uuid": str(uuid.uuid5(ns, slug + ":module")), "version": [0, 1, 0]}]}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))
    lic = avatar_dir / "LICENSE"
    if lic.is_file(): shutil.copy(lic, out / "AVATAR_LICENSE.txt")
    report["geometry"] = geo_id; report["texture"] = tex_name; report["bones_total"] = len(bones)
    (out / "figurabridge_report.json").write_text(json.dumps(report, indent=1))
    return report


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("avatar"); ap.add_argument("out"); ap.add_argument("--vanilla", required=True)
    ap.add_argument("--models", help="comma list of .bbmodel stems to include (default: all)")
    a = ap.parse_args()
    r = build(Path(a.avatar).resolve(), Path(a.out).resolve(), Path(a.vanilla).resolve(), a.models.split(",") if a.models else None)
    print(json.dumps(r, indent=1))
