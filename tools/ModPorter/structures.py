"""Java structure templates (.nbt, gzip, big-endian) -> Bedrock .mcstructure (little-endian NBT).

    from structures import Converter; Converter(mod_ns, bedrock_ns, custom_blocks).convert(nbt_bytes) -> bytes

Block states: vanilla through minecraft-data's Java->Bedrock table (blocksJ2B), mod blocks to the custom blocks
ModPorter's content layer made (facing -> minecraft:cardinal_direction, axis -> minecraft:block_face,
slab type -> minecraft:vertical_half). Waterlogged blocks get water in layer 1. Chests keep their loot table;
signs keep their text. Air in the template is written as structure_void-free air (Java templates only store what
they place, so unlisted positions become structure_void and leave the world untouched, like Java).
"""
from __future__ import annotations
import gzip, io, json, re
from pathlib import Path

import nbtlib
from nbtlib import Compound, List, Int, String, Byte, Short

MCDATA = Path("/home/claude/tools/mcdata/package/minecraft-data/data/bedrock")
BLOCK_VERSION = 18168865   # 1.21.x block format version; Bedrock upgrades older states itself
RENAME = {"minecraft:grass": "minecraft:short_grass"}


def parse_state(s):
    m = re.match(r"([^\[]+)(?:\[(.*)\])?$", s)
    name, props = m.group(1), m.group(2) or ""
    return name, dict(p.split("=", 1) for p in props.split(",") if p)


