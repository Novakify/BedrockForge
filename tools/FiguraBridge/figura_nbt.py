"""Faithful Python port of Figura 0.1.6's avatar compiler (folder -> avatar NBT), for FiguraBridge.

Java source of truth (org.figuramc.figura, Figura 0.1.6 / MC 1.20.1):
    avatar/local/LocalAvatarLoader.java      loadAvatar / loadScripts / loadSounds / loadModels / loadResources
    parsers/BlockbenchParser2.java           Intermediary, TextureRepresentation, AnimationRepresentation
    parsers/BlockbenchCommonTypes.java       elements (cube/mesh/point/unknown), animators, keyframes, vecToList
    parsers/BlockbenchV4Model.java           Blockbench 4.x (groups inline in the outliner)
    parsers/BlockbenchV5Model.java           Blockbench 5.x (top-level "groups", v5 axis flips, negateLua)
    parsers/AvatarMetadataParser.java        avatar.json -> "metadata", customizations, ignoredTextures
    model/ParentType.java                    ParentType.get(name) prefix matching

NBT is represented with plain Python values so it can be written as JSON:
    CompoundTag -> dict (insertion ordered; Minecraft's CompoundTag is a HashMap so order carries no meaning)
    ListTag     -> list
    FloatTag    -> F (float subclass; serialized with float32 shortest repr, always with a '.' or exponent)
    Byte/Short/IntTag -> int  (signed, exactly the value Figura stores, e.g. mesh "fac" bytes > 127 are negative)
    putBoolean  -> bool
    StringTag   -> str
    ByteArrayTag for texture sources / sounds / resources -> FileRef (written as {"__file": relpath})
    other ByteArrayTag (collections "pr")  -> ByteArr (written as {"__bytes": [..]})
    IntArrayTag ("nr" part uuid)           -> list of int
Scripts are kept as their ORIGINAL source text (Figura stores LuaScriptParser output as a ByteArrayTag).
"""
from __future__ import annotations

import base64
import json
import math
import os
import re
import struct
import uuid as uuidlib
from pathlib import Path

FIGURA_VERSION = "0.1.6+1.20.1"  # FiguraMod.VERSION (build.gradle: mod_version + "+" + minecraft_version)

FORMAT_V4 = 1
FORMAT_V5 = 2


class FiguraCompileError(Exception):
    pass


# ----------------------------------------------------------------------------------------------------------------
# NBT value helpers
# ----------------------------------------------------------------------------------------------------------------

class F(float):
    """FloatTag. Value is already rounded to float32."""
    __slots__ = ()


class FileRef:
    """ByteArrayTag that is written to a side file instead of being inlined."""

    def __init__(self, relpath: str, data: bytes):
        self.relpath = relpath
        self.data = data


class ByteArr(list):
    """Small ByteArrayTag kept inline (collections 'pr')."""


