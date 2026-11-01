#!/usr/bin/env python3
"""GeckoBridge controller extraction (M1): GeckoLib AnimationControllers in a mod jar -> a JSON plan.

GeckoLib decides which animation plays in Java code (a state-handler lambda per controller). This reads the
bytecode with `javap` (JDK; nothing is loaded or run) and, per GeoEntity class, finds:
  - controllers: name, transition ticks, handler method (lambda / method reference / DefaultAnimations template)
  - RawAnimation constants and their chains (thenPlay / thenLoop / thenWait / thenPlayAndHold / thenPlayXTimes)
  - triggerable animations (`triggerableAnim(name, raw)`) - recorded for M2
  - for each handler: every path through the code to a setAnimation / setAndContinue, with the conditions on
    the way (a tiny symbolic stack machine over the bytecode) and setAnimationSpeed constants.
Conditions are then translated to Molang (CONDITIONS table); anything not recognised becomes "0" (never true)
and is listed under "unknown_conditions" in the report so a per-mod overlay can fix it.

    python -I tools/GeckoBridge/controllers.py mod.jar > controllers.json

GeckoLib 4 (software/bernie/geckolib, Forge 1.20.1 SRG names) now; GeckoLib 5 / Mojang names in M4.
"""
from __future__ import annotations
import json, os, re, subprocess, sys, tempfile, zipfile

GL_PREFIXES = ("software/bernie/geckolib/", "com/geckolib/", "mod/azure/azurelib/")
RAW = "RawAnimation"

