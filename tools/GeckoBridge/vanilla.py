"""Vanilla-model entities (no GeckoLib): Java EntityModel meshes -> Bedrock geometry + animations.

Covers what MCreator mods ship next to their GeckoLib mobs:
  * custom Java models (`createBodyLayer()`: CubeListBuilder / PartDefinition / PartPose bytecode) and their
    `setupAnim()` maths, turned into Molang (head look, limb swing, idle bob...)
  * vanilla models used by mod renderers: HumanoidModel on the PLAYER layer (64x64 wide skin), CowModel, PigModel
  * eye layers (RenderType.eyes) -> glow overlay; block-rendering projectiles -> a spinning cube

Coordinate conversion (same as Blockbench's Java-entity -> Bedrock): Java parts hang from y=24 with y down;
Bedrock pivot = [-x, 24 - y, z], cube origin = [-(x + w), 24 - (y + h), z], rotation = [-rx, -ry, rz] (degrees).
Java child poses are relative to the parent part; Bedrock pivots are absolute (unrotated model space).
"""
import math
import re

DEG = 57.29578
# SRG names (1.20.1) used by mesh builders
CLB_CREATE, CLB_TEXOFFS, CLB_MIRROR, CLB_MIRROR_B = "m_171558_", "m_171514_", "m_171480_", "m_171555_"
POSE_OFFSET, POSE_OFFROT, POSE_ROT = "m_171419_", "m_171423_", "m_171430_"
PART_ADD, MESH_ROOT, LAYER_CREATE = "m_171599_", "m_171576_", "m_171565_"
ROT_FIELDS = {"f_104203_": 0, "f_104204_": 1, "f_104205_": 2}          # ModelPart.xRot / yRot / zRot
POS_FIELDS = {"f_104200_": 0, "f_104201_": 1, "f_104202_": 2}          # ModelPart.x / y / z
VIS_FIELD = "f_104207_"                                                # ModelPart.visible
MTH = {"m_14089_": "cos", "m_14031_": "sin", "m_14154_": "abs", "m_14116_": "sqrt"}

INSN = re.compile(r"^\s*(\d+): (\w+)\s*(.*)$")


def _const(op, arg):
    if op.startswith("fconst_") or op.startswith("iconst_") or op.startswith("dconst_"):
        v = op.split("_")[1]; return -1.0 if v == "m1" else float(v)
    if op in ("bipush", "sipush"): return float(arg.split()[0])
    if op.startswith("ldc"):
        m = re.search(r"// (?:float|double|int|long) (-?[\d.E+-]+|NaN|Infinity)[fdl]?", arg)
        if m: return float(m.group(1))
        m = re.search(r"// String (.*)$", arg)
        if m: return ("str", m.group(1))
    return None


def method_body(text, name):
    """Instructions of the first method whose header contains `name`."""
    out, on = [], False
    for line in text.splitlines():
        if not on:
            if re.search(rf"\b{re.escape(name)}\(", line) and line.rstrip().endswith(";"): on = True
            continue
        if line.strip() == "Code:": continue
        m = INSN.match(line)
        if m: out.append((m.group(2), m.group(3))); continue
        if line.strip().startswith(("LineNumber", "LocalVariable", "Exception")) or not line.strip(): continue
        if not line.startswith("    ") and out: break
        if line.startswith("  ") and not line.startswith("    ") and out: break
    return out


# ------------------------------------------------------------------ createBodyLayer() -> parts
class Builder:
    def __init__(self): self.cubes = []; self.u = 0; self.v = 0; self.mirror = False