def f32(x: float) -> float:
    """Round a Python float (double) to the nearest float32, like a Java (float) cast."""
    try:
        return struct.unpack("<f", struct.pack("<f", x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


def F32(x: float) -> F:
    return F(f32(x))


def f32_repr(v: float) -> str:
    """Shortest decimal that round-trips to the same float32 (JSON-safe)."""
    if math.isnan(v) or math.isinf(v):
        raise ValueError("non-finite")
    if v == 0:
        return "-0.0" if math.copysign(1, v) < 0 else "0.0"
    for d in range(1, 10):
        s = "%.*g" % (d, v)
        if f32(float(s)) == v:
            break
    if "e" not in s and "." not in s and "inf" not in s:
        s += ".0"
    return s


def java_float_to_string(v: float) -> str:
    """Java Float.toString / String.valueOf(Float) formatting (shortest digits; see note in report)."""
    if math.isnan(v):
        return "NaN"
    if math.isinf(v):
        return "Infinity" if v > 0 else "-Infinity"
    if v == 0:
        return "-0.0" if math.copysign(1, v) < 0 else "0.0"
    # shortest round-tripping significant digits
    for d in range(1, 10):
        s = "%.*e" % (d - 1, v)
        if f32(float(s)) == v:
            break
    mant, exp = s.split("e")
    exp = int(exp)
    neg = mant.startswith("-")
    digits = mant.lstrip("-").replace(".", "")
    digits = digits.rstrip("0") or "0"
    a = abs(v)
    if 1e-3 <= a < 1e7:
        point = exp + 1  # digits before the decimal point
        if point <= 0:
            out = "0." + "0" * (-point) + digits
        elif point >= len(digits):
            out = digits + "0" * (point - len(digits)) + ".0"
        else:
            out = digits[:point] + "." + digits[point:]
    else:
        out = digits[0] + "." + (digits[1:] or "0") + "E" + str(exp)
    return ("-" if neg else "") + out


_JAVA_WS = "".join(chr(c) for c in range(0, 0x21))
_DEC_RE = re.compile(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?[fFdD]?\Z")
_HEX_RE = re.compile(r"([+-]?)0[xX]((?:[0-9a-fA-F]+\.?[0-9a-fA-F]*|\.[0-9a-fA-F]+))[pP]([+-]?\d+)[fFdD]?\Z")


def jfloat(s) -> float | None:
    """Java Float.parseFloat: returns the float32 value, or None where Java throws NumberFormatException."""
    if s is None:
        return None
    t = s.strip(_JAVA_WS)  # String.trim(): removes chars <= U+0020
    if t in ("NaN", "+NaN", "-NaN"):
        return math.nan
    if t in ("Infinity", "+Infinity"):
        return math.inf
    if t == "-Infinity":
        return -math.inf
    if _DEC_RE.match(t):
        if t[-1] in "fFdD":
            t = t[:-1]
        return f32(float(t))
    m = _HEX_RE.match(t)
    if m:
        sign, mant, exp = m.groups()
        if "." not in mant:
            mant += "."
        return f32(float.fromhex(f"{sign}0x{mant}p{exp}"))
    return None


def is_number(v) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def gson_string(v):
    """What Gson puts into a Java String field for this JSON value."""
    if v is None:
        return None
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return v
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return repr(v)
    raise FiguraCompileError(f"expected a string, got {type(v).__name__}")


def gson_float(v, default=0.0) -> float:
    """Gson primitive float field (number or numeric string)."""
    if v is None:
        return default
    if is_number(v):
        return f32(float(v))
    if isinstance(v, str):
        r = jfloat(v)
        if r is None:
            raise FiguraCompileError(f"bad float {v!r}")
        return r
    raise FiguraCompileError(f"expected a number, got {v!r}")


def gson_int(v):
    if v is None:
        return None
    if is_number(v):
        if float(v) != int(v):
            raise FiguraCompileError(f"expected an int, got {v!r}")
        return int(v)
    if isinstance(v, str):
        return int(v.strip())
    raise FiguraCompileError(f"expected an int, got {v!r}")


def gson_bool(v):
    """@Nullable Boolean field."""
    if v is None:
        return None
    if isinstance(v, bool):
        return v
    if isinstance(v, str):
        return v.lower() == "true"
    raise FiguraCompileError(f"expected a boolean, got {v!r}")


def _float_or_zero(v) -> float:  # BlockbenchCommonTypes.floatOrZero
    return f32(float(v)) if is_number(v) else 0.0


def vec(v, n: int):
    """VEC2/3/4_DESERIALIZER: exact size required, non-numbers -> 0. Returns tuple of float32 values or None."""
    if v is None:
        return None
    if not isinstance(v, list):
        raise FiguraCompileError(f"expected a vector of {n}, got {v!r}")
    if len(v) != n:
        raise FiguraCompileError(f"Trying to parse a Vector{n}, but the list has {len(v)} items (expected {n})")
    return tuple(_float_or_zero(x) for x in v)


def is_zero(v) -> bool:
    return all(x == 0 for x in v)  # FiguraVec equals uses ==, so -0.0 equals 0


def float_arr_to_list(fa) -> list:
    """BlockbenchCommonTypes.floatArrToList: smallest of Byte/Short/Float tag lists that holds the values."""
    fa = [f32(c) for c in fa]
    typ = 0
    for c in fa:
        if math.isfinite(c) and math.fmod(c, 1) == 0:
            if c < -127 or c >= 128:
                typ = 1
            if c < -16383 or c >= 16384:
                typ = 2
                break
        else:
            typ = 2
            break
    if typ == 2:
        return [F(c) for c in fa]
    # (byte)/(short) casts of whole floats in range are exact
    return [int(c) for c in fa]


def vec_to_list(v) -> list:
    return float_arr_to_list(v)


def to_byte(i: int) -> int:
    return ((i + 128) & 0xFF) - 128


def to_short(i: int) -> int:
    return ((i + 32768) & 0xFFFF) - 32768


def to_int(i: int) -> int:
    return ((i + 2 ** 31) & 0xFFFFFFFF) - 2 ** 31


# ----------------------------------------------------------------------------------------------------------------
# ParentType (model/ParentType.java) - enum order and aliases copied verbatim
# ----------------------------------------------------------------------------------------------------------------

PARENT_TYPES = [
    ("None", ("NONE", "Model", "MODEL")),
    ("Head", ("HEAD",)),
    ("Body", ("BODY",)),
    ("LeftArm", ("LEFT_ARM",)),
    ("RightArm", ("RIGHT_ARM",)),
    ("LeftLeggingPivot", ("LEFT_LEGGING_PIVOT", "LeftLeggingPivot")),
    ("RightLeggingPivot", ("RIGHT_LEGGING_PIVOT", "RightLeggingPivot")),
    ("LeftLeg", ("LEFT_LEG",)),
    ("RightLeg", ("RIGHT_LEG",)),
    ("LeftElytraPivot", ("LEFT_ELYTRA_PIVOT", "LeftElytraPivot", "LeftWingPivot", "LEFT_WING_PIVOT")),
    ("RightElytraPivot", ("RIGHT_ELYTRA_PIVOT", "RightElytraPivot", "RightWingPivot", "RIGHT_WING_PIVOT")),
    ("LeftElytra", ("LEFT_ELYTRA", "LeftElytron", "LEFT_ELYTRON")),
    ("RightElytra", ("RIGHT_ELYTRA", "RightElytron", "RIGHT_ELYTRON")),
    ("Cape", ("CAPE",)),
    ("World", ("WORLD",)),
    ("Hud", ("HUD", "HeadsUpDisplay", "Gui", "GUI", "GraphicalUserInterface", "JraficalUserInterface")),
    ("Camera", ("CAMERA", "Billboard", "BILLBOARD")),
    ("Skull", ("SKULL", "☠")),
    ("Portrait", ("PORTRAIT",)),
    ("Arrow", ("ARROW",)),
    ("Trident", ("TRIDENT",)),
    ("Item", ("ITEM",)),
    ("LeftItemPivot", ("LEFT_ITEM_PIVOT",)),
    ("RightItemPivot", ("RIGHT_ITEM_PIVOT",)),
    ("LeftSpyglassPivot", ("LEFT_SPYGLASS_PIVOT",)),
    ("RightSpyglassPivot", ("RIGHT_SPYGLASS_PIVOT",)),
    ("LeftParrotPivot", ("LEFT_PARROT_PIVOT",)),
    ("RightParrotPivot", ("RIGHT_PARROT_PIVOT",)),
    ("HelmetItemPivot", ("HELMET_ITEM_PIVOT",)),
    ("HelmetPivot", ("HELMET_PIVOT", "HelmetPivot")),
    ("ChestplatePivot", ("CHESTPLATE_PIVOT", "ChestplatePivot", "ChestplateBodyPivot", "CHESTPLATE_BODY_PIVOT")),
    ("LeftShoulderPivot", ("LEFT_SHOULDER_PIVOT", "LeftShoulderPivot")),
    ("RightShoulderPivot", ("RIGHT_SHOULDER_PIVOT", "RightShoulderPivot")),
    ("LeggingsPivot", ("LEGGINGS_PIVOT", "LeggingsPivot", "BeltPivot", "BELT_PIVOT")),
    ("LeftBootPivot", ("LEFT_BOOT_PIVOT", "LeftBootPivot")),
    ("RightBootPivot", ("RIGHT_BOOT_PIVOT", "RightBootPivot")),
]
PARENT_TYPE_NAMES = {n for n, _ in PARENT_TYPES}


def parent_type(name) -> str:
    """ParentType.get: first enum constant whose name or any alias is a prefix of `name`."""
    if name is None:
        return "None"
    for pname, aliases in PARENT_TYPES:
        if name.startswith(pname):
            return pname
        for a in aliases:
            if name.startswith(a):
                return pname
    return "None"


def parse_parent(name, nbt: dict):
    pt = parent_type(name)
    if pt != "None":
        nbt["pt"] = pt


def png_dimensions(png: bytes):
    if len(png) < 24:
        raise FiguraCompileError("not a PNG (too short)")
    return struct.unpack(">ii", png[16:24])


# ----------------------------------------------------------------------------------------------------------------
# Blockbench parsing (BlockbenchParser2 + BlockbenchCommonTypes + V4/V5)
# ----------------------------------------------------------------------------------------------------------------

_JWS = r"[ \t\n\x0b\f\r]"
DEFINITELY_STMT = re.compile(r"^" + _JWS + r"*([:;]|break|goto|do|while|repeat|if|for|function" + _JWS +
                             r"*[^ \t\n\x0b\f\r(]|local)")
RETURN_BOUNDARY = re.compile(r"\breturn\b")


def negate_lua(source: str) -> str:
    """BlockbenchV5Model.negateLua."""
    if RETURN_BOUNDARY.search(source) or DEFINITELY_STMT.search(source):
        return source
    return "-(%s)" % source


def kf_data(s, fallback: float):
    """Keyframe.kfData: blank -> fallback, parseable -> float, otherwise the original string."""
    if s is None or s.strip() == "":
        return fallback
    r = jfloat(s)
    return s if r is None else r


def kf_data_to_nbt(dp: dict, channel: str, inv_x: bool, inv_y: bool) -> list:
    """Keyframe3.Data.toNBT."""
    fallback = 1.0 if channel == "scale" else 0.0
    x, y, z = (kf_data(gson_string(dp.get(k)), fallback) for k in ("x", "y", "z"))
    if all(isinstance(c, float) for c in (x, y, z)):
        return [F32(x * (-1 if inv_x else 1)), F32(y * (-1 if inv_y else 1)), F32(z)]

    def s(c):  # String.valueOf(Object): Float -> Float.toString
        return java_float_to_string(c) if isinstance(c, float) else c

    return [negate_lua(s(x)) if inv_x else s(x), negate_lua(s(y)) if inv_y else s(y), s(z)]


V5_ROT_TRANS = (-1.0, -1.0, 1.0)
V5_POS_TRANS = (-1.0, 1.0, 1.0)
LEFT_TIMING = (f32(-0.1),) * 3
RIGHT_TIMING = (f32(0.1),) * 3


def animator_nbt(animator: dict, anim_global_id: int, is_v5: bool):
    """BlockbenchCommonTypes.Animator.getNBT (per-part keyframes, attached to the group's 'anim' list)."""
    if animator.get("type") == "effect":
        raise FiguraCompileError("Shouldn't be attaching FX animators to parts!")
    rot, pos, scl = [], [], []
    for kf in animator.get("keyframes") or []:
        channel = kf.get("channel") if isinstance(kf.get("channel"), str) else None
        if channel not in ("position", "rotation", "scale"):
            continue  # not a Keyframe3
        tag = {"time": F32(gson_float(kf.get("time"))),
               "int": gson_string(kf.get("interpolation")) if kf.get("interpolation") is not None else "linear"}
        dps = kf.get("data_points") or []
        if not dps:
            raise FiguraCompileError("keyframe without data_points")
        inv_x = is_v5 and channel in ("rotation", "position")
        inv_y = is_v5 and channel == "rotation"
        trans = (V5_ROT_TRANS if channel == "rotation" else V5_POS_TRANS if channel == "position" else (1.0, 1.0, 1.0)) \
            if is_v5 else (1.0, 1.0, 1.0)
        tag["pre"] = kf_data_to_nbt(dps[0], channel, inv_x, inv_y)
        if len(dps) > 1:
            tag["end"] = kf_data_to_nbt(dps[1], channel, inv_x, inv_y)
        bl, br = vec(kf.get("bezier_left_value"), 3), vec(kf.get("bezier_right_value"), 3)
        blt, brt = vec(kf.get("bezier_left_time"), 3), vec(kf.get("bezier_right_time"), 3)
        if bl is not None and not is_zero(bl):
            tag["bl"] = vec_to_list([a * b for a, b in zip(bl, trans)])
        if br is not None and not is_zero(br):
            tag["br"] = vec_to_list([a * b for a, b in zip(br, trans)])
        if blt is not None and blt != LEFT_TIMING:
            tag["blt"] = vec_to_list(blt)
        if brt is not None and brt != RIGHT_TIMING:
            tag["brt"] = vec_to_list(brt)
        {"position": pos, "rotation": rot, "scale": scl}[channel].append(tag)
    channels = {}
    if rot:
        channels["grot" if animator.get("rotation_global") is True else "rot"] = rot
    if pos:
        channels["pos"] = pos
    if scl:
        channels["scl"] = scl
    if not channels:
        return None
    return {"id": anim_global_id, "data": channels}


class TextureRep:
    """BlockbenchParser2.Intermediary.TextureRepresentation."""

    def __init__(self, ctx: "Intermediary", tex: dict):
        name = gson_string(tex.get("name"))
        if name is None:
            raise FiguraCompileError("texture without a name")
        if name.endswith(".png"):
            name = name[:-4]
        # "ugh why do we have to perpetuate this garbage"
        self.type = "e" if name.endswith("_e") else "n" if name.endswith("_n") else "s" if name.endswith("_s") else "d"
        self.file: Path | None = None  # absolute source file, or None when bundled (base64)
        rel = gson_string(tex.get("relative_path"))
        try:
            if rel is None:
                raise LookupError("load from source")
            # Blockbench writes '/' but Windows Figura also accepts '\' (Path.resolve); we normalise both.
            rel_n = rel.replace("\\", "/")
            p = Path(os.path.realpath(ctx.model_path.parent / rel_n))
            root = ctx.avatar_root
            inside = lambda q: q == root or root in q.parents
            if not p.exists() or (len(root.parts) - 1 > 1 and not inside(p)):
                # <= 4.9 wrote paths relative to the .bbmodel FILE (modelPath.resolve, not its parent)
                if rel_n.startswith("../"):
                    p = Path(os.path.realpath(ctx.model_path / rel_n))
            if not p.exists():
                raise FileNotFoundError(f"Could not locate texture '{tex.get('name')}'")
            if len(root.parts) - 1 > 1 and not inside(p):
                raise PermissionError(f"Texture '{tex.get('name')}' is a reference outside the avatar folder")
            self.source = p.read_bytes()
            relp = p.relative_to(root)
            self.path = ".".join(relp.parts)[:-4]  # separators -> '.', drop 4 chars (extension)
            name = ctx.located_within + name
            self.file = p
        except Exception as e:  # Figura catches everything and falls back to the bundled base64
            src = gson_string(tex.get("source"))
            if src is None or not src.startswith("data:image/png;base64,"):
                raise FiguraCompileError(
                    f"Failed to find the {tex.get('name')} texture: {e}\n(and the bundled texture data was missing or bad)")
            self.source = base64.b64decode(src[len("data:image/png;base64,"):])
            self.path = ctx.located_within + ctx.name + "." + name
        if self.type != "d":
            name = name[:-2]
        self.name = name
        if tex.get("width") is not None:
            # (float) width / uv_width   (Float fields; NPE in Figura when uv_width is missing)
            w, h = gson_float(tex.get("width")), gson_float(tex.get("height"))
            uw, uh = gson_float(tex.get("uv_width"), None), gson_float(tex.get("uv_height"), None)
            if uw is None or uh is None:
                raise FiguraCompileError(f"texture {tex.get('name')}: width without uv_width")
            self.fixed_size = (f32(w / uw) if uw else math.copysign(math.inf, w) if w else math.nan,
                               f32(h / uh) if uh else math.copysign(math.inf, h) if h else math.nan)
        else:
            iw, ih = png_dimensions(self.source)
            self.fixed_size = (f32(iw / ctx.default_res[0]), f32(ih / ctx.default_res[1]))
        self.global_id = -1
        self.local_id = -1


class AnimationRep:
    """BlockbenchParser2.Intermediary.AnimationRepresentation."""

    def __init__(self, ctx: "Intermediary", global_id: int, anim: dict):
        self.global_id = global_id
        lw = ctx.located_within
        self.mdl = ctx.name if lw.strip() == "" else lw + ctx.name
        self.name = gson_string(anim.get("name"))
        self.loop = gson_string(anim.get("loop")) if anim.get("loop") is not None else "once"
        self.override = anim.get("override") is True
        self.length = gson_float(anim.get("length"))
        self.offset = parse_float_or(gson_string(anim.get("anim_time_update")), 0.0)
        self.blend = parse_float_or(gson_string(anim.get("blend_weight")), 1.0)
        self.start_delay = parse_float_or(gson_string(anim.get("start_delay")), 0.0)
        self.loop_delay = parse_float_or(gson_string(anim.get("loop_delay")), 0.0)
        animators = anim.get("animators")
        if animators is not None:
            filtered = dict(animators)
            self.fx = filtered.pop("effects", None)
            self.part_animators = filtered
        else:
            self.fx = None
            self.part_animators = {}
        for key in self.part_animators:
            ctx.animations_by_element.setdefault(key, []).append(self)  # LinkedHashSet in animation order

    def anim_nbt(self) -> dict:
        tag = {"mdl": self.mdl, "name": self.name}
        if self.loop != "once":
            tag["loop"] = self.loop
        if self.override:
            tag["ovr"] = True
        if self.length != 0:
            tag["len"] = F32(self.length)
        if self.offset != 0:
            tag["off"] = F32(self.offset)
        if self.blend != 1:
            tag["bld"] = F32(self.blend)
        if self.start_delay != 0:
            tag["sdel"] = F32(self.start_delay)
        if self.loop_delay != 0:
            tag["ldel"] = F32(self.loop_delay)
        if self.fx is not None:  # only Instruction ("timeline") keyframes survive; sound/particle keyframes are dropped
            code = []
            for kf in self.fx.get("keyframes") or []:
                if kf.get("channel") != "timeline":  # InstructionKeyframe is bound to exactly "timeline"
                    continue
                dps = kf.get("data_points") or []
                if not dps or gson_string(dps[0].get("script")) is None:
                    raise FiguraCompileError(f"animation {self.name}: instruction keyframe without script")
                code.append({"time": F32(gson_float(kf.get("time"))), "src": gson_string(dps[0].get("script"))})
            tag["code"] = code
        return tag


def parse_float_or(s, fallback: float) -> float:
    if s is None or s.strip() == "":
        return fallback
    r = jfloat(s)
    return fallback if r is None else r


class Intermediary:
    """BlockbenchParser2.Intermediary - per-model state."""

    def __init__(self, parser: "BlockbenchParser2", options: dict):
        self.parser = parser
        self.parts_with_uuids = bool(options.get("partsWithUUIDs")) if options else False
        self.name = ""
        self.model_path: Path = Path()
        self.avatar_root: Path = Path()
        self.located_within = ""
        self.default_res = (16, 16)
        self.textures: list[TextureRep] = []
        self.texture_names: dict[str, dict[str, TextureRep]] = {}
        self.elements: dict[str, dict] = {}
        self.referents: dict[str, tuple[str, dict]] = {}  # uuid -> ("element"|"group", json)
        self.animations: list[AnimationRep] = []
        self.animations_by_element: dict[str, list[AnimationRep]] = {}
        self.collections: list[str] = []
        self.collections_by_element: dict[str, set[int]] = {}

    # --- loaders ---
    def load_textures(self, textures):
        for i, t in enumerate(textures):
            rep = TextureRep(self, t)
            rep.local_id = i
            v = self.texture_names.get(rep.name)
            if v is None:
                rep.global_id = self.parser.next_texture
                self.parser.next_texture += 1
                v = {}
                self.texture_names[rep.name] = v
            else:
                rep.global_id = next(iter(v.values())).global_id
            v[rep.type] = rep
            self.textures.append(rep)

    def load_animations(self, animations):
        for a in animations:
            self.animations.append(AnimationRep(self, self.parser.next_animation, a))
            self.parser.next_animation += 1

    def load_collections(self, collections):
        for i, c in enumerate(collections):
            self.collections.append(gson_string(c.get("name")))
            for child in c.get("children") or []:
                self.collections_by_element.setdefault(child, set()).add(i)

    def texture_global_id(self, local_id):
        if local_id is None or local_id < 0 or local_id >= len(self.textures):
            return None
        return self.textures[local_id].global_id

    # --- outputs ---
    def textures_nbt(self) -> dict:
        sources, build = {}, {}
        for t in self.textures:
            sources[t.path] = t  # resolved to a FileRef by the loader
            entry = build.setdefault(t.name, {})
            if t.type in entry:
                raise FiguraCompileError(f'Model "{self.name}" contains texture with duplicate name "{t.name}"')
            entry[t.type] = t.path
        return {"src": sources, "data": list(build.values())}

    def animations_nbt(self) -> list:
        return [a.anim_nbt() for a in self.animations]

    def attach_collections(self, uuid, tag):
        cols = sorted(self.collections_by_element.get(uuid, ()))  # HashSet<Integer> iteration ~ ascending
        if cols:
            tag["pr"] = ByteArr(to_byte(c) for c in cols)

    # --- elements (BlockbenchCommonTypes.Element and subclasses) ---
    def element_nbt(self, el: dict):
        etype = el.get("type") if isinstance(el.get("type"), str) else None
        if etype not in ("cube", "mesh", "locator", "null_object"):
            return None  # UnknownElement: never exported (armature_bone, texture_mesh, text, missing type ...)
        if el.get("export") is False:
            return None
        name = gson_string(el.get("name"))
        if name is None:
            raise FiguraCompileError("element without a name")
        tag = {"name": name}
        uid = gson_string(el.get("uuid"))
        if self.parts_with_uuids:
            try:
                u = uuidlib.UUID(uid)
                msb, lsb = u.int >> 64, u.int & ((1 << 64) - 1)
                tag["nr"] = [to_int(msb >> 32), to_int(msb), to_int(lsb >> 32), to_int(lsb)]
            except (ValueError, TypeError):
                tag["nr"] = uid
        rotation, origin = vec(el.get("rotation"), 3), vec(el.get("origin"), 3)
        if rotation is not None and not is_zero(rotation):
            tag["rot"] = vec_to_list(rotation)
        if origin is not None and not is_zero(origin):
            tag["piv"] = vec_to_list(origin)
        if gson_bool(el.get("visibility")) is False:
            tag["vsb"] = False
        self.attach_collections(uid, tag)

        if etype == "cube":
            self._cube(el, tag)
        elif etype == "mesh":
            self._mesh(el, tag, origin)
        else:  # PointElement
            position = vec(el.get("position"), 3)
            if position is not None and origin is None and not is_zero(position):
                tag["piv"] = vec_to_list(position)
            parse_parent(name, tag)
        return tag

    def _cube(self, el, tag):
        frm, to = vec(el.get("from"), 3), vec(el.get("to"), 3)
        if frm is not None and not is_zero(frm):
            tag["f"] = vec_to_list(frm)
        if to is not None and not is_zero(to):
            tag["t"] = vec_to_list(to)
        inflate = gson_float(el.get("inflate"))
        if inflate != 0:
            tag["inf"] = F32(inflate)
        faces = el.get("faces")
        if faces is None:
            raise FiguraCompileError(f"cube {tag['name']}: no faces")
        cube_data = {}
        for face in ("north", "south", "west", "east", "up", "down"):  # CubeFace.FACES order
            fd = faces.get(face)
            if fd is None:
                continue
            fn = self._cube_face(fd)
            if fn is not None:
                cube_data[face[0]] = fn
        tag["cube_data"] = cube_data

    def _cube_face(self, fd):
        tex = gson_int(fd.get("texture"))
        if tex is None:
            return None
        gid = self.texture_global_id(tex)
        if gid is None:
            return None
        out = {"tex": gid}
        rot = gson_float(fd.get("rotation"))
        if rot != 0:
            out["rot"] = F32(rot)
        uv = vec(fd.get("uv"), 4)
        if uv is not None and not is_zero(uv):
            sx, sy = self.textures[tex].fixed_size
            out["uv"] = vec_to_list([sx * uv[0], sy * uv[1], sx * uv[2], sy * uv[3]])  # double math, then (float)
        return out

    def _mesh(self, el, tag, origin):
        if el.get("shading") == "smooth":
            tag["smo"] = True
        vertices = el.get("vertices")
        faces = el.get("faces")
        if vertices is None or faces is None or origin is None:
            raise FiguraCompileError(f"mesh {tag['name']}: missing vertices/faces/origin")
        vert2idx, vert2pos, vtx = {}, {}, []
        for i, (k, v) in enumerate(vertices.items()):
            p = vec(v, 3)
            combined = (p[0] + origin[0], p[1] + origin[1], p[2] + origin[2])  # double math
            vert2idx[k] = i
            vert2pos[k] = combined
            vtx += [F32(c) for c in combined]
        n = len(vert2idx)
        int_type = 2 if n > 0xFFFF else 1 if n > 0xFF else 0
        tex_l, uvs, fac = [], [], []
        for face in faces.values():
            ftex = gson_int(face.get("texture"))
            fverts = face.get("vertices")
            fuv = face.get("uv")
            if ftex is None or fverts is None or fuv is None or len(fverts) < 3 or len(fverts) > 4:
                continue
            gid = self.texture_global_id(ftex)
            if gid is None:
                continue
            tex_l.append(to_short((gid << 4) + len(fverts)))
            fverts = list(fverts)
            if len(fverts) == 4:
                fverts = mesh_face_reorder(fverts, vert2pos)
            sx, sy = self.textures[ftex].fixed_size
            for vid in fverts:
                idx = vert2idx[vid]
                fac.append(to_byte(idx) if int_type == 0 else to_short(idx) if int_type == 1 else idx)
                uv = vec(fuv.get(vid), 2)
                if uv is None:
                    raise FiguraCompileError(f"mesh {tag['name']}: face vertex {vid} without uv")
                uvs += [F32(uv[0] * sx), F32(uv[1] * sy)]
        tag["mesh_data"] = {"vtx": vtx, "tex": tex_l, "fac": fac, "uvs": uvs}


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _opposite_sides(line1, line2, p1, p2) -> bool:
    """MeshFace.testOppositeSides."""
    l = _sub(line2, line1)
    return _dot(_cross(l, _sub(p1, line1)), _cross(l, _sub(p2, line1))) < 0


def mesh_face_reorder(v, pos):
    """MeshFace.reorder (quads only)."""
    v1, v2, v3, v4 = (pos[k] for k in v)
    if _opposite_sides(v2, v3, v1, v4):
        return [v[2], v[0], v[1], v[3]]
    if _opposite_sides(v1, v2, v3, v4):
        return [v[0], v[2], v[1], v[3]]
    return v


_FORMAT_VERSION = re.compile(r"^(\d+?)\.")


class BlockbenchParser2:
    def __init__(self):
        self.next_texture = 0
        self.next_animation = 0

    def parse_model(self, avatar_root: Path, model_path: Path, text: str, model_name: str, located_within: str,
                    options: dict | None):
        try:
            model = json.loads(text)
            meta = model["meta"]
            version = gson_string(meta["format_version"])
            m = _FORMAT_VERSION.search(version)
            if not m:
                raise FiguraCompileError("No match on version number")
            if m.group(1) not in ("4", "5"):
                raise FiguraCompileError(f"No match on '{m.group(1)}'")
            model_format = gson_string(meta["model_format"])
            if not (model_format == "free" or "figura" in model_format):
                raise FiguraCompileError(
                    f'Model "{model_name}" has an incompatible model format: "{model_format}".\n'
                    'Compatibility is limited to the "Generic Model" format and third-party Figura-specific formats.')
            ctx = Intermediary(self, options or {})
            ctx.name, ctx.avatar_root, ctx.model_path, ctx.located_within = model_name, avatar_root, model_path, located_within
            res = model["resolution"]
            ctx.default_res = (gson_int(res["width"]), gson_int(res["height"]))
            nbt = (self._convert_v5 if m.group(1) == "5" else self._convert_v4)(ctx, model)
            return ctx.textures_nbt(), ctx.animations_nbt(), nbt, ctx
        except Exception as e:
            raise FiguraCompileError(f'model "{model_name}": {e}') from e

    # ---- shared group body (V4 Group.toNBT / V5 Container.toNBT are identical apart from the v5 flag) ----
    @staticmethod
    def _group_nbt(ctx: Intermediary, g: dict, uid: str, children_nbt, is_v5: bool):
        visibility = gson_bool(g.get("visibility"))
        name = gson_string(g.get("name"))
        tag = {"name": name}
        if visibility is False:
            tag["vsb"] = False
        origin, rotation = vec(g.get("origin"), 3), vec(g.get("rotation"), 3)
        if origin is not None and not is_zero(origin):
            tag["piv"] = vec_to_list(origin)
        if rotation is not None and not is_zero(rotation):
            tag["rot"] = vec_to_list(rotation)
        parse_parent(name, tag)
        chld = []
        for ct in children_nbt:
            if ct is None:
                continue
            # do not propagate 'vsb' for children with the same value (vsb is only ever written as false,
            # and Objects.equals(Boolean group.visibility, false) is true only when the group is explicitly hidden)
            if "vsb" in ct and visibility is not None and visibility == ct["vsb"]:
                del ct["vsb"]
            chld.append(ct)
        if chld:
            tag["chld"] = chld
        anims = ctx.animations_by_element.get(uid)
        if anims is not None:
            lst = []
            for a in anims:
                att = animator_nbt(a.part_animators[uid], a.global_id, is_v5)
                if att is not None:
                    lst.append(att)
            tag["anim"] = lst
        ctx.attach_collections(uid, tag)
        return tag

    @staticmethod
    def _model_root(ctx: Intermediary, chld: list) -> dict:
        tag = {"name": ctx.name}
        parse_parent(ctx.name, tag)
        tag["chld"] = chld
        tag["cn"] = list(ctx.collections)
        return tag

    # ---- Blockbench 5 ----
    def _convert_v5(self, ctx: Intermediary, model: dict) -> dict:
        elements = model.get("elements") or []
        groups = model.get("groups") or []
        ctx.load_textures(model.get("textures") or [])
        ctx.load_animations(model.get("animations") or [])
        for e in elements:
            ctx.elements[e.get("uuid")] = e
        for e in elements:
            ctx.referents[e.get("uuid")] = ("element", e)
        for g in groups:
            ctx.referents[g.get("uuid")] = ("group", g)
        ctx.load_collections(model.get("collections") or [])

        def item(it):
            if isinstance(it, str):  # OutlinerItem.Element: context.elements.get(uuid).toNBT (NPE if missing)
                el = ctx.elements.get(it)
                if el is None:
                    raise FiguraCompileError(f"outliner references missing element {it}")
                return ctx.element_nbt(el)
            if isinstance(it, dict):  # Container
                uid = gson_string(it.get("uuid"))
                ref = ctx.referents.get(uid)
                children = it.get("children")
                if children is None:
                    raise FiguraCompileError(f"outliner group {uid} without children")
                if ref is None or ref[0] != "group":
                    # Figura logs "Broken reference" and drops the whole subtree
                    ctx.parser_warnings.append(f"broken outliner reference {uid}")
                    return None
                g = ref[1]
                if g.get("export") is False:
                    return None
                return self._group_nbt(ctx, g, uid, (item(c) for c in children), True)
            raise FiguraCompileError("Expecting an outliner item (string or object)")

        ctx.parser_warnings = []
        chld = [t for t in (item(it) for it in model.get("outliner") or []) if t is not None]
        tag = self._model_root(ctx, chld)
        tag["_v"] = FORMAT_V5
        return tag

    # ---- Blockbench 4 ----
    def _convert_v4(self, ctx: Intermediary, model: dict) -> dict:
        elements = model.get("elements") or []
        ctx.load_textures(model.get("textures") or [])
        for e in elements:
            ctx.elements[e.get("uuid")] = e
        for e in elements:
            ctx.referents[e.get("uuid")] = ("element", e)

        def fill(it):
            if isinstance(it, dict):
                ctx.referents[gson_string(it.get("uuid"))] = ("group", it)
                for c in it.get("children") or []:
                    fill(c)

        for it in model.get("outliner") or []:
            fill(it)
        ctx.load_animations(model.get("animations") or [])
        ctx.load_collections(model.get("collections") or [])
        ctx.parser_warnings = []

        def item(it):
            if isinstance(it, dict):  # Group (outliner object)
                if it.get("export") is False:
                    return None
                uid = gson_string(it.get("uuid"))
                children = it.get("children")
                kids = (item(c) for c in children) if children is not None else ()
                g = self._group_nbt(ctx, it, uid, kids, False)
                return g
            el = ctx.elements.get(gson_string(it))
            return None if el is None else ctx.element_nbt(el)

        chld = [t for t in (item(it) for it in model.get("outliner") or []) if t is not None]
        return self._model_root(ctx, chld)


# ----------------------------------------------------------------------------------------------------------------
# Metadata (AvatarMetadataParser)
# ----------------------------------------------------------------------------------------------------------------

_VERSION_RE = re.compile(
    r"^(?P<major>0|[1-9]\d*)(?:\.(?P<minor>0|[1-9]\d*)(?:\.(?P<patch>0|[1-9]\d*))?)?"
    r"(?:-(?P<pre>(?:0|[1-9]\d*|\d*[a-zA-Z-][\da-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][\da-zA-Z-]*))*))?"
    r"(?:\+(?P<build>[\da-zA-Z-]+(?:\.[\da-zA-Z-]+)*))?$")


def version_string(v) -> str | None:
    """utils/Version: toString(), or None when invalid."""
    if v is None:
        return None
    m = _VERSION_RE.fullmatch(v)
    if not m:
        return None
    s = f"{int(m['major'])}.{int(m['minor'] or 0)}.{int(m['patch'] or 0)}"
    if m["pre"] and m["pre"].strip():
        s += "-" + m["pre"]
    if m["build"] and m["build"].strip():
        s += "+" + m["build"]
    return s


def _java_split_slash(s: str) -> list:
    parts = s.split("/")
    while parts and parts[-1] == "":  # Java String.split drops trailing empty strings
        parts.pop()
    return parts if parts else [""] if s == "" else parts


def compute_safe_string(name: str) -> str:
    """PathUtils.computeSafeString(String) -> getPath (PermissivePath) -> normalize -> toString -> strip '^/+'."""
    if name == "":
        return ""
    p = name if name.startswith(".") else "/" + name
    p = p.replace("\\", "/")
    p = re.sub(r"[.]([^./])", r"/\1", p)
    p = p.replace("//", "/")
    chunks = _java_split_slash(p)
    absolute = p.startswith("/")
    if chunks and absolute:
        chunks = chunks[1:]
    if chunks and chunks[-1] == "":
        chunks = chunks[:-1]
    norm = []
    for c in chunks:  # PermissivePath.internalNormalize
        if c == ".." and norm:
            norm.pop()
            continue
        if c == ".":
            continue
        norm.append(c)
    s = ("/" if absolute else "") + "/".join(norm)
    return re.sub(r"^/+", "", s.replace("\\", "/"))


def lenient_json(text: str):
    """Gson is lenient: allow // and /* */ comments (avatar.jsonc) and a UTF-8 BOM."""
    text = text.lstrip("﻿")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        out, i, n, in_str = [], 0, len(text), False
        while i < n:
            c = text[i]
            if in_str:
                out.append(c)
                if c == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
                out.append(c)
            elif text.startswith("//", i) or c == "#":
                while i < n and text[i] not in "\r\n":
                    i += 1
                continue
            elif text.startswith("/*", i):
                j = text.find("*/", i + 2)
                i = n if j < 0 else j + 2
                continue
            else:
                out.append(c)
            i += 1
        return json.loads(re.sub(r",(\s*[}\]])", r"\1", "".join(out)))


def metadata_nbt(meta: dict, raw: dict, filename: str) -> dict:
    """AvatarMetadataParser.parse."""
    nbt = {}
    if isinstance(raw, dict):
        for k, v in raw.items():
            if k and k.strip() and "badge_color_" in k:
                nbt[k] = gson_string(v)
    ver = version_string(gson_string(meta.get("version"))) or FIGURA_VERSION
    name = gson_string(meta.get("name"))
    nbt["name"] = filename if name is None or name.strip() == "" else name
    nbt["ver"] = ver
    for src, dst in (("color", "color"), ("background", "bg"), ("id", "id")):
        if meta.get(src) is not None:
            nbt[dst] = gson_string(meta[src])
    if meta.get("authors") is not None:
        nbt["authors"] = "\n".join(gson_string(a) or "null" for a in meta["authors"])
    else:
        a = gson_string(meta.get("author"))
        nbt["authors"] = "?" if a is None else a
    if meta.get("autoScripts") is not None:
        nbt["autoScripts"] = [compute_safe_string(re.sub(r"\.lua$", "", gson_string(s))) for s in meta["autoScripts"]]
    # Configs.FORMAT_SCRIPT defaults to 1 (< 2) -> no "minify" flag
    if meta.get("autoAnims") is not None:
        nbt["autoAnims"] = [gson_string(s) for s in meta["autoAnims"]]
    if meta.get("resources") is not None:
        nbt["resources_paths"] = [gson_string(s) for s in meta["resources"]]
    return nbt


RENDER_TYPES = {"NONE", "CUTOUT", "CUTOUT_CULL", "CUTOUT_EMISSIVE_SOLID", "TRANSLUCENT", "TRANSLUCENT_CULL", "EMISSIVE",
                "EMISSIVE_SOLID", "EYES", "END_PORTAL", "END_GATEWAY", "TEXTURED_PORTAL", "GLINT", "TEXTURED_GLINT",
                "LINES", "LINES_STRIP", "SOLID", "BLURRY"}  # model/rendering/texture/RenderTypes.java


def _render_type(name: str) -> str:
    rt = name.upper()
    if rt not in RENDER_TYPES:
        raise FiguraCompileError(f'Invalid render type "{name}"!')
    return rt


def _get_tag(models: dict, path: str, remove: bool) -> dict:
    """AvatarMetadataParser.getTag."""
    keys = re.sub(r"^models", "", path, count=1).split(".")
    while keys and keys[-1] == "":
        keys.pop()  # Java split drops trailing empties
    current = models
    for i, key in enumerate(keys):
        if key == "":
            continue
        if "chld" not in current:
            raise FiguraCompileError(f'Invalid part path: "{path}"')
        children = current["chld"]
        j = 0
        while j < len(children):
            child = children[j]
            if child.get("name", "") == key:
                current = child
                break
            if j == len(children) - 1:
                raise FiguraCompileError(f'Invalid part path: "{path}"')
            j += 1
        if remove and i == len(keys) - 1:
            del children[j]
    return current


def inject_to_models(meta: dict, models: dict):
    moves = {}
    for path, c in (meta.get("customizations") or {}).items():
        remove = c.get("remove") is True
        part = _get_tag(models, path, remove)
        if remove:
            continue
        if c.get("primaryRenderType") is not None:
            part["primary"] = _render_type(c["primaryRenderType"])
        if c.get("secondaryRenderType") is not None:
            part["secondary"] = _render_type(c["secondaryRenderType"])
        if c.get("parentType") is not None:
            pt = parent_type(c["parentType"])
            if pt == "None":
                part.pop("pt", None)
            else:
                part["pt"] = pt
        if c.get("moveTo") is not None:
            moves[path] = c["moveTo"]
        if c.get("visible") is not None:
            if c["visible"]:
                part.pop("vsb", None)
            else:
                part["vsb"] = False
        if c.get("smooth") is not None:
            part["smo"] = bool(c["smooth"])
    for src, dst in moves.items():  # PARTS_TO_MOVE is a HashMap: Figura's order is unspecified, we use JSON order
        part = _get_tag(models, src, True)
        target = _get_tag(models, dst, False)
        target.setdefault("chld", []).append(part)


def inject_to_textures(meta: dict, textures: dict):
    ign = meta.get("ignoredTextures")
    if not ign:
        return
    src = textures.get("src", {})
    for t in ign:
        rep = src.get(t)
        data = rep.data if isinstance(rep, FileRef) else b""
        w, h = png_dimensions(data)
        src[t] = [w, h]  # ListTag of IntTag: Figura creates a blank texture of this size


# ----------------------------------------------------------------------------------------------------------------
# LocalAvatarLoader
# ----------------------------------------------------------------------------------------------------------------

def _is_hidden(p: Path) -> bool:
    return p.name.startswith(".")


def _list_paths(d: Path):
    try:
        return sorted(d.iterdir(), key=lambda p: p.name.encode("utf-16-be"))  # String.compareTo (UTF-16 units)
    except OSError:
        return None


def files_by_extension(root: Path, ext: str) -> list:
    out = []
    for c in _list_paths(root) or []:
        if _is_hidden(c):
            continue
        if c.is_dir():
            out += files_by_extension(c, ext)
        elif str(c).lower().endswith(ext.lower()):
            out.append(c)
    return out


class CompiledAvatar:
    def __init__(self):
        self.nbt: dict = {}
        self.texture_reps: list[TextureRep] = []  # every TextureRepresentation, in global load order
        self.warnings: list[str] = []


def _dotted(root: Path, p: Path) -> str:
    return ".".join(p.relative_to(root).parts)[:-4]


def compile_avatar(avatar_dir: Path, owner_uuid: str = "00000000-0000-0000-0000-000000000000") -> CompiledAvatar:
    """LocalAvatarLoader.loadAvatar, minus the Lua minifier."""
    root = Path(os.path.realpath(avatar_dir))
    res = CompiledAvatar()
    nbt = res.nbt

    # scripts (original source text, see module docstring)
    scripts = files_by_extension(root, ".lua")
    if scripts:
        nbt["scripts"] = {_dotted(root, s): s.read_bytes().decode("utf-8", errors="replace") for s in scripts}

    # custom sounds
    sounds = files_by_extension(root, ".ogg")
    if sounds:
        nbt["sounds"] = {}
        for s in sounds:
            name = _dotted(root, s)
            nbt["sounds"][name] = FileRef(f"sounds/{name}.ogg", s.read_bytes())

    # metadata part 1
    mj = root / "avatar.jsonc"
    if not mj.exists():
        mj = root / "avatar.json"
    raw_text = mj.read_bytes().decode("utf-8")
    raw = lenient_json(raw_text) if raw_text.strip() else None
    meta = raw if isinstance(raw, dict) else {}
    nbt["metadata"] = metadata_nbt(meta, raw, root.name)
    nbt["metadata"]["uuid"] = owner_uuid

    # models
    textures, animations = {}, []
    parser = BlockbenchParser2()
    load_options = meta.get("loadOptions")

    def load_models(current: Path, folders: str) -> dict:
        result, children = {}, []
        for f in _list_paths(current) or []:
            if _is_hidden(f):
                continue
            name = f.name
            if f.is_dir():
                sub = load_models(f, folders + name + ".")
                if sub:
                    sub["name"] = name
                    parse_parent(name, sub)
                    children.append(sub)
            elif str(f).lower().endswith(".bbmodel"):
                tex_nbt, anim_list, model_nbt, ctx = parser.parse_model(
                    root, f, f.read_bytes().decode("utf-8").lstrip("﻿"), name[:-8], folders, load_options)
                res.warnings += [f"{name}: {w}" for w in getattr(ctx, "parser_warnings", [])]
                res.texture_reps += ctx.textures
                children.append(model_nbt)
                animations.extend(anim_list)
                if not textures:
                    textures["data"] = []
                    textures["src"] = {}
                textures["data"].extend(tex_nbt["data"])
                for k, rep in tex_nbt["src"].items():
                    textures["src"][k] = FileRef(f"textures/{k}.png", rep.source)
        if children:
            result["chld"] = children
        return result

    models = load_models(root, "")
    models["name"] = "models"

    # metadata part 2: customizations
    inject_to_models(meta, models)
    inject_to_textures(meta, textures)

    if models:
        nbt["models"] = models
    if textures:
        nbt["textures"] = textures
    if animations:
        nbt["animations"] = animations

    md = nbt["metadata"]
    if "resources_paths" in md:
        _load_resources(nbt, md["resources_paths"], root)
        md.pop("resource_paths", None)  # sic: Figura removes the misspelled key, so resources_paths stays
    return res


def _load_resources(nbt: dict, globs: list, root: Path):
    """LocalAvatarLoader.loadResources: glob-matched files, gzipped, keyed by unix relative path."""
    import fnmatch
    import gzip
    out = {}
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        if any(_java_glob(g, rel) for g in globs):
            out[rel] = FileRef(f"resources/{rel}.gz", gzip.compress(p.read_bytes()))
    nbt["resources"] = out


def _java_glob(pattern: str, rel: str) -> bool:
    """Approximation of Java's glob PathMatcher ('*' within a segment, '**' across segments)."""
    rx, i = "", 0
    while i < len(pattern):
        c = pattern[i]
        if pattern.startswith("**", i):
            rx += ".*"
            i += 2
            continue
        rx += {"*": "[^/]*", "?": "[^/]"}.get(c, re.escape(c))
        i += 1
    return re.fullmatch(rx, rel) is not None


# ----------------------------------------------------------------------------------------------------------------
# JSON writer
# ----------------------------------------------------------------------------------------------------------------

def _enc(v, out: list, ind: str, files: dict):
    nl = "\n" + ind + " "
    if isinstance(v, bool):
        out.append("true" if v else "false")
    elif isinstance(v, F):
        if math.isfinite(v):
            out.append(f32_repr(v))
        else:
            out.append(json.dumps({"__float": "NaN" if math.isnan(v) else ("Infinity" if v > 0 else "-Infinity")}))
    elif isinstance(v, float):
        out.append(repr(v))
    elif isinstance(v, int):
        out.append(str(v))
    elif isinstance(v, str):
        out.append(json.dumps(v, ensure_ascii=False))
    elif isinstance(v, FileRef):
        files[v.relpath] = v.data
        out.append(json.dumps({"__file": v.relpath}))
    elif isinstance(v, ByteArr):
        out.append('{"__bytes": [' + ", ".join(str(b) for b in v) + "]}")
    elif isinstance(v, dict):
        if not v:
            out.append("{}")
            return
        out.append("{")
        first = True
        for k, x in v.items():
            out.append(("" if first else ",") + nl + json.dumps(k, ensure_ascii=False) + ": ")
            _enc(x, out, ind + " ", files)
            first = False
        out.append("\n" + ind + "}")
    elif isinstance(v, (list, tuple)):
        if not v:
            out.append("[]")
            return
        if all(isinstance(x, (int, float, str)) and not isinstance(x, (FileRef,)) for x in v):
            parts = []
            for x in v:
                o = []
                _enc(x, o, ind, files)
                parts.append("".join(o))
            out.append("[" + ", ".join(parts) + "]")
            return
        out.append("[")
        for i, x in enumerate(v):
            out.append(("" if i == 0 else ",") + nl)
            _enc(x, out, ind + " ", files)
        out.append("\n" + ind + "]")
    elif v is None:
        out.append("null")
    else:
        raise TypeError(f"cannot encode {type(v)}")


def nbt_to_json(nbt) -> tuple[str, dict]:
    """Returns (json text, {relpath: bytes} side files)."""
    out, files = [], {}
    _enc(nbt, out, "", files)
    return "".join(out) + "\n", files