# ---- Java condition -> Molang. Keys: method/field name (SRG for Forge 1.20.1, Mojang names, or the mod's own
# readable method names). "expr" receives the receiver/argument text when needed. Confidence is noted per entry.
CONDITIONS = {
    # AnimationState (GeckoLib): limb swing amount outside +-0.15 (GeckoLib's own definition) ~ Bedrock walk anim speed
    "isMoving": "(q.modified_move_speed > 0.05)",
    # Entity / LivingEntity / Mob (Forge 1.20.1 SRG; verified against how the test mods use them)
    "m_5803_": "q.is_sleeping", "isSleeping": "q.is_sleeping",
    "m_20142_": "(q.ground_speed > 4.0)", "isSprinting": "(q.ground_speed > 4.0)",   # mobs never 'sprint' on Bedrock: speed instead
    "m_20069_": "q.is_in_water", "isInWater": "q.is_in_water",
    "m_20072_": "q.is_in_water", "isInWaterOrBubble": "q.is_in_water",
    "m_5842_": "q.is_in_water", "isUnderWater": "q.is_in_water",
    "m_6069_": "q.is_swimming", "isSwimming": "q.is_swimming",
    "m_20096_": "q.is_on_ground", "onGround": "q.is_on_ground", "f_19861_": "q.is_on_ground",
    "m_6162_": "q.is_baby", "isBaby": "q.is_baby",
    "m_6084_": "q.is_alive", "isAlive": "q.is_alive",
    "m_21224_": "(!q.is_alive)", "isDeadOrDying": "(!q.is_alive)",
    "m_5912_": "q.has_target", "isAggressive": "q.has_target",
    "f_20911_": "(v.attack_time > 0)", "swinging": "(v.attack_time > 0)",
    "m_6047_": "q.is_sneaking", "isCrouching": "q.is_sneaking", "m_20163_": "q.is_sneaking",
    "m_21825_": "q.is_sitting", "isInSittingPose": "q.is_sitting", "m_21827_": "q.is_sitting", "isOrderedToSit": "q.is_sitting",
    "m_20159_": "q.is_riding", "isPassenger": "q.is_riding",
    "m_6688_": "q.has_rider", "isVehicle": "q.has_rider",
    "m_21827_x": "q.is_sitting",
    # common readable names mods give their own flags
    "isSitting": "q.is_sitting", "isAngry": "q.is_angry", "isFlying": "(!q.is_on_ground)",
    "isRunning": "(q.ground_speed > 4.0)", "isTame": "q.is_tamed", "m_21824_": "q.is_tamed", "isTamed": "q.is_tamed",
    "hasTarget": "q.has_target", "isClimbing": "q.is_wall_climbing", "m_6147_": "q.is_wall_climbing", "onClimbable": "q.is_wall_climbing",
    # GeckoLib idiom `controller.getAnimationState().equals(State.STOPPED)` (restart a one-shot attack): Bedrock
    # controllers restart the animation on state entry, so it is simply true here
    "getAnimationState.equals": "1",
    "m_20197_.isEmpty": "(!q.has_rider)", "getPassengers.isEmpty": "(!q.has_rider)",
    "isCharging": "q.is_charging", "isAttacking": "(v.attack_time > 0)", "isLeashed": "q.is_leashed", "m_21523_": "q.is_leashed",
}
# Numeric expressions (used with a comparison). value -> Molang number in the SAME units as Java.
NUMERIC = {
    # getDeltaMovement().horizontalDistanceSqr(): blocks^2 per tick^2; Bedrock ground_speed is blocks per second
    ("m_20184_", "m_165925_"): "math.pow(q.ground_speed / 20, 2)", ("getDeltaMovement", "horizontalDistanceSqr"): "math.pow(q.ground_speed / 20, 2)",
    ("m_20184_", "m_165924_"): "(q.ground_speed / 20)", ("getDeltaMovement", "horizontalDistance"): "(q.ground_speed / 20)",
    ("m_20184_", "m_82556_"): "math.pow(q.ground_speed / 20, 2) + math.pow(q.vertical_speed / 20, 2)",
    ("m_20184_", "f_82480_"): "(q.vertical_speed / 20)", ("getDeltaMovement", "y"): "(q.vertical_speed / 20)",
    ("m_21223_",): "q.health", ("getHealth",): "q.health", ("m_21233_",): "q.max_health",
    ("f_20916_",): "q.hurt_time", ("hurtTime",): "q.hurt_time", ("f_20919_",): "q.death_ticks", ("deathTime",): "q.death_ticks",
    ("f_19797_",): "q.life_time * 20", ("tickCount",): "q.life_time * 20",
    ("getLimbSwingAmount",): "q.modified_move_speed", ("m_267731_",): "q.modified_move_speed",
    # getX()/xo (last tick's x): movement tests like getX() != xo
    ("m_20185_",): "q.position(0)", ("getX",): "q.position(0)", ("m_20186_",): "q.position(1)", ("getY",): "q.position(1)",
    ("m_20189_",): "q.position(2)", ("getZ",): "q.position(2)",
    ("f_19790_",): "(q.position(0) - q.position_delta(0))", ("xo",): "(q.position(0) - q.position_delta(0))",
    ("f_19791_",): "(q.position(1) - q.position_delta(1))", ("yo",): "(q.position(1) - q.position_delta(1))",
    ("f_19792_",): "(q.position(2) - q.position_delta(2))", ("zo",): "(q.position(2) - q.position_delta(2))",
}
# GeckoLib DefaultAnimations templates (GeckoLib 4 constant/DefaultAnimations.java, MIT)
TEMPLATES = {
    "genericLivingController": ("Living", 10, [([], "misc.living")]),
    "genericIdleController": ("Idle", 10, [([], "misc.idle")]),
    "genericWalkController": ("Walk", 0, [(["isMoving"], "move.walk"), ([], None)]),
    "genericWalkIdleController": ("Walk/Idle", 0, [(["isMoving"], "move.walk"), ([], "misc.idle")]),
    "genericSwimController": ("Swim", 0, [(["isMoving"], "move.swim"), ([], None)]),
    "genericSwimIdleController": ("Swim/Idle", 0, [(["isMoving"], "move.swim"), ([], "misc.idle")]),
    "genericFlyController": ("Fly", 0, [([], "move.fly")]),
    "genericFlyIdleController": ("Fly/Idle", 0, [(["isMoving"], "move.fly"), ([], "misc.idle")]),
    "genericWalkFlyIdleController": ("Walk/Fly/Idle", 0, [(["onGround", "isMoving"], "move.walk"), (["onGround"], "misc.idle"),
                                                           (["isMoving"], "move.fly"), ([], "misc.idle.flying")]),
    "genericWalkRunIdleController": ("Walk/Run/Idle", 0, [(["isMoving", "isSprinting"], "move.run"), (["isMoving"], "move.walk"),
                                                           ([], "misc.idle")]),
    "genericAttackAnimation": ("Attack", 5, [(["swinging"], "@arg"), ([], None)]),
}
DEFAULT_ANIM_FIELDS = {  # DefaultAnimations.<FIELD> -> animation name
    "ITEM_ON_USE": "item.use", "IDLE": "misc.idle", "IDLE_FLYING": "misc.idle.flying", "LIVING": "misc.living",
    "SPAWN": "misc.spawn", "DIE": "misc.die", "INTERACT": "misc.interact", "DEPLOY": "misc.deploy", "REST": "misc.rest",
    "SIT": "misc.sit", "WALK": "move.walk", "SWIM": "move.swim", "RUN": "move.run", "DRIVE": "move.drive", "FLY": "move.fly",
    "CRAWL": "move.crawl", "JUMP": "move.jump", "SNEAK": "move.sneak", "DIVE": "move.dive", "ATTACK_CAST": "attack.cast",
    "ATTACK_SWING": "attack.swing", "ATTACK_THROW": "attack.throw", "ATTACK_PUNCH": "attack.punch", "ATTACK_BITE": "attack.bite",
    "ATTACK_SLAM": "attack.slam", "ATTACK_STOMP": "attack.stomp", "ATTACK_STRIKE": "attack.strike",
    "ATTACK_FLYING_ATTACK": "attack.flying_attack", "ATTACK_SHOOT": "attack.shoot", "ATTACK_BLOCK": "attack.block",
    "ATTACK_CHARGE": "attack.charge", "ATTACK_CHARGE_END": "attack.charge_end", "ATTACK_POWERUP": "attack.powerup",
}

