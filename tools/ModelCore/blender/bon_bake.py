"""Blender (bpy) bake recipe for an imported glTF mob -> ModelCore mesh JSON + baked textures.

    blender -b -P bon_bake.py -- model.glb OUTDIR      (env: TEX, HEAD_TRIS, BODY_TRIS, ATLAS, BLOOD_DECAL)

Outputs OUTDIR/<name>_mesh.json, _albedo/_mer/_emit.png for tools/ModelCore/mesh_entity.py.
The jaw pose, eye boost and blood decal placement are bon's (PURGATORY); copy and adjust per mob.
Blood decal: python blood_decal.py OUT.png
"""
import bpy, bmesh, sys, math, json, os
from mathutils import Vector, Matrix

glb, outdir = sys.argv[-2], sys.argv[-1]
os.makedirs(outdir, exist_ok=True)
TEX = int(os.environ.get('TEX', '2048'))
TARGET_TRIS = int(os.environ.get('TRIS', '8000'))
DECAL = os.environ.get('BLOOD_DECAL', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blood_decal.png'))

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
for o in [o for o in bpy.data.objects if o.type == 'MESH']:
    if 'cosphere' in o.name.lower(): bpy.data.objects.remove(o, do_unlink=True)
main = next(o for o in bpy.data.objects if o.type == 'MESH' and o.vertex_groups.get('Jaw'))
for md in list(main.modifiers):
    if md.type == 'ARMATURE': main.modifiers.remove(md)
mw = main.matrix_world.copy(); me = main.data
for v in me.vertices: v.co = mw @ v.co
main.matrix_world = Matrix.Identity(4)

# ---- look: bright eyes ----
EYE_MATS = set()
for m in bpy.data.materials:
    if 'eye' in m.name.lower() and m.use_nodes:
        EYE_MATS.add(m.name)
        nt = m.node_tree; bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        if not bsdf: continue
        bc = bsdf.inputs['Base Color']
        if bc.is_linked:
            src = bc.links[0].from_socket
            bcn = nt.nodes.new('ShaderNodeBrightContrast'); bcn.inputs['Bright'].default_value = 0.15; bcn.inputs['Contrast'].default_value = 0.55
            nt.links.new(src, bcn.inputs['Color']); nt.links.new(bcn.outputs['Color'], bc)

# ---- jaw open 6 deg ----
gi = main.vertex_groups['Jaw'].index
pivot = Vector((0.002, 0.043, 2.132)); ANGLE = -6.0; FWD = 0.015
jaw_set = set()
for v in me.vertices:
    w = next((g.weight for g in v.groups if g.group == gi), 0.0)
    if w <= 0.02: continue
    jaw_set.add(v.index)
    R = Matrix.Rotation(math.radians(ANGLE) * w, 4, 'X'); co = v.co.copy()
    v.co = R @ (co - pivot) + pivot + Vector((0, FWD, 0)) * w
me.update()

# ---- tears ----
BOX = [(-0.18,0.18,-0.02,0.32,1.50,1.90),(0.06,0.22,-0.05,0.32,1.12,1.34),
       (0.05,0.29,-0.06,0.30,0.76,1.08),(-0.29,-0.05,-0.06,0.30,0.76,1.08),
       (0.34,0.56,-0.16,0.26,1.48,1.76),(-0.56,-0.34,-0.16,0.26,1.48,1.76)]
def inbox(c):
    return any(x0<=c.x<=x1 and y0<=c.y<=y1 and z0<=c.z<=z1 for x0,x1,y0,y1,z0,z1 in BOX)
bm = bmesh.new(); bm.from_mesh(me); dead = []
for f in bm.faces:
    if any(v.index in jaw_set for v in f.verts): continue
    mi = f.material_index
    mnm = main.material_slots[mi].name.lower() if mi < len(main.material_slots) else ''
    if ('casing' in mnm or 'cyberfun' in mnm) and f.calc_center_median().z <= 1.92 and inbox(f.calc_center_median()):
        dead.append(f)
if dead: bmesh.ops.delete(bm, geom=dead, context='FACES')
bmesh.ops.triangulate(bm, faces=bm.faces)
# glTF import keeps every UV/normal seam split; weld coincident verts so decimation sees connected surfaces
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0001)
bm.to_mesh(me); bm.free(); me.update()

