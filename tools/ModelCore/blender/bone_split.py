"""Add bones to a baked ModelCore mesh JSON, from the source glTF's armature (rigid skinning).

    python bone_split.py SOURCE.glb MESH.json OUT_MESH.json [--keep Bone,Bone,...] [--merge Child=Parent,...]

Every baked vertex takes the dominant vertex-group bone of its nearest source vertex, each face takes the
majority bone of its corners, and small bones are folded into the nearest kept ancestor. The output mesh JSON
gets "bones": {name: {"parent", "pivot"}} (pixel space, like the vertices) and "bone" on every face, which
mesh_entity.py turns into one poly_mesh per Bedrock bone, ready for keyframe animation. Animatronics and
robots look right with rigid parts; organic mobs will show creases at joints.
Runs with the bpy module (pip install bpy) or inside Blender: blender -b -P bone_split.py -- ARGS
"""
import argparse, json, sys
from collections import Counter

import bpy
from mathutils import kdtree

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("glb"); ap.add_argument("mesh"); ap.add_argument("out")
ap.add_argument("--keep", help="comma list of bones to keep (default: all deforming bones)")
ap.add_argument("--min-faces", type=int, default=12, help="bones with fewer faces fold into their parent")
a = ap.parse_args(argv)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=a.glb)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.vertex_groups and o.find_armature() == arm]

px = lambda v: [round(v.x * 16.0, 4), round(v.z * 16.0, 4), round(-v.y * 16.0, 4)]   # same mapping as bon_bake.py
parent = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
head = {b.name: arm.matrix_world @ b.head_local for b in arm.data.bones}

keep = set(a.keep.split(",")) if a.keep else set(parent)
def kept(b):
    while b is not None and b not in keep: b = parent[b]
    return b

pts, owner = [], []
for o in meshes:
    names = {g.index: g.name for g in o.vertex_groups}
    for v in o.data.vertices:
        gs = [(g.weight, names[g.group]) for g in v.groups if g.weight > 0 and names[g.group] in parent]
        if not gs: continue
        pts.append(o.matrix_world @ v.co); owner.append(max(gs)[1])
kd = kdtree.KDTree(len(pts))
for i, p in enumerate(pts): kd.insert(p, i)
kd.balance()

mesh = json.load(open(a.mesh, encoding="utf-8"))
from mathutils import Vector
vbone = {}
for vid, (x, y, z) in mesh["vertices"].items():
    _, i, _ = kd.find(Vector((x / 16.0, -z / 16.0, y / 16.0)))
    vbone[vid] = kept(owner[i])

fbone = {fid: Counter(vbone[v] for v in f["vertices"]).most_common(1)[0][0] for fid, f in mesh["faces"].items()}
while True:   # fold tiny bones (a few stray faces) into their kept parent
    cnt = Counter(fbone.values())
    small = [b for b, c in cnt.items() if c < a.min_faces and kept(parent[b]) is not None]
    if not small: break
    for fid, b in fbone.items():
        if b in small: fbone[fid] = kept(parent[b])

used = set(fbone.values())
need = set()
for b in used:   # keep the chain up to the root so the hierarchy stays connected
    while b is not None: need.add(b); b = kept(parent[b])
bones = {b: {"parent": kept(parent[b]), "pivot": px(head[b])} for b in sorted(need, key=lambda n: list(parent).index(n))}
for fid, f in mesh["faces"].items(): f["bone"] = fbone[fid]
mesh["bones"] = bones
json.dump(mesh, open(a.out, "w", encoding="utf-8"))
print(f"{len(bones)} bones:", {b: c for b, c in Counter(fbone.values()).most_common()})