INSN = re.compile(r"^\s*(\d+):\s+(\w+)\s*(.*)$")
METHOD_HDR = re.compile(r"^  (?:[\w$<>\[\],.? ]+ )?([\w$<>.]+)\((.*)\);$")


import functools


@functools.lru_cache(maxsize=4096)
def javap(classdir, cls, verbose=False):
    args = ["javap", "-p", "-c", "-constants"] + (["-v"] if verbose else []) + ["-cp", classdir, cls]
    env = dict(os.environ, JAVA_TOOL_OPTIONS="")
    out = subprocess.run(args, capture_output=True, text=True, env=env).stdout
    # AzureLib (GeckoLib 4 fork, mod.azure.azurelib) mirrors GeckoLib's API: read it as GeckoLib
    if "azurelib" in out: out = out.replace("mod/azure/azurelib", "software/bernie/geckolib").replace("mod.azure.azurelib", "software.bernie.geckolib")
    return out


def parse_methods(dis):
    """-> {method_name: [(offset, op, rest)]} (overloads merged under name#n)."""
    out, cur, name = {}, None, None
    for line in dis.splitlines():
        m = METHOD_HDR.match(line)
        if line.strip() == "static {};":
            name = "<clinit>"; cur = out.setdefault(name, []); continue
        if m and not line.startswith("    "):
            name = m.group(1)
            if "." in name: name = "<init>"          # constructor: javap prints the class name
            k = name; i = 1
            while k in out: i += 1; k = f"{name}#{i}"
            cur = out.setdefault(k, []); continue
        mi = INSN.match(line)
        if mi and cur is not None:
            cur.append((int(mi.group(1)), mi.group(2), mi.group(3)))
    return out


def comment(rest):
    return rest.split("//", 1)[1].strip() if "//" in rest else ""


def member(rest):
    """'Method a/b/C.name:(Desc)R' -> (owner, name, desc)."""
    c = comment(rest)
    m = re.search(r"(?:Method|Field|InterfaceMethod)\s+(?:([\w/$]+)\.)?\"?([\w$<>]+)\"?:(\S+)", c)
    return (m.group(1) or "", m.group(2), m.group(3)) if m else ("", "", "")


def nargs(desc):
    inner = desc[desc.index("(") + 1:desc.index(")")]
    n, i = 0, 0
    while i < len(inner):
        ch = inner[i]
        if ch == "L": i = inner.index(";", i)
        elif ch == "[":
            i += 1
            continue
        n += 1; i += 1
    return n


def const_of(op, rest):
    c = comment(rest)
    if op.startswith("iconst_"): return int(op[7:].replace("m1", "-1"))
    if op in ("bipush", "sipush"): return int(rest.split()[0])
    if op.startswith(("dconst_", "fconst_", "lconst_")): return float(op[7:])
    m = re.match(r"(?:String) (.*)$", c)
    if m: return m.group(1)
    m = re.match(r"(?:double|float|long|int) ([-\d.Ee]+)[dfl]?$", c)
    if m: return float(m.group(1))
    return None


class Path_:
    __slots__ = ("conds", "anims", "speed", "ret", "stack", "pc", "locals", "res")
    def __init__(self, pc): self.conds, self.anims, self.speed, self.ret, self.stack, self.pc, self.locals, self.res = [], [], None, None, [], pc, {}, []
    def fork(self, pc):
        p = Path_(pc); p.conds, p.anims, p.speed, p.stack, p.locals = list(self.conds), list(self.anims), self.speed, list(self.stack), dict(self.locals)
        p.res = list(self.res)
        return p