def parse_mesh(text, method="createBodyLayer"):
    """Symbolically run a LayerDefinition factory. Returns {"parts": [...], "tex": (w, h)} or None."""
    code = method_body(text, method)
    if not code: return None
    stack, local = [], {}
    parts, tex = [], (64, 64)
    root = {"name": "__root", "parent": None, "pose": (0, 0, 0, 0, 0, 0), "cubes": []}
    for op, arg in code:
        c = _const(op, arg)
        if c is not None: stack.append(c); continue
        if op in ("fload", "aload", "iload", "dload") or re.match(r"[fadi]load_\d", op):
            idx = int(op.split("_")[1]) if "_" in op else int(arg.split()[0]); stack.append(local.get(idx)); continue
        if op in ("fstore", "astore", "istore", "dstore") or re.match(r"[fadi]store_\d", op):
            idx = int(op.split("_")[1]) if "_" in op else int(arg.split()[0]); local[idx] = stack.pop() if stack else None; continue
        if op == "fneg": stack.append(-stack.pop()); continue
        if op in ("fmul", "fdiv", "fadd", "fsub", "dmul", "ddiv", "dadd", "dsub"):
            b, a = stack.pop(), stack.pop()
            try: stack.append({"mul": a * b, "div": a / b, "add": a + b, "sub": a - b}[op[1:]])
            except Exception: stack.append(0.0)
            continue
        if op in ("d2f", "f2d", "i2f", "checkcast", "nop"): continue
        if op == "dup": stack.append(stack[-1]); continue
        if op == "pop": stack.pop(); continue
        if op == "new":
            stack.append({"new": arg.split("class ")[-1]}); continue
        if op == "getstatic":
            if "CubeDeformation" in arg: stack.append(("def", 0.0))
            elif "PartPose" in arg: stack.append(("pose", (0, 0, 0, 0, 0, 0)))
            else: stack.append(None)
            continue
        if op.startswith("invoke"):
            m = re.search(r"Method ([\w/$]+)\.([\w<>\"]+):\(([^)]*)\)(\S+)", arg)
            if not m: continue
            owner, name, desc, ret = m.group(1), m.group(2).strip('"'), m.group(3), m.group(4)
            nargs = len(re.findall(r"\[*(?:L[^;]+;|[ZBCSIJFD])", desc))
            args = [stack.pop() for _ in range(nargs)][::-1] if nargs else []
            this = stack.pop() if op != "invokestatic" and name != "<init>" else None
            if name == "<init>":
                obj = stack.pop() if stack else None          # the dup'd `new`
                if stack and stack[-1] is obj: stack.pop()
                if owner.endswith("CubeDeformation"):
                    g = args[0] if args and isinstance(args[0], float) else 0.0
                    stack.append(("def", g))
                elif owner.endswith("MeshDefinition"): stack.append(("mesh", root))
                else: stack.append(None)
                continue
            if owner.endswith("MeshDefinition") and name == MESH_ROOT: stack.append(root); continue
            if owner.endswith("CubeListBuilder"):
                if name == CLB_CREATE: stack.append(Builder()); continue
                b = this
                if not isinstance(b, Builder): stack.append(b); continue
                if name == CLB_TEXOFFS: b.u, b.v = int(args[0]), int(args[1])
                elif name == CLB_MIRROR: b.mirror = True
                elif name == CLB_MIRROR_B: b.mirror = bool(args[0])
                else:
                    nums = [a for a in args if isinstance(a, float)]
                    defo = next((a[1] for a in args if isinstance(a, tuple) and a and a[0] == "def"), 0.0)
                    if len(nums) >= 6:
                        mir = b.mirror
                        if desc.endswith("Z") and len(nums) >= 7: mir = bool(nums[6])
                        tex_uv = (b.u, b.v)
                        if len(nums) >= 8 and desc.count("F") >= 8 and "CubeDeformation" in desc:   # addBox(..., def, uScale, vScale)
                            pass
                        b.cubes.append({"from": nums[0:3], "size": nums[3:6], "uv": tex_uv, "inflate": defo, "mirror": mir})
                stack.append(b); continue
            if owner.endswith("PartPose"):
                f = [a if isinstance(a, float) else 0.0 for a in args]
                if name == POSE_OFFSET: stack.append(("pose", (f[0], f[1], f[2], 0, 0, 0)))
                elif name == POSE_OFFROT: stack.append(("pose", tuple(f[:6])))
                elif name == POSE_ROT: stack.append(("pose", (0, 0, 0, f[0], f[1], f[2])))
                else: stack.append(("pose", (0, 0, 0, 0, 0, 0)))
                continue
            if owner.endswith("PartDefinition") and name == PART_ADD:
                pname = args[0][1] if isinstance(args[0], tuple) and args[0][0] == "str" else f"part{len(parts)}"
                bld = args[1] if isinstance(args[1], Builder) else Builder()
                pose = args[2][1] if isinstance(args[2], tuple) and args[2] and args[2][0] == "pose" else (0, 0, 0, 0, 0, 0)
                par = this if isinstance(this, dict) else root
                p = {"name": pname, "parent": par["name"], "pose": pose, "cubes": bld.cubes}
                parts = [x for x in parts if x["name"] != pname] + [p]
                stack.append(p); continue
            if owner.endswith("LayerDefinition") and name == LAYER_CREATE:
                tex = (int(args[1]), int(args[2])) if len(args) >= 3 else tex
                stack.append(None); continue
            if ret != "V": stack.append(None)
            continue
        if op in ("areturn", "return"): break
    return {"parts": parts, "tex": tex} if parts else None


