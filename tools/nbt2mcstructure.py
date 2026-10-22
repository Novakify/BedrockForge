#!/usr/bin/env python3
"""Convert Java structure-block .nbt files to Bedrock .mcstructure.

Usage: python3 tools/nbt2mcstructure.py <src_structure_dir> <dst_dir>

Self-contained (no third-party NBT library): reads gzip'd big-endian Java NBT,
writes little-endian uncompressed Bedrock NBT. Blocks are mapped through
JAVA_TO_BEDROCK below; unknown blocks fall back to air with a warning.
Entities (markers) are dropped. Only the templates used by the dynamic
structure placement are converted (base/development structures are skipped).
"""
import gzip
import io
import os
import struct
import sys

# ---------------------------------------------------------------- NBT read (Java, big endian)
TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG, TAG_FLOAT, TAG_DOUBLE, TAG_BYTE_ARRAY, TAG_STRING, TAG_LIST, TAG_COMPOUND, TAG_INT_ARRAY, TAG_LONG_ARRAY = range(13)


def _read(f, fmt):
    size = struct.calcsize(fmt)
    return struct.unpack(fmt, f.read(size))[0]


def read_payload(f, t):
    if t == TAG_BYTE: return _read(f, ">b")
    if t == TAG_SHORT: return _read(f, ">h")
    if t == TAG_INT: return _read(f, ">i")
    if t == TAG_LONG: return _read(f, ">q")
    if t == TAG_FLOAT: return _read(f, ">f")
    if t == TAG_DOUBLE: return _read(f, ">d")
    if t == TAG_BYTE_ARRAY:
        n = _read(f, ">i"); return list(f.read(n))
    if t == TAG_STRING:
        n = _read(f, ">H"); return f.read(n).decode("utf-8")
    if t == TAG_LIST:
        et = _read(f, ">b"); n = _read(f, ">i")
        return [read_payload(f, et) for _ in range(n)]
    if t == TAG_COMPOUND:
        out = {}
        while True:
            ct = _read(f, ">b")
            if ct == TAG_END: return out
            name = read_payload(f, TAG_STRING)
            out[name] = read_payload(f, ct)
    if t == TAG_INT_ARRAY:
        n = _read(f, ">i"); return [_read(f, ">i") for _ in range(n)]
    if t == TAG_LONG_ARRAY:
        n = _read(f, ">i"); return [_read(f, ">q") for _ in range(n)]
    raise ValueError(f"bad tag {t}")