def run_paths(code, rawfields, max_paths=96):
    """Symbolic execution of one handler method -> list of paths (conds, anims, speed, ret)."""
    idx = {off: i for i, (off, _, _) in enumerate(code)}
    done, todo, seen = [], [Path_(0)], set()
    while todo and len(done) < max_paths:
        p = todo.pop()
        steps = 0
        while p.pc < len(code) and steps < 4000:
            steps += 1
            off, op, rest = code[p.pc]
            key = (p.pc, len(p.conds), tuple(map(str, p.conds[-3:])))
            st = p.stack
            def pop():
                return st.pop() if st else "?"
            nxt = p.pc + 1
            if op.startswith(("aload", "iload", "fload", "dload", "lload")):
                n = op.split("_")[1] if "_" in op else rest.split()[0]
                st.append(p.locals[n] if n in p.locals else {"0": "this", "1": "state"}.get(n, f"local{n}"))
            elif op in ("ldc", "ldc_w", "ldc2_w") or op.startswith(("iconst", "dconst", "fconst", "lconst")) or op in ("bipush", "sipush"):
                st.append(("const", const_of(op, rest)))
            elif op == "getstatic":
                own, nm, d_ = member(rest)
                st.append(("field", own, nm))
                if "ResourceLocation" in d_: p.res.append(("field", nm))
            elif op == "getfield":
                own, nm, d_ = member(rest); obj = pop()
                st.append(("call", obj, nm))
                if "ResourceLocation" in d_: p.res.append(("ctor", nm))
            elif op.startswith("invoke"):
                own, nm, desc = member(rest)
                if op == "invokedynamic":
                    md = re.search(r"InvokeDynamic #\d+:[\w$]+:(\(.*?\)\S+)", rest)
                    for _ in range(nargs(md.group(1)) if md else 0): pop()
                    mk = re.search(r"InvokeDynamic #(\d+):", rest)
                    k = int(mk.group(1)) if mk else None
                    st.append(("const", CONCAT[k]) if k in CONCAT and CONCAT[k] else "lambda"); p.pc = nxt; continue
                args = [pop() for _ in range(nargs(desc))][::-1] if desc else []
                recv = None if op == "invokestatic" else pop()
                if op == "invokespecial" and nm == "<init>" and isinstance(recv, tuple) and recv[0] == "new":
                    obj = ("obj", recv[1], args)       # the dup'd copy below it on the stack becomes the built object
                    if "ResourceLocation" in recv[1]:
                        p.res.append(("path", next((a[1] for a in args if isinstance(a, tuple) and a[0] == "const" and isinstance(a[1], str) and ("textures/" in a[1] or a[1].endswith(".png"))), None)))
                    for i in range(len(st) - 1, -1, -1):
                        if st[i] is recv or st[i] == recv: st[i] = obj; break
                    p.pc = nxt; continue
                ret = desc.split(")")[-1] if desc else "V"
                if nm in ("setAnimation", "setAndContinue") and args:
                    p.anims.append(args[0])
                elif nm == "setAnimationSpeed" and args:
                    a = args[0]; p.speed = a[1] if isinstance(a, tuple) and a[0] == "const" else "dynamic"
                if own.endswith(RAW) and nm == "begin":
                    val = ("raw", [])
                elif own.endswith(RAW) and nm.startswith("then"):
                    base = recv if isinstance(recv, tuple) and recv[0] == "raw" else ("raw", [])
                    a = [x[1] if isinstance(x, tuple) and x[0] == "const" else str(x) for x in args]
                    val = ("raw", base[1] + [(nm, *a)])
                else:
                    val = ("call", recv, nm, args)
                if ret != "V": st.append(val)
            elif op in ("dcmpl", "dcmpg", "fcmpl", "fcmpg", "lcmp"):
                b, a = pop(), pop(); st.append(("cmp", a, b))
            elif op in ("ifeq", "ifne", "iflt", "ifge", "ifgt", "ifle", "ifnull", "ifnonnull") or op.startswith("if_"):
                target = int(rest.split()[0])
                if op.startswith("if_"):
                    b, a = pop(), pop(); v = ("cmp", a, b); rel = {"if_icmpeq": "==", "if_icmpne": "!=", "if_icmplt": "<", "if_icmpge": ">=",
                                                                       "if_icmpgt": ">", "if_icmple": "<=", "if_acmpeq": "==", "if_acmpne": "!="}[op]
                    jump = ("rel", v, rel)
                else:
                    v = pop()
                    if isinstance(v, tuple) and v[0] == "cmp":
                        jump = ("rel", v, {"ifeq": "==", "ifne": "!=", "iflt": "<", "ifge": ">=", "ifgt": ">", "ifle": "<="}[op])
                    elif op in ("ifnull", "ifnonnull"):
                        jump = ("null", v, op == "ifnull")
                    elif isinstance(v, tuple) and v[0] == "const" and op in ("ifeq", "ifne"):
                        taken = (v[1] != 0) == (op == "ifne")      # known value (ternary results stored in locals)
                        p.pc = idx[target] if taken and target in idx else nxt
                        continue
                    else:
                        jump = ("bool", v, op == "ifne")   # jump taken when value is true (ifne) / false (ifeq)
                if key in seen: break
                seen.add(key)
                if target in idx:
                    q = p.fork(idx[target]); q.conds.append((jump, True)); todo.append(q)
                p.conds.append((jump, False))
            elif op == "goto" or op == "goto_w":
                target = int(rest.split()[0])
                if target in idx: nxt = idx[target]
            elif op in ("areturn", "ireturn", "return"):
                p.ret = pop() if op != "return" else None; done.append(p); break
            elif op == "athrow":
                break
            elif op == "dup":
                st.append(st[-1] if st else "?")
            elif op in ("pop",):
                pop()
            elif op == "pop2":
                pop(); pop()
            elif op == "new":
                st.append(("new", comment(rest)))
            elif op in ("instanceof",):
                x = pop(); st.append(("call", x, "instanceof:" + comment(rest).replace("class ", "")))
            elif op in ("checkcast", "nop", "i2d", "f2d", "d2f", "i2f", "l2d", "d2i"):
                pass
            elif op.startswith(("istore", "astore", "fstore", "dstore", "lstore")):
                n = op.split("_")[1] if "_" in op else rest.split()[0]
                p.locals[n] = pop()
            elif op in ("iadd", "isub", "imul", "dadd", "dsub", "dmul", "ddiv", "fadd", "fsub", "fmul", "fdiv", "iand", "ior", "ixor"):
                b, a = pop(), pop(); st.append(("arith", a, op, b))
            elif op in ("ineg", "dneg", "fneg"):
                st.append(("neg", pop()))
            elif op.startswith(("anewarray", "newarray")):
                pop(); st.append("array")
            elif op.endswith("astore") and op != "astore":
                pop(); pop(); pop()
            p.pc = nxt
    return done