# ---- blood decal (projected, layered over muzzle materials) ----
img = bpy.data.images.load(DECAL, check_existing=True)
MUZZLE = ('torsoheadcasing', 'boltendo', 'endolight', 'endocustom', 'limbendo')
def add_decal(mat):
    nt = mat.node_tree
    out = next((n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'), None)
    if not out or not out.inputs['Surface'].is_linked: return
    orig = out.inputs['Surface'].links[0].from_socket
    tco = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ'); nt.links.new(tco.outputs['Object'], sep.inputs[0])
    um = nt.nodes.new('ShaderNodeMath'); um.operation='MULTIPLY_ADD'; um.inputs[1].default_value=1/0.36; um.inputs[2].default_value=0.5
    nt.links.new(sep.outputs['X'], um.inputs[0])
    vm = nt.nodes.new('ShaderNodeMath'); vm.operation='MULTIPLY_ADD'; vm.inputs[1].default_value=1/0.28; vm.inputs[2].default_value=-1.86/0.28
    nt.links.new(sep.outputs['Z'], vm.inputs[0])
    cmb = nt.nodes.new('ShaderNodeCombineXYZ'); nt.links.new(um.outputs[0], cmb.inputs['X']); nt.links.new(vm.outputs[0], cmb.inputs['Y'])
    tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = img; tex.extension = 'CLIP'
    nt.links.new(cmb.outputs[0], tex.inputs['Vector'])
    geo = nt.nodes.new('ShaderNodeNewGeometry'); ns = nt.nodes.new('ShaderNodeSeparateXYZ'); nt.links.new(geo.outputs['Normal'], ns.inputs[0])
    gt = nt.nodes.new('ShaderNodeMath'); gt.operation='GREATER_THAN'; gt.inputs[1].default_value=0.0; nt.links.new(ns.outputs['Y'], gt.inputs[0])
    fac = nt.nodes.new('ShaderNodeMath'); fac.operation='MULTIPLY'
    nt.links.new(tex.outputs['Alpha'], fac.inputs[0]); nt.links.new(gt.outputs[0], fac.inputs[1])
    bb = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(tex.outputs['Color'], bb.inputs['Base Color']); bb.inputs['Roughness'].default_value = 0.9; bb.inputs['Metallic'].default_value = 0.0
    mix = nt.nodes.new('ShaderNodeMixShader')
    nt.links.new(fac.outputs[0], mix.inputs[0]); nt.links.new(orig, mix.inputs[1]); nt.links.new(bb.outputs['BSDF'], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs['Surface'])
for m in bpy.data.materials:
    if m.use_nodes and any(k in m.name.lower() for k in MUZZLE): add_decal(m)

# ---- decimate: head (face/eyes/teeth) gently, body harder ----
HEAD_Z = 1.86
HEAD_TRIS = int(os.environ.get('HEAD_TRIS', '5000')); BODY_TRIS = int(os.environ.get('BODY_TRIS', '7000'))
def only(o):
    for x in bpy.data.objects: x.select_set(False)
    o.select_set(True); bpy.context.view_layer.objects.active = o
only(main)
tris_before = len(me.polygons)
bpy.ops.object.mode_set(mode='EDIT')
ebm = bmesh.from_edit_mesh(main.data)
for f in ebm.faces: f.select_set(f.calc_center_median().z > HEAD_Z)
bmesh.update_edit_mesh(main.data)
bpy.ops.mesh.separate(type='SELECTED')
bpy.ops.object.mode_set(mode='OBJECT')
head = next(o for o in bpy.data.objects if o.type == 'MESH' and o is not main)
for obj, tgt in ((main, BODY_TRIS), (head, HEAD_TRIS)):
    only(obj); n = len(obj.data.polygons)
    d = obj.modifiers.new('dec', 'DECIMATE'); d.decimate_type = 'COLLAPSE'; d.ratio = min(1.0, tgt / max(1, n))
    bpy.ops.object.modifier_apply(modifier='dec')
    print("  part", obj.name, n, "->", len(obj.data.polygons))
for x in bpy.data.objects: x.select_set(x in (main, head))
bpy.context.view_layer.objects.active = main
bpy.ops.object.join()
me = main.data
bm = bmesh.new(); bm.from_mesh(me)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0001)  # re-weld the neck seam
bmesh.ops.triangulate(bm, faces=bm.faces)
bm.to_mesh(me); bm.free(); me.update()
print("TRIS", tris_before, "->", len(me.polygons), "verts", len(me.vertices))

# ---- atlas UV (bake target) from the ORIGINAL islands, evenly scaled + packed ----
only(main)
orig_name = me.uv_layers[0].name
me.uv_layers.new(name="atlas")          # starts as a copy of the original UVs
me.uv_layers.active = me.uv_layers["atlas"]
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
if os.environ.get('ATLAS', 'orig') == 'smart':
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.006)
else:
    bpy.ops.uv.average_islands_scale()
    bpy.ops.uv.pack_islands(rotate=True, margin=0.004)