def read_java_nbt(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    f = io.BytesIO(raw)
    t = _read(f, ">b"); read_payload(f, TAG_STRING)
    return read_payload(f, t)


# ---------------------------------------------------------------- NBT write (Bedrock, little endian)
class Tag:
    def __init__(self, t, v): self.t, self.v = t, v
def B(v): return Tag(TAG_BYTE, v)
def I(v): return Tag(TAG_INT, v)
def S(v): return Tag(TAG_STRING, v)
def C(d): return Tag(TAG_COMPOUND, d)
def L(et, items): return Tag(TAG_LIST, (et, items))


def write_payload(f, tag):
    t, v = tag.t, tag.v
    if t == TAG_BYTE: f.write(struct.pack("<b", v))
    elif t == TAG_INT: f.write(struct.pack("<i", v))
    elif t == TAG_STRING:
        b = v.encode("utf-8"); f.write(struct.pack("<H", len(b))); f.write(b)
    elif t == TAG_LIST:
        et, items = v
        f.write(struct.pack("<b", et)); f.write(struct.pack("<i", len(items)))
        for it in items: write_payload(f, it)
    elif t == TAG_COMPOUND:
        for k, sub in v.items():
            f.write(struct.pack("<b", sub.t)); write_payload(f, S(k)); write_payload(f, sub)
        f.write(b"\x00")
    else: raise ValueError(t)


def write_bedrock_nbt(path, root):
    f = io.BytesIO()
    f.write(struct.pack("<b", TAG_COMPOUND)); write_payload(f, S("")); write_payload(f, root)
    open(path, "wb").write(f.getvalue())


# ---------------------------------------------------------------- block mapping
TORCH_FACING = {"north": "north", "south": "south", "east": "east", "west": "west"}


def map_block(name, props):
    """Return (bedrock_name, states dict of Tag)."""
    n = name.replace("minecraft:", "")
    if n in ("air", "cave_air", "void_air", "structure_void"):
        return "minecraft:air", {}
    if n in ("redstone_wall_torch", "wall_torch"):
        # Java facing = direction the torch points AWAY from the wall; Bedrock torch_facing_direction uses the same meaning.
        base = "minecraft:redstone_torch" if n.startswith("redstone") else "minecraft:torch"
        return base, {"torch_facing_direction": S(TORCH_FACING.get(props.get("facing", "north"), "north"))}
    if n in ("redstone_torch", "torch"):
        return "minecraft:" + n, {"torch_facing_direction": S("top")}
    if n.endswith("_log") or n.endswith("_wood"):
        return "minecraft:" + n, {"pillar_axis": S(props.get("axis", "y"))}
    simple = {"mossy_cobblestone", "sand", "glowstone", "cobblestone", "stone", "dirt", "grass_block", "sandstone"}
    if n in simple:
        return "minecraft:" + n, {}
    print(f"  WARN unmapped block {name} -> air", file=sys.stderr)
    return "minecraft:air", {}


def convert(src, dst):
    nbt = read_java_nbt(src)
    sx, sy, sz = nbt["size"]
    palette_java = nbt["palette"] if "palette" in nbt else nbt["palettes"][0]
    pal = []
    for e in palette_java:
        bn, st = map_block(e["Name"], e.get("Properties", {}))
        pal.append((bn, st))
    # dedupe bedrock palette
    bpal, index_of = [], {}
    def key(bn, st): return bn + "|" + ",".join(f"{k}={v.v}" for k, v in sorted(st.items()))
    jmap = []
    for bn, st in pal + [("minecraft:air", {})]:
        k = key(bn, st)
        if k not in index_of:
            index_of[k] = len(bpal); bpal.append((bn, st))
        jmap.append(index_of[k])
    air_idx = jmap[-1]
    vol = sx * sy * sz
    layer0 = [air_idx] * vol
    layer1 = [-1] * vol
    for b in nbt["blocks"]:
        x, y, z = b["pos"]
        layer0[(x * sy + y) * sz + z] = jmap[b["state"]]
    block_palette = [C({"name": S(bn), "states": C(st), "version": I(18168865)}) for bn, st in bpal]
    root = C({
        "format_version": I(1),
        "size": L(TAG_INT, [I(sx), I(sy), I(sz)]),
        "structure_world_origin": L(TAG_INT, [I(0), I(0), I(0)]),
        "structure": C({
            "block_indices": L(TAG_LIST, [L(TAG_INT, [I(v) for v in layer0]), L(TAG_INT, [I(v) for v in layer1])]),
            "entities": L(TAG_COMPOUND, []),
            "palette": C({"default": C({"block_palette": L(TAG_COMPOUND, block_palette), "block_position_data": C({})})}),
        }),
    })
    write_bedrock_nbt(dst, root)
    print(f"{os.path.relpath(src)} -> {os.path.basename(dst)}  size {sx}x{sy}x{sz}, {len(nbt['blocks'])} blocks, palette {len(bpal)}")


TEMPLATES = {
    "mossy_pyramid/pyramid1.nbt": "mossy_pyramid_1",
    "mossy_pyramid/pyramid2.nbt": "mossy_pyramid_2",
    "sand_pyramid/pyramid1.nbt": "sand_pyramid_1",
    "sand_pyramid/pyramid2.nbt": "sand_pyramid_2",
    "wooden_cross/wooden_cross.nbt": "wooden_cross",
    **{f"letter/letter_{c}.nbt": f"letter_{c}" for c in "herobin"},
}

if __name__ == "__main__":
    src_dir, dst_dir = sys.argv[1], sys.argv[2]
    os.makedirs(dst_dir, exist_ok=True)
    for rel, out in TEMPLATES.items():
        convert(os.path.join(src_dir, rel), os.path.join(dst_dir, out + ".mcstructure"))