# ---------------------------------------------------------------- expressions -> Molang ----------------
def chain(v):
    """('call', ('call', 'this', 'm_20184_', []), 'm_165925_', []) -> ['m_20184_', 'm_165925_']"""
    names = []
    while isinstance(v, tuple) and v[0] == "call":
        names.append(v[2]); v = v[1]
    return names[::-1]


CTX = {"own_methods": set()}     # methods the entity class (or its mod superclasses) defines itself
RESERVED_INFO = {}                # Molang var -> {"java", "chain", "consts", "fields"} for the M2 runtime drivers
# Java methods with a Bedrock query that mods often OVERRIDE with their own logic (Bear.isSleeping = "sleeps at night").
# When the entity class defines them itself, the Bedrock query would be wrong: the condition becomes runtime-driven.
OVERRIDABLE = {"m_5803_", "isSleeping", "isSitting", "m_21825_", "isInSittingPose", "m_21827_", "isOrderedToSit",
               "m_5912_", "isAggressive", "isAngry", "isFlying", "isRunning", "m_20142_", "isSprinting", "isClimbing",
               "isCharging", "isAttacking", "m_6069_", "isSwimming"}


def call_consts(v):
    """String / number constants and static fields passed as arguments anywhere along a call chain."""
    consts, fields = [], []
    while isinstance(v, tuple) and v[0] == "call":
        for a in (v[3] if len(v) > 3 else []):
            if isinstance(a, tuple) and a[0] == "const": consts.append(a[1])
            if isinstance(a, tuple) and a[0] == "field": fields.append(a[2])
            if isinstance(a, tuple) and a[0] == "call":          # REGISTRY_OBJECT.get() passed as the argument
                b = a
                while isinstance(b, tuple) and b[0] == "call": b = b[1]
                if isinstance(b, tuple) and b[0] == "field": fields.append(b[2])
        v = v[1]
    return consts[::-1], fields[::-1]


def field_arg(v):
    """Last static field passed as an argument along a call chain (e.g. SynchedEntityData.get(FLEEING_ACCESSOR))."""
    found = None
    while isinstance(v, tuple) and v[0] == "call":
        for a in (v[3] if len(v) > 3 else []):
            if isinstance(a, tuple) and a[0] == "field": found = found or a[2]
        v = v[1]
    return found