def to_bedrock(mesh, identifier, extra_bones=()):
    """Java parts -> one Bedrock geometry dict (format 1.12.0)."""
    by = {p["name"]: p for p in mesh["parts"]}
    absolute = {"__root": (0.0, 0.0, 0.0)}
    def abs_of(p):
        if p["name"] in absolute: return absolute[p["name"]]
        par = by.get(p["parent"]); pa = abs_of(par) if par else (0.0, 0.0, 0.0)
        r = (pa[0] + p["pose"][0], pa[1] + p["pose"][1], pa[2] + p["pose"][2]); absolute[p["name"]] = r; return r
    bones = []
    for p in mesh["parts"]:
        ax, ay, az = abs_of(p)
        b = {"name": bone(p["name"]), "pivot": [r(-ax), r(24 - ay), r(az)]}
        if p["parent"] and p["parent"] != "__root": b["parent"] = bone(p["parent"])
        rx, ry, rz = p["pose"][3:6]
        if rx or ry or rz: b["rotation"] = [r(-rx * DEG), r(-ry * DEG), r(rz * DEG)]
        cubes = []
        for c in p["cubes"]:
            (x, y, z), (w, h, d) = c["from"], c["size"]
            cube = {"origin": [r(-(ax + x + w)), r(24 - (ay + y + h)), r(az + z)], "size": [r(w), r(h), r(d)], "uv": list(c["uv"])}
            if c["inflate"]: cube["inflate"] = r(c["inflate"])
            if c["mirror"]: cube["mirror"] = True
            cubes.append(cube)
        if cubes: b["cubes"] = cubes
        bones.append(b)
    bones += list(extra_bones)
    lo = [min((bb["pivot"][i] for bb in bones), default=0) for i in range(3)]
    return {"format_version": "1.12.0", "minecraft:geometry": [{
        "description": {"identifier": identifier, "texture_width": mesh["tex"][0], "texture_height": mesh["tex"][1],
                        "visible_bounds_width": 3, "visible_bounds_height": 3.5, "visible_bounds_offset": [0, 1.25, 0]},
        "bones": bones}]}


def bone(n): return re.sub(r"[^a-z0-9_]", "_", n.lower())
def r(x): return round(float(x), 4) + 0.0


# ------------------------------------------------------------------ setupAnim() -> Molang
LOCALS = {2: "v.gb_ls", 3: "q.modified_move_speed", 4: "(q.life_time * 20)", 5: "(-q.target_y_rotation)", 6: "(-q.target_x_rotation)"}