bpy.ops.object.mode_set(mode='OBJECT')
me = main.data  # refs go stale across mode switches
me.uv_layers[orig_name].active_render = True
me.uv_layers.active = me.uv_layers["atlas"]
print("UV layers:", [(l.name, l.active, l.active_render) for l in me.uv_layers])

# ---- bake setup ----
scn = bpy.context.scene
scn.render.engine = 'CYCLES'; scn.cycles.device = 'CPU'; scn.cycles.samples = 4
scn.render.bake.margin = 12; scn.render.bake.use_clear = True
mats = [s.material for s in main.material_slots if s.material]
def target(name, noncolor):
    im = bpy.data.images.new(name, TEX, TEX, alpha=False)
    if noncolor: im.colorspace_settings.name = 'Non-Color'
    for m in mats:
        nt = m.node_tree
        n = nt.nodes.get('BAKE_T') or nt.nodes.new('ShaderNodeTexImage')
        n.name = 'BAKE_T'; n.image = im
        for x in nt.nodes: x.select = False
        n.select = True; nt.nodes.active = n
    return im
def save(im, fn):
    im.filepath_raw = os.path.join(outdir, fn); im.file_format = 'PNG'; im.save()
    print("SAVED", fn)

# pass 1: roughness on the real shaders
im = target('bon_rough', True); bpy.ops.object.bake(type='ROUGHNESS'); save(im, 'bon_rough.png')

# rewire every Principled BSDF -> Emission, so EMIT bakes read a chosen input straight (no lighting)
EM = []  # (material, principled, emission)
for m in mats:
    nt = m.node_tree
    for p in [n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED']:
        e = nt.nodes.new('ShaderNodeEmission'); e.inputs['Strength'].default_value = 1.0
        for l in list(p.outputs['BSDF'].links):
            to = l.to_socket; nt.links.remove(l); nt.links.new(e.outputs['Emission'], to)
        EM.append((m, p, e))
def feed(input_name):
    for m, p, e in EM:
        nt = m.node_tree
        for l in list(e.inputs['Color'].links): nt.links.remove(l)
        s = p.inputs[input_name]
        if s.is_linked: nt.links.new(s.links[0].from_socket, e.inputs['Color'])
        else:
            v = s.default_value
            e.inputs['Color'].default_value = (v[0], v[1], v[2], 1) if hasattr(v, '__len__') else (v, v, v, 1)

feed('Metallic');   im = target('bon_metal', True);  bpy.ops.object.bake(type='EMIT'); save(im, 'bon_metal.png')
feed('Base Color'); im = target('bon_albedo', False); bpy.ops.object.bake(type='EMIT'); save(im, 'bon_albedo.png')
# emissive mask: eye materials white, everything else black
for m, p, e in EM:
    for l in list(e.inputs['Color'].links): m.node_tree.links.remove(l)
    e.inputs['Color'].default_value = (1, 1, 1, 1) if m.name in EYE_MATS else (0, 0, 0, 1)
im = target('bon_emit', True); bpy.ops.object.bake(type='EMIT'); save(im, 'bon_emit.png')

# ---- export mesh (same atlas UVs) in Blockbench space ----
uvl = me.uv_layers['atlas'].data
verts = {"v%d" % i: [v.co.x*16.0, v.co.z*16.0, -v.co.y*16.0] for i, v in enumerate(me.vertices)}
faces = {}
for p in me.polygons:
    vids = []; uvd = {}
    for li in p.loop_indices:
        vid = "v%d" % me.loops[li].vertex_index; vids.append(vid)
        u, vv = uvl[li].uv; uvd[vid] = [u*TEX, (1.0-vv)*TEX]
    faces["f%d" % p.index] = {"vertices": vids, "uv": uvd, "texture": 0}
json.dump({"resolution": [TEX, TEX], "vertices": verts, "faces": faces,
           "tris": len(me.polygons), "verts": len(me.vertices)}, open(os.path.join(outdir, 'bon_mesh.json'), 'w'))
print("EXPORTED tris", len(me.polygons))