def reserve(java, unknown, hint=None, v=None):
    """Unrecognised Java condition -> reserved Molang variable v.gb_<name> (0 until the M2 runtime sets it).
    Negations then behave like 'false' in Java, which keeps the else-branches (idle/walk) alive."""
    consts, fields = call_consts(v) if v is not None else ([], [])
    strs = [c for c in consts if isinstance(c, str)]
    if strs and not hint: hint = java.split(".")[-1] + "_" + strs[0]
    base = hint.lower().replace("_accessor", "").replace("data_", "") if hint else java.split(".")[-1].lower()
    name = "v.gb_" + re.sub(r"[^a-z0-9]+", "_", base).strip("_")[:40]
    unknown[java + (f"({hint})" if hint else "")] = name
    RESERVED_INFO.setdefault(name, {"java": java, "chain": java.split("."), "consts": consts, "fields": fields})
    return name


def molang_value(v, unknown):
    if isinstance(v, tuple) and v[0] == "const":
        if isinstance(v[1], bool) or not isinstance(v[1], (int, float)): return None
        x = v[1]
        return str(int(x)) if float(x).is_integer() else (f"{x:.10f}".rstrip("0").rstrip("."))   # no 1e-06: Molang has no exponents
    if isinstance(v, tuple) and v[0] == "call":
        ch = tuple(chain(v))
        for n in range(len(ch), 0, -1):
            if ch[-n:] in NUMERIC: return NUMERIC[ch[-n:]]
        last = ch[-1] if ch else ""
        if last in ("m_188503_", "nextInt") and len(v) > 3 and v[3] and isinstance(v[3][-1], tuple) and v[3][-1][0] == "const":
            n = int(v[3][-1][1]); return f"math.random_integer(0, {max(n - 1, 0)})"   # random.nextInt(n), re-rolled per frame like Java
        if last in ("m_188501_", "nextFloat", "m_188500_", "nextDouble"): return "math.random(0, 1)"
        own = len(ch) == 1 and v[1] == "this" and last in CTX["own_methods"] and last in OVERRIDABLE
        if not own:
            if ".".join(ch[-2:]) in CONDITIONS: return CONDITIONS[".".join(ch[-2:])]
            if last in CONDITIONS: return CONDITIONS[last]
        h = field_arg(v)
        if not h and len(ch) > 1:   # same method on different receivers/arguments (block above vs at feet): keep apart
            import hashlib; h = ch[-1] + "_" + hashlib.md5(repr(v).encode()).hexdigest()[:4]
        return reserve(".".join(ch), unknown, hint=h, v=v)
    if isinstance(v, tuple) and v[0] == "field":
        return reserve(v[2], unknown)
    if isinstance(v, tuple) and v[0] == "arith":
        a, b = molang_value(v[1], unknown), molang_value(v[3], unknown)
        o = {"iadd": "+", "dadd": "+", "fadd": "+", "isub": "-", "dsub": "-", "fsub": "-", "imul": "*", "dmul": "*", "fmul": "*",
             "ddiv": "/", "fdiv": "/"}.get(v[2])
        return f"({a} {o} {b})" if a and b and o else None
    return None


def molang_cond(c, unknown):
    (kind, v, extra), taken = c
    if kind == "bool":
        m = molang_value(v, unknown)
        if m is None: return None
        truth = extra if taken else not extra
        return m if truth else f"!{m}"
    if kind == "null":   # getTarget() != null etc.
        ch = chain(v)
        if ch and ch[-1] in ("m_5448_", "getTarget"):
            isnull = extra if taken else not extra
            return "!q.has_target" if isnull else "q.has_target"
        n = reserve(".".join(ch) + "_present", unknown, v=v)
        isnull = extra if taken else not extra
        return f"!{n}" if isnull else n
    if kind == "rel":
        cmpv = v
        a, b = molang_value(cmpv[1], unknown), molang_value(cmpv[2], unknown)
        if a is None or b is None: return None
        rel = extra if taken else {"==": "!=", "!=": "==", "<": ">=", ">=": "<", ">": "<=", "<=": ">"}[extra]
        return f"({a} {rel} {b})"
    return None


def path_terms(conds, unknown):
    """Conditions of one path -> list of Molang terms, or None if the path can never run (x && !x, !1)."""
    terms = []
    for x in conds:
        m = molang_cond(x, unknown)
        if m == "!1": return None
        if m and m != "1" and m not in terms: terms.append(m)
    for t in terms:
        neg = t[1:] if t.startswith("!") else "!" + t
        if neg in terms: return None
    return terms