def part_fields(text):
    """Model field -> part name, from the constructor's root.getChild("name") chain (falls back to the field name)."""
    out, last = {}, None
    for op, arg in method_body(text, "<init>") or method_body(text, text.split("class ")[1].split("<")[0].split(" ")[0].split(".")[-1]):
        s = re.search(r"// String (\S+)", arg)
        if s: last = s.group(1)
        f = re.search(r"putfield\s+#\d+\s+// Field (\w+):Lnet/minecraft/client/model/geom/ModelPart;", f"{op} {arg}")
        if f and last: out[f.group(1)] = last; last = None
    return out


def parse_anim(text, fields, method="m_6973_"):
    """setupAnim assignments -> {(part, axis): molang-in-radians}. Unknown constructs give up on that line."""
    code = method_body(text, method)
    out, stack, cur = {}, [], {}
    for op, arg in code:
        c = _const(op, arg)
        if c is not None: stack.append(fmt(c) if isinstance(c, float) else None); continue
        if re.match(r"fload(_\d)?$", op):
            idx = int(op.split("_")[1]) if "_" in op else int(arg.split()[0]); stack.append(LOCALS.get(idx)); continue
        if op == "aload_0": stack.append("this"); continue
        if re.match(r"aload", op): stack.append("obj"); continue
        if op == "getfield":
            m = re.search(r"Field (?:[\w/$]+\.)?(\w+):(\S+)", arg); obj = stack.pop() if stack else None
            if m and m.group(2) == "Lnet/minecraft/client/model/geom/ModelPart;" and obj == "this": stack.append(("part", fields.get(m.group(1), m.group(1)))); continue
            if m and isinstance(obj, tuple) and obj[0] == "part" and m.group(1) in ROT_FIELDS:
                stack.append(cur.get((obj[1], ROT_FIELDS[m.group(1)]), "0")); continue
            stack.append(None); continue
        if op == "putfield":
            m = re.search(r"Field (?:[\w/$]+\.)?(\w+):", arg); val = stack.pop() if stack else None; obj = stack.pop() if stack else None
            if m and isinstance(obj, tuple) and obj[0] == "part" and m.group(1) in ROT_FIELDS and isinstance(val, str):
                cur[(obj[1], ROT_FIELDS[m.group(1)])] = val; out[(obj[1], ROT_FIELDS[m.group(1)])] = val
            continue
        if op in ("fmul", "fdiv", "fadd", "fsub"):
            b, a = (stack.pop() if stack else None), (stack.pop() if stack else None)
            stack.append(f"({a} {'*' if op == 'fmul' else '/' if op == 'fdiv' else '+' if op == 'fadd' else '-'} {b})" if isinstance(a, str) and isinstance(b, str) else None); continue
        if op == "fneg": a = stack.pop() if stack else None; stack.append(f"(-{a})" if isinstance(a, str) else None); continue
        if op in ("d2f", "f2d", "i2f", "f2i", "checkcast"): continue
        if op == "dup": stack.append(stack[-1] if stack else None); continue
        if op == "pop": stack and stack.pop(); continue
        if op.startswith("invoke"):
            m = re.search(r"Method ([\w/$]+)\.([\w<>]+):\(([^)]*)\)(\S+)", arg)
            if not m: stack.append(None); continue
            nargs = len(re.findall(r"\[*(?:L[^;]+;|[ZBCSIJFD])", m.group(3)))
            args = [stack.pop() if stack else None for _ in range(nargs)][::-1]
            if op != "invokestatic" and stack: stack.pop()
            fn = MTH.get(m.group(2)) or ({"cos": "cos", "sin": "sin", "abs": "abs", "sqrt": "sqrt", "toRadians": "rad"}.get(m.group(2)) if m.group(1) == "java/lang/Math" else None)
            if fn and args and isinstance(args[0], str):
                stack.append(f"math.{fn}({args[0]} * {DEG})" if fn in ("cos", "sin") else f"({args[0]} / {DEG})" if fn == "rad" else f"math.{fn}({args[0]})")
            elif m.group(4) != "V": stack.append(None)
            continue
        if op in ("ifeq", "ifne", "if_icmpeq", "if_icmpne", "goto", "fcmpl", "fcmpg", "instanceof", "iflt", "ifgt", "ifle", "ifge"):
            return out                                         # branches: keep what was computed before them
        if op == "return": break
    return out


