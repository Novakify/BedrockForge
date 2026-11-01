#!/usr/bin/env python3
"""FiguraBridge F1 checks for ModelCore/bbmodel.py. python -I tools/ModelCore/test_bbmodel.py [avatar_dir_or.bbmodel ...]
Built-in fixtures: a Blockbench 4 file (inline outliner groups) and the same model as Blockbench 5 (top-level groups)
must give identical rigs; expected numbers are worked out by hand from Blockbench's Bedrock exporter rules.
Extra args: every .bbmodel found is loaded, converted to Bedrock geo and round-tripped (smoke test on real avatars)."""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ModelCore.bbmodel import load_bbmodel, figura_parent_type
from ModelCore.rig import write_bedrock_geo, roundtrip_equal

fails = 0
def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += 0 if cond else 1

cube = {"uuid": "c1", "type": "cube", "name": "skull", "from": [-4, 24, -4], "to": [4, 32, 4], "origin": [0, 24, 0],
        "rotation": [10, 20, 0], "inflate": 0.5, "box_uv": False,
        "faces": {"north": {"uv": [8, 8, 16, 16], "texture": 0}, "up": {"uv": [8, 0, 16, 8], "texture": 0, "rotation": 90},
                  "down": {"uv": [16, 8, 24, 0], "texture": 0}, "east": {"uv": [0, 8, 8, 16], "texture": None}}}
boxcube = {"uuid": "c2", "type": "cube", "name": "arm", "from": [4, 12, -2], "to": [8, 24, 2], "origin": [5, 22, 0],
           "box_uv": True, "uv_offset": [40, 16], "mirror_uv": True}
mesh = {"uuid": "m1", "type": "mesh", "name": "plane", "origin": [0, 30, 0], "rotation": [0, 0, 0],
        "vertices": {"a": [-2, 0, -2], "b": [2, 0, -2], "c": [2, 0, 2], "d": [-2, 0, 2], "e": [0, 3, 0]},
        "faces": {"q": {"vertices": ["a", "b", "c", "d"], "uv": {"a": [0, 0], "b": [4, 0], "c": [4, 4], "d": [0, 4]}, "texture": 0},
                  "t": {"vertices": ["a", "b", "e"], "uv": {"a": [0, 0], "b": [4, 0], "e": [2, 3]}, "texture": 0}}}
loc = {"uuid": "l1", "type": "locator", "name": "hat_point", "position": [1, 33, 0]}
base = {"meta": {"format_version": "4.10", "model_format": "free", "box_uv": False},
        "resolution": {"width": 64, "height": 64}, "elements": [cube, boxcube, mesh, loc],
        "textures": [{"name": "skin.png", "id": "0", "uuid": "t0", "width": 64, "height": 64}],
        "animations": [{"name": "wave", "loop": "loop", "length": 1.0, "animators": {}}]}
v4 = {**base, "outliner": [{"name": "Head", "uuid": "g1", "origin": [0, 24, 0], "rotation": [0, 45, 0],
                            "children": ["c1", "m1", "l1", {"name": "RightArmThing", "uuid": "g2", "origin": [5, 22, 0], "children": ["c2"]}]}]}
v5 = {**base, "meta": {**base["meta"], "format_version": "5.0"},
      "groups": [{"name": "Head", "uuid": "g1", "origin": [0, 24, 0], "rotation": [0, 45, 0]},
                 {"name": "RightArmThing", "uuid": "g2", "origin": [5, 22, 0]}],
      "outliner": [{"uuid": "g1", "children": ["c1", "m1", "l1", {"uuid": "g2", "children": ["c2"]}]}]}

r4, r5 = load_bbmodel(v4, "test"), load_bbmodel(v5, "test")
g4, g5 = write_bedrock_geo([r4]), write_bedrock_geo([r5])
check(json.dumps(g4, sort_keys=True) == json.dumps(g5, sort_keys=True), "Blockbench 4 and 5 layouts give the same rig")
h, a = r4.bone("Head"), r4.bone("RightArmThing")
check(h and a and a.parent == "Head", "group tree: RightArmThing under Head")
check(h.pivot == [0.0, 24.0, 0.0] and h.rotation == [0.0, -45.0, 0.0], f"bone pivot/rotation Bedrock signs {h.pivot} {h.rotation}")
check(a.pivot == [-5.0, 22.0, 0.0], f"bone pivot x negated {a.pivot}")
c = h.cubes[0]
check(c.origin == [-4.0, 24.0, -4.0] and c.size == [8.0, 8.0, 8.0], f"cube origin = [-to.x, from.y, from.z] {c.origin} {c.size}")
check(c.inflate == 0.5 and c.rotation == [-10.0, -20.0, 0.0] and c.pivot == [0.0, 24.0, 0.0], "cube inflate/rotation/pivot")
check(c.uv["north"] == {"uv": [8.0, 8.0], "uv_size": [8.0, 8.0]}, f"side face uv {c.uv['north']}")
check(c.uv["up"] == {"uv": [16.0, 8.0], "uv_size": [-8.0, -8.0], "uv_rotation": 90}, f"up face flipped + rotation {c.uv['up']}")
check(c.uv["down"] == {"uv": [24.0, 0.0], "uv_size": [-8.0, 8.0]}, f"down face flipped {c.uv['down']}")
check("east" not in c.uv, "face with no texture dropped")
bc = a.cubes[0]
check(bc.uv == [40.0, 16.0] and bc.mirror is True and bc.origin == [-8.0, 12.0, -2.0], f"box uv cube {bc.uv} {bc.origin}")
pm = h.poly_mesh
check(pm and len(pm["positions"]) == 5 and len(pm["polys"]) == 2, "mesh -> poly_mesh (5 verts, 2 polys)")
check([2.0, 30.0, -2.0] in pm["positions"] and [0.0, 33.0, 0.0] in pm["positions"], "mesh vertices in model space, x negated")
check(all(len(p) == 4 for p in pm["polys"]) and pm["polys"][1][3] == pm["polys"][1][2], "triangle padded to quad_list")
check(all(0 <= u <= 1 and 0 <= v <= 1 for u, v in pm["uvs"]), "mesh uvs normalized")
check(h.locators == {"hat_point": [-1.0, 33.0, 0.0]}, f"locator {h.locators}")
check(h.extra["figura_parent"] == "Head" and a.extra["figura_parent"] == "RightArm", "Figura ParentType from group names")
check(figura_parent_type("HeadsUpDisplayThing") == "Head" and figura_parent_type("Hud") == "Hud"
      and figura_parent_type("CameraRig") == "Camera" and figura_parent_type("hat") == "None"
      and figura_parent_type("LEFT_ELYTRON_x") == "LeftElytra", "ParentType prefix rules (Figura quirks kept)")
check(r4.source_meta["animations"][0]["name"] == "wave", "animations kept raw for F2")
check(roundtrip_equal(g4), "Bedrock geo from a .bbmodel round-trips through rig.py")

for arg in sys.argv[1:]:
    p = Path(arg)
    for f in ([p] if p.is_file() else sorted(p.rglob("*.bbmodel"))):
        try:
            r = load_bbmodel(json.loads(f.read_text(encoding="utf-8-sig")), f.stem)
            check(roundtrip_equal(write_bedrock_geo([r])), f"{f}: {r.stats()}")
        except Exception as e:
            check(False, f"{f}: {type(e).__name__}: {e}")
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