def anim_names(a, rawfields):
    """setAnimation argument -> RawAnimation chain."""
    if isinstance(a, tuple) and a[0] == "raw": return a[1]
    if isinstance(a, tuple) and a[0] == "field":
        if a[1].endswith("DefaultAnimations") and a[2] in DEFAULT_ANIM_FIELDS: return [("thenLoop", DEFAULT_ANIM_FIELDS[a[2]])]
        return rawfields.get(a[2]) or rawfields.get(f"{a[1]}.{a[2]}") or [("unresolved", a[2])]
    if isinstance(a, tuple) and a[0] == "call" and a[2] in rawfields: return rawfields[a[2]]
    if isinstance(a, tuple) and a[0] == "call" and a[2] == "getAnimation": return [("unresolved", "getAnimation()")]
    return [("unresolved", str(a)[:60])]


# ---------------------------------------------------------------- per class --------------------------
def raw_constants(methods):
    """<clinit> / constructors: putstatic/putfield of RawAnimation chains."""
    out = {}
    for mname in ("<clinit>", "<init>") + tuple(k for k in methods if k.startswith(("<clinit>#", "<init>#"))):
        code = methods.get(mname)
        if not code: continue
        cur = None
        for off, op, rest in code:
            own, nm, desc = member(rest)
            if op == "invokestatic" and own.endswith(RAW) and nm == "begin": cur = []
            elif cur is not None and op in ("ldc", "ldc_w"): cur.append(("arg", const_of(op, rest)))
            elif cur is not None and op.startswith(("iconst", "bipush", "sipush")): cur.append(("arg", const_of(op, rest)))
            elif cur is not None and op.startswith("invoke") and own.endswith(RAW) and nm.startswith("then"):
                args = [x[1] for x in cur if x[0] == "arg"]; cur = [x for x in cur if x[0] != "arg"]
                cur.append((nm, *args))
            elif cur is not None and op in ("putstatic", "putfield") and desc.endswith(f"{RAW};"):
                out[nm] = cur; cur = None
    return out


def bootstrap_targets(verbose_dis):
    """BootstrapMethods index -> implementation method name (LambdaMetafactory) or None (string concat etc.)."""
    targets, cur = {}, None
    in_bsm = False
    FULL_TARGETS.clear(); CONCAT.clear()
    for line in verbose_dis.splitlines():
        if line.startswith("BootstrapMethods:"): in_bsm = True; continue
        if not in_bsm: continue
        m = re.match(r"^\s+(\d+): #\d+ REF_invokeStatic (\S+)", line)
        if m:
            cur = int(m.group(1)); targets[cur] = None if "LambdaMetafactory" not in m.group(2) else ""
            if "StringConcatFactory" in m.group(2): CONCAT[cur] = None
            continue
        if cur is not None and cur in CONCAT and CONCAT[cur] is None:
            mc = re.match(r"^\s+#\d+ (.*)$", line)
            if mc: CONCAT[cur] = mc.group(1).replace("\\u0001", "*").replace("\u0001", "*")
        if cur is not None and targets.get(cur) == "":
            mm = re.search(r"REF_(invoke\w+|newInvokeSpecial) ([\w/$]+)\.\"?([\w$<>]+)\"?:", line)
            if mm: targets[cur] = mm.group(3); FULL_TARGETS[cur] = (mm.group(1), mm.group(2).replace("/", "."), mm.group(3))
    return targets


FULL_TARGETS = {}   # last parsed class: bootstrap index -> (ref kind, owner class, method)
CONCAT = {}         # last parsed class: bootstrap index -> string concat recipe, '*' where a runtime value goes


def own_methods(classdir, cls):
    out, c, seen = set(), cls, 0
    while c and seen < 8:
        d = javap(classdir, c)
        if not d: break
        out |= {k.split("#")[0] for k in parse_methods(d)}
        m = re.search(r"extends ([\w.$]+)", d); c = m.group(1) if m and os.path.exists(os.path.join(classdir, m.group(1).replace(".", "/") + ".class")) else None
        seen += 1
    return out