def fmt(x): return repr(round(x, 6))


def anim_json(name, assigns, rest):
    """{(part, axis): radians-molang} -> Bedrock animation (rotation relative to the bone's rest pose)."""
    bones = {}
    for (part, axis), expr in assigns.items():
        sign = -1 if axis in (0, 1) else 1
        b = bones.setdefault(bone(part), {"rotation": ["0", "0", "0"]})
        base = rest.get(bone(part), [0, 0, 0])[axis]
        val = f"{sign} * ({expr}) * {DEG}"
        b["rotation"][axis] = f"{val} - ({base})" if base else val
    for b in bones.values():
        b["rotation"] = [x if x != "0" else 0 for x in b["rotation"]]
    return {name: {"loop": True, "bones": bones}} if bones else {}


# ------------------------------------------------------------------ vanilla meshes (1.20.1 PlayerModel / CowModel / PigModel)
def _p(name, parent, pose, *cubes):
    return {"name": name, "parent": parent, "pose": pose,
            "cubes": [{"from": c[2:5], "size": c[5:8], "uv": (c[0], c[1]), "inflate": c[8] if len(c) > 8 else 0.0, "mirror": c[9] if len(c) > 9 else False} for c in cubes]}


def player_mesh(slim=False):
    aw = 3.0 if slim else 4.0; ay = 2.5 if slim else 2.0
    P = [_p("head", None, (0, 0, 0, 0, 0, 0), (0, 0, -4, -8, -4, 8, 8, 8)),
         _p("hat", "head", (0, 0, 0, 0, 0, 0), (32, 0, -4, -8, -4, 8, 8, 8, 0.5)),
         _p("body", None, (0, 0, 0, 0, 0, 0), (16, 16, -4, 0, -2, 8, 12, 4)),
         _p("jacket", "body", (0, 0, 0, 0, 0, 0), (16, 32, -4, 0, -2, 8, 12, 4, 0.25)),
         _p("right_arm", None, (-5, ay, 0, 0, 0, 0), (40, 16, -3 if not slim else -2, -2, -2, aw, 12, 4)),
         _p("right_sleeve", "right_arm", (0, 0, 0, 0, 0, 0), (40, 32, -3 if not slim else -2, -2, -2, aw, 12, 4, 0.25)),
         _p("left_arm", None, (5, ay, 0, 0, 0, 0), (32, 48, -1, -2, -2, aw, 12, 4)),
         _p("left_sleeve", "left_arm", (0, 0, 0, 0, 0, 0), (48, 48, -1, -2, -2, aw, 12, 4, 0.25)),
         _p("right_leg", None, (-1.9, 12, 0, 0, 0, 0), (0, 16, -2, 0, -2, 4, 12, 4)),
         _p("right_pants", "right_leg", (0, 0, 0, 0, 0, 0), (0, 32, -2, 0, -2, 4, 12, 4, 0.25)),
         _p("left_leg", None, (1.9, 12, 0, 0, 0, 0), (16, 48, -2, 0, -2, 4, 12, 4)),
         _p("left_pants", "left_leg", (0, 0, 0, 0, 0, 0), (0, 48, -2, 0, -2, 4, 12, 4, 0.25))]
    for p in P:
        if p["parent"] is None: p["parent"] = "__root"
    return {"parts": P, "tex": (64, 64)}