class Converter:
    def __init__(self, mod_ns, bns, custom_blocks, j2b_version="1.26.30"):
        self.mod_ns, self.bns, self.custom = mod_ns, bns, custom_blocks   # custom: regname -> bedrock id
        raw = json.loads((MCDATA / j2b_version / "blocksJ2B.json").read_text())
        self.j2b, self.by_name = {}, {}
        for k, v in raw.items():
            n, p = parse_state(k)
            self.j2b[(n, tuple(sorted(p.items())))] = v
            self.by_name.setdefault(n, v)
        self.stats = {"unmapped": {}}

    def bedrock_state(self, name, props):
        key0 = (name, tuple(sorted(props.items())))
        memo = self.__dict__.setdefault("_memo", {})
        if key0 not in memo: memo[key0] = self._bedrock_state(name, props)
        return memo[key0]

    def _bedrock_state(self, name, props):
        name = RENAME.get(name, name)
        ns = name.split(":")[0]
        if ns == self.mod_ns:
            reg = name.split(":")[1]
            bid = self.custom.get(reg)
            if not bid: self.stats["unmapped"][name] = self.stats["unmapped"].get(name, 0) + 1; return "minecraft:air", {}
            st = {}
            f = props.get("facing")
            if f in ("north", "south", "east", "west"): st["minecraft:cardinal_direction"] = String(f)
            a = props.get("axis")
            if a: st["minecraft:block_face"] = String({"x": "east", "y": "up", "z": "south"}[a])
            t = props.get("type")
            if t in ("top", "bottom"): st["minecraft:vertical_half"] = String(t)
            return bid, st
        key = (name, tuple(sorted(props.items())))
        v = self.j2b.get(key)
        if v is None:   # property set differs (1.20.1 vs table version): drop unknown props, then name only
            if not hasattr(self, "_byn"):
                self._byn = {}
                for k, vv in self.j2b.items(): self._byn.setdefault(k[0], []).append((k, vv))
            cands = self._byn.get(name, [])
            best = max(cands, key=lambda kv: len(set(kv[0][1]) & set(props.items())), default=None)
            v = best[1] if best else None
        if v is None: self.stats["unmapped"][name] = self.stats["unmapped"].get(name, 0) + 1; return "minecraft:air", {}
        bn, bp = parse_state(v)
        st = {}
        for k, x in bp.items():
            if x in ("true", "false"): st[k] = Byte(1 if x == "true" else 0)
            elif re.fullmatch(r"-?\d+", x): st[k] = Int(int(x))
            else: st[k] = String(x)
        return bn, st

    def convert(self, data):
        nbt = nbtlib.File.parse(io.BytesIO(gzip.decompress(data) if data[:2] == b"\x1f\x8b" else data))
        sx, sy, sz = (int(v) for v in nbt["size"])
        palette, water = [], []
        for p in nbt["palette"]:
            props = {k: str(v) for k, v in (p.get("Properties") or {}).items()}
            water.append(props.get("waterlogged") == "true")
            if str(p["Name"]) in ("minecraft:structure_void", "minecraft:jigsaw", "minecraft:structure_block"):
                palette.append(None); continue
            n, st = self.bedrock_state(str(p["Name"]), props)
            palette.append(Compound({"name": String(n), "states": Compound(st), "version": Int(BLOCK_VERSION)}))
        # bedrock palette: only the placed states. Positions the template doesn't place are block index -1 - the
        # .mcstructure "nothing here" marker. (A minecraft:structure_void palette entry is a real block: it was
        # placed as invisible structure_void blocks everywhere, which the mod's spawn checks read as solid ground.)
        bpal = []
        idx_of = {}
        for i, c in enumerate(palette):
            if c is None: idx_of[i] = -1; continue
            bpal.append(c); idx_of[i] = len(bpal) - 1
        water_idx = None
        if any(water):
            bpal.append(Compound({"name": String("minecraft:water"), "states": Compound({"liquid_depth": Int(0)}), "version": Int(BLOCK_VERSION)}))
            water_idx = len(bpal) - 1
        n = sx * sy * sz
        layer0, layer1 = [-1] * n, [-1] * n
        pos_data = {}
        for b in nbt["blocks"]:
            x, y, z = (int(v) for v in b["pos"]); s = int(b["state"])
            i = (x * sy + y) * sz + z
            layer0[i] = idx_of[s]
            if water[s] and water_idx is not None: layer1[i] = water_idx
            be = b.get("nbt")
            if be is not None:
                d = self.block_entity(be, bpal[idx_of[s]]["name"] if idx_of[s] >= 0 else "", (x, y, z))
                if d is not None: pos_data[str(i)] = Compound({"block_entity_data": d})
        out = nbtlib.File({
            "format_version": Int(1), "size": List[Int]([Int(sx), Int(sy), Int(sz)]),
            "structure_world_origin": List[Int]([Int(0), Int(0), Int(0)]),
            "structure": Compound({
                "block_indices": List[List[Int]]([List[Int]([Int(v) for v in layer0]), List[Int]([Int(v) for v in layer1])]),
                "entities": List[Compound]([]),
                "palette": Compound({"default": Compound({"block_palette": List[Compound](bpal), "block_position_data": Compound(pos_data)})}),
            }),
        })
        buf = io.BytesIO(); out.write(buf, byteorder="little"); return buf.getvalue()

    def block_entity(self, be, bname, pos):
        x, y, z = pos
        base = {"x": Int(x), "y": Int(y), "z": Int(z), "isMovable": Byte(1)}
        if "LootTable" in be:
            lt = str(be["LootTable"]); ns, path = lt.split(":", 1) if ":" in lt else ("minecraft", lt)
            ref = f"loot_tables/{self.bns}/{path}.json" if ns == self.mod_ns else f"loot_tables/{path}.json"
            return Compound({**base, "id": String("Chest" if "chest" in bname or "barrel" not in bname else "Barrel"), "LootTable": String(ref),
                             "Items": List[Compound]([])})
        if "front_text" in be or "Text1" in be:
            def plain(j):
                try:
                    d = json.loads(str(j)); return d if isinstance(d, str) else d.get("text", "") + "".join(x.get("text", "") for x in d.get("extra", []) if isinstance(x, dict))
                except Exception: return str(j).strip('"')
            msgs = [plain(m) for m in be["front_text"]["messages"]] if "front_text" in be else [plain(be.get(f"Text{i}", "")) for i in range(1, 5)]
            back = [plain(m) for m in be["back_text"]["messages"]] if "back_text" in be else ["", "", "", ""]
            txt = lambda ms: Compound({"Text": String("\n".join(ms).rstrip("\n")), "TextOwner": String(""), "SignTextColor": Int(-16777216),
                                       "IgnoreLighting": Byte(0), "HideGlowOutline": Byte(0), "PersistFormatting": Byte(1)})
            return Compound({**base, "id": String("Sign"), "FrontText": txt(msgs), "BackText": txt(back), "IsWaxed": Byte(0)})
        if "Items" in be:
            items = []
            for it in be["Items"]:
                iid = RENAME.get(str(it.get("id", "")), str(it.get("id", "")))
                if iid.split(":")[0] == self.mod_ns: iid = f"{self.bns}:{iid.split(':')[1]}"
                items.append(Compound({"Name": String(iid), "Count": Byte(int(it.get("Count", 1))), "Damage": Short(0), "Slot": Byte(int(it.get("Slot", 0)))}))
            return Compound({**base, "id": String("Chest"), "Items": List[Compound](items)})
        return None