def controllers_of(classdir, cls):
    dis = javap(classdir, cls); vdis = javap(classdir, cls, verbose=True)
    methods = parse_methods(dis)
    CTX["own_methods"] = own_methods(classdir, cls)
    rawfields = raw_constants(methods)
    boot = bootstrap_targets(vdis)
    # controllers are built in registerControllers, but also in constructors / field initialisers and kept in fields
    regs = [k for k in methods if k.split("#")[0] in ("registerControllers", "<init>")]
    out = {"class": cls, "controllers": [], "triggerable": [], "raw_constants": rawfields}
    for r in regs:
        code = methods[r]; pend = {}; last_str = None; last_field = None
        for i, (off, op, rest) in enumerate(code):
            own, nm, desc = member(rest)
            if op == "getstatic" and desc.endswith(f"{RAW};"): last_field = (own, nm)
            if op in ("ldc", "ldc_w") and isinstance(const_of(op, rest), str): pend["name"] = last_str = const_of(op, rest)
            elif op.startswith(("iconst", "bipush", "sipush")): pend["ticks"] = const_of(op, rest)
            elif op == "invokedynamic":
                mk = re.search(r"InvokeDynamic #(\d+):", rest)
                k = int(mk.group(1)) if mk else None
                pend["handler"] = boot.get(k) if k is not None else None
            elif op == "invokespecial" and own.endswith("AnimationController") and nm == "<init>":
                out["controllers"].append({"name": pend.get("name", "?"), "transition_ticks": pend.get("ticks") if "I" in desc.split(")")[0] else 0,
                                           "handler": pend.get("handler")}); pend = {"ctrl_name": pend.get("name")}
            elif op == "invokestatic" and own.endswith("DefaultAnimations") and nm in TEMPLATES:
                t = TEMPLATES[nm]
                out["controllers"].append({"name": t[0], "transition_ticks": t[1], "template": nm}); pend = {}
            elif op.startswith("invoke") and nm == "triggerableAnim":
                chain_ = None
                if last_field:
                    own_, fn_ = last_field
                    chain_ = ([("thenPlay", DEFAULT_ANIM_FIELDS[fn_])] if own_.endswith("DefaultAnimations") and fn_ in DEFAULT_ANIM_FIELDS
                              else rawfields.get(fn_))
                out["triggerable"].append({"name": last_str, "controller": pend.get("ctrl_name"), "chain": chain_})
    seen_names = set(); uniq = []
    for c in out["controllers"]:
        if c["name"] in seen_names: continue
        seen_names.add(c["name"]); uniq.append(c)
    out["controllers"] = uniq
    # analyse handlers
    for c in out["controllers"]:
        unknown = {}
        if c.get("template"):
            t = TEMPLATES[c["template"]]
            c["paths"] = []
            prior = []
            for conds, anim in t[2]:
                mol = [CONDITIONS.get(x, "0") for x in conds]
                c["paths"].append({"when": " && ".join(mol) if mol else "1", "anims": [[("thenLoop", anim)]] if anim and anim != "@arg" else ([] if anim is None else [[("unresolved", "attack argument")]]), "speed": None})
            continue
        h = c.get("handler")
        code = methods.get(h) if h else None
        if code is None:
            c["paths"] = []; c["note"] = f"handler {h} not found in class (inherited or in another class)"; continue
        paths = run_paths(code, rawfields)
        c["paths"] = []
        for p in paths:
            terms = path_terms(p.conds, unknown)
            if terms is None: continue
            if any(("!" + t) in terms or ("!(" + t.strip("()") + ")") in terms for t in terms): continue   # impossible path
            when = " && ".join(terms) if terms else "1"
            stop = isinstance(p.ret, tuple) and p.ret[0] == "field" and p.ret[2] == "STOP"
            c["paths"].append({"when": when, "anims": [] if stop else [anim_names(a, rawfields) for a in p.anims],
                               "speed": p.speed, "stop": stop})
        c["unknown_conditions"] = dict(sorted(unknown.items()))
        c["reserved_info"] = {n: RESERVED_INFO[n] for n in set(unknown.values()) if n in RESERVED_INFO}
    return out


def scan_jar(jar):
    tmp = tempfile.mkdtemp(prefix="gb_ctrl_")
    with zipfile.ZipFile(jar) as z:
        classes = []
        for n in z.namelist():
            if n.endswith(".class") and not n.startswith(GL_PREFIXES):
                b = z.read(n)
                if b"registerControllers" in b and (b"geckolib" in b or b"azurelib" in b):
                    z.extract(n, tmp); classes.append(n[:-6].replace("/", "."))
        # also extract the whole mod package so javap can resolve nothing else is needed (javap reads one class)
    return {"jar": os.path.basename(jar), "entities": [controllers_of(tmp, c) for c in sorted(classes)]}


if __name__ == "__main__":
    json.dump(scan_jar(sys.argv[1]), sys.stdout, indent=1, default=list)