def quadruped_mesh(kind):
    if kind == "cow":
        P = [_p("head", None, (0, 4, -8, 0, 0, 0), (0, 0, -4, -4, -6, 8, 8, 6), (22, 0, -5, -5, -4, 1, 3, 1), (22, 0, 4, -5, -4, 1, 3, 1)),
             _p("body", None, (0, 5, 2, math.pi / 2, 0, 0), (18, 4, -6, -10, -7, 12, 18, 10), (52, 0, -2, 2, -8, 4, 6, 1)),
             _p("leg0", None, (-4, 12, 7, 0, 0, 0), (0, 16, -2, 0, -2, 4, 12, 4)),
             _p("leg1", None, (4, 12, 7, 0, 0, 0), (0, 16, -2, 0, -2, 4, 12, 4, 0.0, True)),
             _p("leg2", None, (-4, 12, -6, 0, 0, 0), (0, 16, -2, 0, -2, 4, 12, 4)),
             _p("leg3", None, (4, 12, -6, 0, 0, 0), (0, 16, -2, 0, -2, 4, 12, 4, 0.0, True))]
        tex = (64, 32)
    else:   # pig
        P = [_p("head", None, (0, 12, -6, 0, 0, 0), (0, 0, -4, -4, -8, 8, 8, 8), (16, 16, -2, 0, -9, 4, 3, 1)),
             _p("body", None, (0, 11, 2, math.pi / 2, 0, 0), (28, 8, -5, -10, -7, 10, 16, 8)),
             _p("leg0", None, (-3, 18, 7, 0, 0, 0), (0, 16, -2, 0, -2, 4, 6, 4)),
             _p("leg1", None, (3, 18, 7, 0, 0, 0), (0, 16, -2, 0, -2, 4, 6, 4, 0.0, True)),
             _p("leg2", None, (-3, 18, -5, 0, 0, 0), (0, 16, -2, 0, -2, 4, 6, 4)),
             _p("leg3", None, (3, 18, -5, 0, 0, 0), (0, 16, -2, 0, -2, 4, 6, 4, 0.0, True))]
        tex = (64, 32)
    for p in P: p["parent"] = "__root"
    return {"parts": P, "tex": tex}


def cube_mesh(size=8):
    h = size / 2
    return {"parts": [{"name": "block", "parent": "__root", "pose": (0, 24 - h, 0, 0, 0, 0),
                       "cubes": [{"from": (-h, -h, -h), "size": (size, size, size), "uv": (0, 0), "inflate": 0.0, "mirror": False}]}],
            "tex": (int(size) * 4, int(size) * 2)}


# setupAnim of the vanilla models above (HumanoidModel / QuadrupedModel), same symbols as parse_anim
HUMANOID_ANIM = {("head", 1): "(-q.target_y_rotation) / 57.29578", ("head", 0): "(-q.target_x_rotation) / 57.29578",
                 ("right_arm", 0): f"math.cos((v.gb_ls * 0.6662 + 3.1415927) * {DEG}) * 2.0 * q.modified_move_speed * 0.5",
                 ("left_arm", 0): f"math.cos((v.gb_ls * 0.6662) * {DEG}) * 2.0 * q.modified_move_speed * 0.5",
                 ("right_leg", 0): f"math.cos((v.gb_ls * 0.6662) * {DEG}) * 1.4 * q.modified_move_speed",
                 ("left_leg", 0): f"math.cos((v.gb_ls * 0.6662 + 3.1415927) * {DEG}) * 1.4 * q.modified_move_speed",
                 ("right_arm", 2): f"math.cos((q.life_time * 20 * 0.09) * {DEG}) * 0.05 + 0.05",
                 ("left_arm", 2): f"-(math.cos((q.life_time * 20 * 0.09) * {DEG}) * 0.05 + 0.05)"}
QUADRUPED_ANIM = {("head", 1): "(-q.target_y_rotation) / 57.29578", ("head", 0): "(-q.target_x_rotation) / 57.29578",
                  ("leg0", 0): f"math.cos((v.gb_ls * 0.6662) * {DEG}) * 1.4 * q.modified_move_speed",
                  ("leg1", 0): f"math.cos((v.gb_ls * 0.6662 + 3.1415927) * {DEG}) * 1.4 * q.modified_move_speed",
                  ("leg2", 0): f"math.cos((v.gb_ls * 0.6662 + 3.1415927) * {DEG}) * 1.4 * q.modified_move_speed",
                  ("leg3", 0): f"math.cos((v.gb_ls * 0.6662) * {DEG}) * 1.4 * q.modified_move_speed"}
