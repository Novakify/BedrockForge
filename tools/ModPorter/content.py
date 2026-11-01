"""ModPorter content layer: a Forge mod's blocks, items, recipes, loot tables, sounds, paintings and names -> Bedrock.

    from content import Content; Content(zipfile, modid, ns, rp_dir, bp_dir, report).run()

Reads only the jar's data/assets JSON + a little bytecode (block/item properties). No mod code is copied.
  blocks   blockstates + models: cube_all/cube/column/cross/slab/stairs/fence/wall/door/pane and custom element models
           (Java elements -> Bedrock block geometry with per-face material instances); facing/axis/half via block traits
  items    generated (layer0) and block items; food / tools / armour from the item class; spawn eggs come from entities
  recipes  crafting_shaped / crafting_shapeless / smelting / blasting / smoking / stonecutting
  loot     blocks/entities/chests tables (Java and Bedrock share the pool format; functions translated where they differ)
  sounds   every sounds.json event as <ns>.<event>
"""
from __future__ import annotations
import json, re
from pathlib import Path

try:
    from ModelCore.srg import srg_name          # optional: SRG -> Mojang names for bytecode reading
except Exception:                                 # pragma: no cover
    srg_name = lambda n: n


def jload(b):
    t = b.decode("utf-8-sig", "replace") if isinstance(b, bytes) else b
    t = re.sub(r"(?m)^\s*//[^\n]*", "", t); t = re.sub(r",(\s*[}\]])", r"\1", t)
    return json.loads(t)


def clean(s): return re.sub(r"[^a-z0-9_]+", "_", s.lower()).strip("_")


FACES = ("north", "south", "east", "west", "up", "down")
# Java 1.20.1 names that Bedrock spells differently (block / item ids)
RENAMES = {"minecraft:grass": "minecraft:short_grass", "minecraft:grass_path": "minecraft:grass_path",
           "minecraft:dirt_path": "minecraft:grass_path", "minecraft:magma_block": "minecraft:magma",
           "minecraft:snow_block": "minecraft:snow", "minecraft:snow": "minecraft:snow_layer",
           "minecraft:spawner": "minecraft:mob_spawner", "minecraft:note_block": "minecraft:noteblock",
           "minecraft:cobweb": "minecraft:web", "minecraft:slime_block": "minecraft:slime",
           "minecraft:nether_quartz_ore": "minecraft:quartz_ore", "minecraft:terracotta": "minecraft:hardened_clay",
           "minecraft:melon": "minecraft:melon_block", "minecraft:jack_o_lantern": "minecraft:lit_pumpkin",
           "minecraft:sugar_cane": "minecraft:reeds", "minecraft:lily_pad": "minecraft:waterlily",
           "minecraft:end_stone_bricks": "minecraft:end_bricks", "minecraft:nether_bricks": "minecraft:nether_brick",
           "minecraft:red_nether_bricks": "minecraft:red_nether_brick", "minecraft:rooted_dirt": "minecraft:dirt_with_roots",
           "minecraft:dead_bush": "minecraft:deadbush", "minecraft:spectral_arrow": "minecraft:arrow", "minecraft:enchanting_table": "minecraft:enchanting_table",
           "minecraft:turtle_helmet": "minecraft:turtle_helmet", "minecraft:redstone_torch": "minecraft:redstone_torch"}


def missing_png():
    """16x16 magenta/black checker - Minecraft's own 'missing texture' look, so the gap is visible but not an error."""
    import io, struct, zlib
    rows = b"".join(b"\x00" + b"".join((b"\xf8\x00\xf8" if ((x // 8) + (y // 8)) % 2 == 0 else b"\x00\x00\x00") for x in range(16)) for y in range(16))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")

class Content:
    def __init__(self, z, modid, ns, rp, bp, report, title=None, block_rename=None):
        self.z, self.modid, self.ns, self.rp, self.bp, self.report = z, modid, ns, Path(rp), Path(bp), report
        self.names = set(z.namelist()); self.title = title or modid
        self.terrain, self.item_tex, self.flip, self.lang = {}, {}, [], {}
        self.blocks, self.items = {}, {}
        self.rep = report.setdefault("content", {"blocks": 0, "items": 0, "recipes": 0, "loot": 0, "warnings": []})

    # ------------------------------------------------------------------ io
    def write(self, root, rel, data):
        p = Path(root) / rel; p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, (dict, list)): p.write_text(json.dumps(data, indent=1))
        elif isinstance(data, bytes): p.write_bytes(data)
        else: p.write_text(data)

    def asset(self, kind, ref, ext):
        """'the_wonderland:block/x' -> assets/the_wonderland/<kind>/block/x<ext>"""
        ns, path = (ref.split(":", 1) if ":" in ref else ("minecraft", ref))
        return f"assets/{ns}/{kind}/{path}{ext}"

    def warn(self, m): self.rep["warnings"].append(m)

    # ------------------------------------------------------------------ textures
    def block_texture(self, ref):
        """Java texture ref -> terrain_texture key (copies the png, flipbook for animated .mcmeta)."""
        if not ref or ref.startswith("#"): return None
        ns, path = (ref.split(":", 1) if ":" in ref else ("minecraft", ref))
        if ns == "minecraft":
            return "vanilla:" + path.split("/")[-1]        # resolved by vanilla terrain names
        src = f"assets/{ns}/textures/{path}.png"
        if src not in self.names: self.warn(f"texture missing {src}"); return None
        key = f"{self.ns}_{clean(path.split('/')[-1])}"
        if key in self.terrain: return key
        dst = f"textures/blocks/{self.ns}/{clean(path.split('/')[-1])}"
        self.write(self.rp, dst + ".png", self.z.read(src))
        self.terrain[key] = {"textures": dst}
        meta = src + ".mcmeta"
        if meta in self.names:
            try:
                m = jload(self.z.read(meta)).get("animation", {})
                self.flip.append({"flipbook_texture": dst, "atlas_tile": key, "ticks_per_frame": int(m.get("frametime", 1)),
                                  **({"frames": [f if isinstance(f, int) else f.get("index", 0) for f in m["frames"]]} if m.get("frames") else {}),
                                  "blend_frames": bool(m.get("interpolate", False))})
            except Exception as ex: self.warn(f"mcmeta {meta}: {ex}")
        return key

    VANILLA_TEX = {"oak_planks": "planks", "stone": "stone", "cobblestone": "cobblestone", "dirt": "dirt", "glass": "glass",
                   "bedrock": "bedrock", "obsidian": "obsidian", "sand": "sand", "gravel": "gravel"}

    def tex_key(self, ref):
        k = self.block_texture(ref)
        if k and k.startswith("vanilla:"):
            v = k.split(":", 1)[1]; return self.VANILLA_TEX.get(v, v)
        return k

    # ------------------------------------------------------------------ models
    def model(self, ref, depth=0):
        """Resolve a Java block model with its parents -> {"parent_chain", "textures", "elements"}."""
        ns, path = (ref.split(":", 1) if ":" in ref else ("minecraft", ref))
        src = f"assets/{ns}/models/{path}.json"
        if src not in self.names or depth > 8:
            return {"chain": [f"{ns}:{path}"], "textures": {}, "elements": None}
        d = jload(self.z.read(src))
        base = self.model(d["parent"], depth + 1) if d.get("parent") else {"chain": [], "textures": {}, "elements": None}
        tex = {**base["textures"], **(d.get("textures") or {})}
        return {"chain": [f"{ns}:{path}"] + base["chain"], "textures": tex, "elements": d.get("elements") or base["elements"],
                "ao": d.get("ambientocclusion", True), "render_type": d.get("render_type")}

    @staticmethod
    def resolve_tex(tex, key, seen=0):
        v = tex.get(key.lstrip("#"))
        while isinstance(v, str) and v.startswith("#") and seen < 10: v = tex.get(v[1:]); seen += 1
        return v

    def kind_of(self, m):
        for c in m["chain"]:
            n = c.split(":")[-1]
            for k in ("cross", "tinted_cross", "cube_all", "cube_column", "cube_bottom_top", "cube_top", "cube", "slab_top", "slab",
                      "inner_stairs", "outer_stairs", "stairs", "fence_post", "fence_side", "template_wall_post", "wall_post",
                      "door_", "template_glass_pane", "pressure_plate", "button", "trapdoor", "carpet", "leaves", "torch"):
                if n.split("/")[-1].startswith(k) or n.endswith("/" + k): return k
        return "custom" if m["elements"] else "cube"

    TEMPLATES = {   # vanilla parent models (not in the mod jar): from, to in Java pixels
        "slab": [([0, 0, 0], [16, 8, 16])], "slab_top": [([0, 8, 0], [16, 16, 16])],
        "stairs": [([0, 0, 0], [16, 8, 16]), ([8, 8, 0], [16, 16, 16])], "inner_stairs": [([0, 0, 0], [16, 8, 16]), ([8, 8, 0], [16, 16, 16]), ([0, 8, 8], [8, 16, 16])],
        "outer_stairs": [([0, 0, 0], [16, 8, 16]), ([8, 8, 8], [16, 16, 16])],
        "fence_post": [([6, 0, 6], [10, 16, 10])], "fence_side": [([7, 12, 0], [9, 15, 9]), ([7, 6, 0], [9, 9, 9])],
        "template_wall_post": [([4, 0, 4], [12, 16, 12])], "wall_post": [([4, 0, 4], [12, 16, 12])],
        "door_": [([0, 0, 0], [16, 16, 3])], "template_glass_pane": [([7, 0, 0], [9, 16, 16])], "carpet": [([0, 0, 0], [16, 1, 16])],
        "pressure_plate": [([1, 0, 1], [15, 1, 15])], "button": [([5, 0, 6], [11, 2, 10])], "trapdoor": [([0, 0, 0], [16, 3, 16])],
        "torch": [([7, 0, 7], [9, 10, 9])],
    }

    def template_elements(self, kind, tex):
        boxes = self.TEMPLATES.get(kind) or [([0, 0, 0], [16, 16, 16])]
        names = {"north": "side", "south": "side", "east": "side", "west": "side", "up": "top", "down": "bottom"}
        els = []
        for f, t in boxes:
            faces = {}
            for face, k in names.items():
                key = k if k in tex else next((x for x in ("texture", "all", "side", "particle", "edge", "pane", "wool", "torch") if x in tex), None)
                if key: faces[face] = {"texture": "#" + key}
            els.append({"from": f, "to": t, "faces": faces})
        return els

    def geometry_from_elements(self, gid, m):
        """Java block-model elements -> Bedrock block geometry (Java x mirrored, faces east/west swapped)."""
        cubes, mats = [], {}
        for el in m["elements"] or []:
            f, t = el["from"], el["to"]
            cube = {"origin": [round(8 - t[0], 4), round(f[1], 4), round(f[2] - 8, 4)],
                    "size": [round(t[0] - f[0], 4), round(t[1] - f[1], 4), round(t[2] - f[2], 4)], "uv": {}}
            r = el.get("rotation")
            if r and r.get("angle"):
                o = r.get("origin", [8, 8, 8]); a = float(r["angle"])
                cube["pivot"] = [round(8 - o[0], 4), o[1], o[2] - 8]
                cube["rotation"] = {"x": [-a, 0, 0], "y": [0, -a, 0], "z": [0, 0, a]}[r["axis"]]
            for face, fd in (el.get("faces") or {}).items():
                tk = fd.get("texture", "#missing").lstrip("#")
                ref = self.resolve_tex(m["textures"], tk)
                key = self.tex_key(ref) if ref else None
                if key: mats[tk] = key
                uv = fd.get("uv") or {"north": [16 - t[0], 16 - t[1], 16 - f[0], 16 - f[1]], "south": [f[0], 16 - t[1], t[0], 16 - f[1]],
                                      "east": [16 - t[2], 16 - t[1], 16 - f[2], 16 - f[1]], "west": [f[2], 16 - t[1], t[2], 16 - f[1]],
                                      "up": [f[0], f[2], t[0], t[2]], "down": [f[0], 16 - t[2], t[0], 16 - f[2]]}[face]
                bface = {"east": "west", "west": "east"}.get(face, face)
                u1, v1, u2, v2 = uv
                if face in ("up", "down"): u1, u2 = u2, u1            # x mirror flips horizontal faces' u
                rot = int(fd.get("rotation", 0)) % 360
                if rot == 180: u1, u2, v1, v2 = u2, u1, v2, v1
                cube["uv"][bface] = {"uv": [u1, v1], "uv_size": [u2 - u1, v2 - v1], "material_instance": tk}
            cubes.append(cube)
        geo = {"format_version": "1.12.0", "minecraft:geometry": [{"description": {"identifier": gid, "texture_width": 16, "texture_height": 16},
                                                                  "bones": [{"name": "root", "pivot": [0, 0, 0], "cubes": cubes}]}]}
        return geo, mats

    # ------------------------------------------------------------------ blocks
    def block_props(self):
        """Per block registry name: hardness, light, no-collision... from the mod's block classes (bytecode)."""
        return getattr(self, "_props", {})

    def blocks_run(self):
        bs_dir = f"assets/{self.modid}/blockstates/"
        for n in sorted(x for x in self.names if x.startswith(bs_dir) and x.endswith(".json")):
            bid = Path(n).stem
            if bid in getattr(self, "skip", ()): continue
            try: self.block(bid, jload(self.z.read(n)))
            except Exception as ex: self.warn(f"block {bid}: {type(ex).__name__} {ex}")
        if self.terrain:
            tt = self.rp / "textures/terrain_texture.json"
            d = json.loads(tt.read_text()) if tt.exists() else {"resource_pack_name": self.title, "texture_name": "atlas.terrain", "padding": 8,
                                                                 "num_mip_levels": 4, "texture_data": {}}
            d["texture_data"].update(self.terrain); self.write(self.rp, "textures/terrain_texture.json", d)
        if self.flip:
            fp = self.rp / "textures/flipbook_textures.json"
            d = json.loads(fp.read_text()) if fp.exists() else []
            self.write(self.rp, "textures/flipbook_textures.json", d + self.flip)

    def block(self, bid, bs):
        full = f"{self.ns}:{bid}"
        props = self.block_props().get(bid, {})
        # one representative model per variant key; facing/axis/half handled with traits
        if "variants" in bs:
            variants = {k: (v[0] if isinstance(v, list) else v) for k, v in bs["variants"].items()}
        else:   # multipart (fence/wall/pane): the unconditional part (post)
            parts = [p["apply"] for p in bs["multipart"] if "when" not in p] or [bs["multipart"][0]["apply"]]
            variants = {"": parts[0] if isinstance(parts[0], dict) else parts[0][0]}
        first = next(iter(variants.values()))
        m = self.model(first["model"]); kind = self.kind_of(m)
        hard = props.get("hardness", 1.5)
        comps = {"minecraft:destructible_by_mining": {"seconds_to_destroy": round(hard * 1.5, 2)} if hard >= 0 else False,
                 "minecraft:destructible_by_explosion": {"explosion_resistance": props.get("resistance", 3)} if hard >= 0 else False}
        if props.get("light"): comps["minecraft:light_emission"] = int(props["light"])
        desc = {"identifier": full, "menu_category": {"category": "construction"}}
        perms, states, traits = [], {}, {}
        tex = m["textures"]; t = lambda k: self.tex_key(self.resolve_tex(tex, k))
        render = "alpha_test" if kind in ("cross", "tinted_cross", "leaves", "torch") or (m.get("render_type") in ("cutout", "cutout_mipped", "translucent")) else "opaque"
        if m.get("render_type") == "translucent": render = "blend"
        if kind in ("cube_all", "leaves"):
            comps["minecraft:material_instances"] = {"*": {"texture": t("all"), "render_method": render}}
        elif kind in ("cube_column", "cube_bottom_top", "cube_top"):
            side, end = t("side"), t("end") or t("top")
            mi = {"*": {"texture": side, "render_method": render}, "up": {"texture": end or side, "render_method": render},
                  "down": {"texture": (t("bottom") or end or side), "render_method": render}}
            comps["minecraft:material_instances"] = mi
        elif kind == "cube":
            mi = {"*": {"texture": t("particle") or t("north") or t("up"), "render_method": render}}
            for f in FACES:
                k = t(f)
                if k: mi[{"east": "west", "west": "east"}.get(f, f) if False else f] = {"texture": k, "render_method": render}
            comps["minecraft:material_instances"] = mi
        elif kind in ("cross", "tinted_cross"):
            comps["minecraft:geometry"] = "minecraft:geometry.cross"
            comps["minecraft:material_instances"] = {"*": {"texture": t("cross"), "render_method": "alpha_test", "face_dimming": False,
                                                           "ambient_occlusion": False}}
            comps["minecraft:collision_box"] = False; comps["minecraft:light_dampening"] = 0
        else:   # slabs, stairs, fences, doors, custom element models: real geometry
            if not m["elements"]:
                m = dict(m); m["elements"] = self.template_elements(kind, m["textures"])
            gid = f"geometry.{self.ns}.{bid}"
            geo, mats = self.geometry_from_elements(gid, m)
            if not geo["minecraft:geometry"][0]["bones"][0]["cubes"]:
                comps["minecraft:material_instances"] = {"*": {"texture": t("particle") or t("all"), "render_method": render}}
            else:
                self.write(self.rp, f"models/blocks/{self.ns}/{bid}.geo.json", geo)
                comps["minecraft:geometry"] = gid
                mi = {"*": {"texture": next(iter(mats.values()), None) or t("particle"), "render_method": "alpha_test" if render == "opaque" else render}}
                for k, v in mats.items(): mi[k] = {"texture": v, "render_method": mi["*"]["render_method"]}
                comps["minecraft:material_instances"] = mi
                # collision/selection from the element bounds (Bedrock: inside the block)
                cs = geo["minecraft:geometry"][0]["bones"][0]["cubes"]
                lo = [max(-8, min(c["origin"][0] for c in cs)), max(0, min(c["origin"][1] for c in cs)), max(-8, min(c["origin"][2] for c in cs))]
                hi = [min(8, max(c["origin"][0] + c["size"][0] for c in cs)), min(16, max(c["origin"][1] + c["size"][1] for c in cs)),
                      min(8, max(c["origin"][2] + c["size"][2] for c in cs))]
                box = {"origin": lo, "size": [max(0.1, hi[i] - lo[i]) for i in range(3)]}
                comps["minecraft:selection_box"] = box
                comps["minecraft:collision_box"] = box if not props.get("no_collision") and box["size"][1] >= 1 else False
                comps["minecraft:light_dampening"] = 0
        if props.get("no_collision"): comps["minecraft:collision_box"] = False
        mi = comps.get("minecraft:material_instances", {})
        if any(v.get("texture") in (None, "") for v in mi.values()):
            for k, v in list(mi.items()):
                if not v.get("texture"): v["texture"] = next((x["texture"] for x in mi.values() if x.get("texture")), "missing")
        # orientation from the variant keys
        keys = [k for k in variants if k]
        if any("facing=" in k for k in keys):
            traits["minecraft:placement_direction"] = {"enabled_states": ["minecraft:cardinal_direction"]}
            for k, v in variants.items():
                fm = re.search(r"facing=(\w+)", k)
                if fm and fm.group(1) in ("north", "south", "east", "west"):
                    ry = int(v.get("y", 0))
                    perms.append({"condition": f"q.block_state('minecraft:cardinal_direction') == '{fm.group(1)}'",
                                  "components": {"minecraft:transformation": {"rotation": [int(v.get("x", 0)), -ry % 360, 0]}}})
            perms = list({p["condition"]: p for p in perms}.values())
        elif any("axis=" in k for k in keys):
            traits["minecraft:placement_position"] = {"enabled_states": ["minecraft:block_face"]}
            rot = {"x": [0, 0, 90], "z": [90, 0, 0], "y": [0, 0, 0]}
            for face, ax in (("east", "x"), ("west", "x"), ("north", "z"), ("south", "z"), ("up", "y"), ("down", "y")):
                perms.append({"condition": f"q.block_state('minecraft:block_face') == '{face}'",
                              "components": {"minecraft:transformation": {"rotation": rot[ax]}}})
        elif any(k.startswith("type=") for k in keys):   # slab top/bottom
            traits["minecraft:placement_position"] = {"enabled_states": ["minecraft:vertical_half"]}
            perms.append({"condition": "q.block_state('minecraft:vertical_half') == 'top'",
                          "components": {"minecraft:transformation": {"translation": [0, 0.5, 0]}}})
        if traits: desc["traits"] = traits
        blk = {"format_version": "1.21.60", "minecraft:block": {"description": desc, "components": comps}}
        if perms: blk["minecraft:block"]["permutations"] = perms
        self.write(self.bp, f"blocks/{self.ns}/{bid}.json", blk)
        self.blocks[bid] = full; self.rep["blocks"] += 1

    # ------------------------------------------------------------------ items
    def items_run(self):
        md = f"assets/{self.modid}/models/item/"
        for n in sorted(x for x in self.names if x.startswith(md) and x.endswith(".json")):
            iid = Path(n).stem
            if iid in self.blocks or iid in getattr(self, "skip", ()): continue                  # block items come with the block
            d = jload(self.z.read(n))
            if "spawn_egg" in (d.get("parent") or ""): continue
            txs = d.get("textures") or {}
            tex = txs.get("layer0") or (next((v for k, v in txs.items() if k != "particle" and isinstance(v, str) and not v.startswith("#")), None)
                                        if d.get("elements") else None)    # 3D Blockbench item model: its texture as the icon
            full = f"{self.ns}:{iid}"
            comps = {"minecraft:icon": f"{self.ns}_{iid}", "minecraft:max_stack_size": 64}
            info = getattr(self, "_item_info", {}).get(iid, {})
            comps.update(info.get("components", {}))
            if tex:
                src = self.asset("textures", tex, ".png")
                data = self.z.read(src) if src in self.names else missing_png()   # the jar ships no texture (pre-release mods)
                if src not in self.names: self.rep.setdefault("items_missing_texture", []).append(iid)
                self.write(self.rp, f"textures/items/{self.ns}/{iid}.png", data)
                self.item_tex[f"{self.ns}_{iid}"] = {"textures": f"textures/items/{self.ns}/{iid}"}
            elif d.get("parent", "").startswith(f"{self.modid}:block/"):
                continue
            self.write(self.bp, f"items/{self.ns}/{iid}.json", {"format_version": "1.21.60", "minecraft:item": {
                "description": {"identifier": full, "menu_category": {"category": info.get("category", "items")}}, "components": comps}})
            self.items[iid] = full; self.rep["items"] += 1
        if self.item_tex:
            it = self.rp / "textures/item_texture.json"
            d = json.loads(it.read_text()) if it.exists() else {"resource_pack_name": self.title, "texture_name": "atlas.items", "texture_data": {}}
            d["texture_data"].update(self.item_tex); self.write(self.rp, "textures/item_texture.json", d)

    # ------------------------------------------------------------------ recipes / loot
    def ref(self, x):
        if not x: return None
        x = RENAMES.get(x, x)
        return x

    TAG_ITEM = {"wool": "minecraft:white_wool", "eggs": "minecraft:egg", "planks": "minecraft:oak_planks", "logs": "minecraft:oak_log",
                "stone_crafting_materials": "minecraft:cobblestone", "coals": "minecraft:coal", "sand": "minecraft:sand", "flowers": "minecraft:poppy",
                "ingots/iron": "minecraft:iron_ingot", "ingots/gold": "minecraft:gold_ingot", "gems/diamond": "minecraft:diamond", "rods/wooden": "minecraft:stick",
                "string": "minecraft:string", "leather": "minecraft:leather", "feathers": "minecraft:feather", "slimeballs": "minecraft:slime_ball"}

    def ingredient(self, i):
        if isinstance(i, list): i = i[0]
        if "item" in i: return {"item": self.ref(i["item"])}
        if "tag" in i:
            t = i["tag"].split(":")[-1]
            return {"item": self.TAG_ITEM[t]} if t in self.TAG_ITEM else {"tag": "minecraft:" + t}
        return None

    def recipes_run(self):
        rd = f"data/{self.modid}/recipes/"
        for n in sorted(x for x in self.names if x.startswith(rd) and x.endswith(".json")):
            rid = clean(Path(n).stem)
            try: r = jload(self.z.read(n))
            except Exception: continue
            ty = r.get("type", "").split(":")[-1]
            res = r.get("result"); res = {"item": self.ref(res)} if isinstance(res, str) else ({"item": self.ref(res.get("item")), "count": res.get("count", 1)} if res else None)
            ident = {"identifier": f"{self.ns}:{rid}"}
            if ty == "crafting_shaped":
                out = {"minecraft:recipe_shaped": {"description": ident, "tags": ["crafting_table"], "pattern": r["pattern"],
                       "key": {k: self.ingredient(v) for k, v in r["key"].items()}, "result": res}}
            elif ty == "crafting_shapeless":
                out = {"minecraft:recipe_shapeless": {"description": ident, "tags": ["crafting_table"],
                       "ingredients": [self.ingredient(i) for i in r["ingredients"]], "result": res}}
            elif ty in ("smelting", "blasting", "smoking", "campfire_cooking"):
                tag = {"smelting": ["furnace"], "blasting": ["blast_furnace"], "smoking": ["smoker"], "campfire_cooking": ["campfire"]}[ty]
                out = {"minecraft:recipe_furnace": {"description": ident, "tags": tag, "input": self.ingredient(r["ingredient"])["item"]
                       if "item" in (self.ingredient(r["ingredient"]) or {}) else self.ingredient(r["ingredient"]), "output": res["item"] if res else None}}
            elif ty == "stonecutting":
                out = {"minecraft:recipe_shapeless": {"description": ident, "tags": ["stonecutter"],
                       "ingredients": [self.ingredient(r["ingredient"])], "result": {"item": self.ref(r["result"]), "count": r.get("count", 1)}}}
            else:
                self.warn(f"recipe {rid}: type {ty} not converted"); continue
            body = next(iter(out.values())); body["unlock"] = {"context": "AlwaysUnlocked"}
            self.write(self.bp, f"recipes/{self.ns}/{rid}.json", {"format_version": "1.20.10", **out}); self.rep["recipes"] += 1

    def loot_fn(self, f):
        fn = f.get("function", "").split(":")[-1]
        if fn == "set_count":
            c = f.get("count", 1)
            return {"function": "set_count", "count": {"min": c.get("min", 1), "max": c.get("max", 1)} if isinstance(c, dict) else c}
        if fn in ("explosion_decay", "set_nbt", "copy_name", "copy_nbt", "copy_state", "apply_bonus"): return None
        if fn == "set_damage": return {"function": "set_damage", "damage": f.get("damage")}
        if fn == "enchant_randomly": return {"function": "enchant_randomly"}
        if fn == "looting_enchant": return {"function": "looting_enchant", "count": f.get("count")}
        return None

    def loot_run(self):
        ld = f"data/{self.modid}/loot_tables/"
        for n in sorted(x for x in self.names if x.startswith(ld) and x.endswith(".json")):
            rel = n[len(ld):]
            try: t = jload(self.z.read(n))
            except Exception: continue
            pools = []
            for p in t.get("pools", []):
                entries = []
                for e in p.get("entries", []):
                    ty = e.get("type", "").split(":")[-1]
                    if ty == "item":
                        name = self.ref(e["name"])
                        entries.append({"type": "item", "name": name, "weight": e.get("weight", 1),
                                        "functions": [x for x in (self.loot_fn(f) for f in e.get("functions", [])) if x]})
                    elif ty == "empty": entries.append({"type": "empty", "weight": e.get("weight", 1)})
                    elif ty == "loot_table": entries.append({"type": "loot_table", "name": f"loot_tables/{self.ns}/{e['name'].split(':')[-1]}.json", "weight": e.get("weight", 1)})
                r = p.get("rolls", 1)
                if isinstance(r, dict): r = {"min": r.get("min", 1), "max": r.get("max", 1)} if "min" in r else r.get("value", 1)
                if entries: pools.append({"rolls": r, "entries": entries})
            self.write(self.bp, f"loot_tables/{self.ns}/{rel}", {"pools": pools}); self.rep["loot"] += 1
            if rel.startswith("blocks/"):
                bid = Path(rel).stem; bf = self.bp / f"blocks/{self.ns}/{bid}.json"
                if bf.exists():
                    d = json.loads(bf.read_text()); d["minecraft:block"]["components"]["minecraft:loot"] = f"loot_tables/{self.ns}/{rel}"
                    bf.write_text(json.dumps(d, indent=1))

    # ------------------------------------------------------------------ names
    def lang_run(self):
        p = f"assets/{self.modid}/lang/en_us.json"
        if p not in self.names: return {}
        src = jload(self.z.read(p)); out = {}
        for k, v in src.items():
            parts = k.split(".")
            if len(parts) < 3 or parts[1] != self.modid: continue
            kind, name = parts[0], ".".join(parts[2:])
            if kind == "block": out[f"tile.{self.ns}:{name}.name"] = v
            elif kind == "item": out[f"item.{self.ns}:{name}"] = v
            elif kind == "entity": out[f"entity.{self.ns}:{name}.name"] = v; out[f"item.spawn_egg.entity.{self.ns}:{name}.name"] = f"Spawn {v}"
        return out

    # ------------------------------------------------------------------ vanilla resource overrides bundled in the mod
    TEXMAP = Path("/home/claude/tools/packconv/textures.json")   # GeyserMC PackConverter mapping (MIT), fetched locally

    VANILLA_DIR = Path("/home/claude/tools/vanilla")   # lists read from the installed game (terrain atlas paths, sound files)

    def vanilla_overrides(self):
        """assets/minecraft/** in the mod jar -> Bedrock vanilla paths, conservatively.

        Only block / item textures (GeyserMC PackConverter mapping) whose Bedrock target is a known atlas texture,
        not animated (.mcmeta strips have other frame layouts), sound files that exist in Bedrock under the same path,
        and splashes. Entity / GUI / environment / colormap / panorama replacements are skipped: their Bedrock layouts
        differ, and shipping the whole Java set crashed the game while loading (Wonderland, 2026-10-31)."""
        names = [n for n in self.names if n.startswith("assets/minecraft/") and not n.endswith("/")]
        if not names: return
        mp = json.loads(self.TEXMAP.read_text()) if self.TEXMAP.exists() else {}
        vd = self.VANILLA_DIR
        terrain = set(json.loads((vd / "terrain_paths.json").read_text())) if (vd / "terrain_paths.json").exists() else set()
        vsnd = {k[1:] for k in json.loads((vd / "vanilla_tex.json").read_text()) if k.startswith("@")} if (vd / "vanilla_tex.json").exists() else set()
        dims = json.loads((vd / "bedrock_samples_png_dims.json").read_text()) if (vd / "bedrock_samples_png_dims.json").exists() else {}
        n_tex = n_snd = 0; skipped = {}
        nameset = set(names)
        for n in names:
            rel = n[len("assets/minecraft/"):]
            if rel.startswith("textures/") and rel.endswith(".png"):
                key = rel[len("textures/"):-4]; cat, _, name = key.partition("/")
                if n + ".mcmeta" in nameset: skipped[cat] = skipped.get(cat, 0) + 1; continue
                if cat == "models" and name.startswith("armor/"):   # Java <mat>_layer_<n> -> Bedrock <mat>_<n>
                    m_ = re.match(r"armor/(\w+?)_layer_(\d)$", name)
                    if m_: key = f"models/armor/{ {'chainmail': 'chain', 'leather': 'cloth'}.get(m_.group(1), m_.group(1)) }_{m_.group(2)}"
                if cat in ("entity", "environment", "colormap", "models", "gui"):
                    # only where Bedrock has the same file at the same size (bedrock-samples): same layout, safe to swap
                    v = mp.get(cat, {}).get(name) if isinstance(mp.get(cat), dict) else mp.get(key)
                    cands = [key] + ([x if "/" in x else f"{cat}/{x}" for x in (v if isinstance(v, list) else [v])] if v else [])
                    tgt = next((c for c in cands if c in dims), None)
                    import struct as _st
                    raw = self.z.read(n)
                    if tgt and list(_st.unpack(">II", raw[16:24])) == dims[tgt]:
                        self.write(self.rp, f"textures/{tgt}.png", raw); n_tex += 1
                    else: skipped[cat] = skipped.get(cat, 0) + 1
                    continue
                if cat not in ("block", "item"): skipped[cat] = skipped.get(cat, 0) + 1; continue
                if key in mp: v = mp[key]; targets = v if isinstance(v, list) else [v]
                elif isinstance(mp.get(cat), dict) and name in mp[cat]:
                    v = mp[cat][name]; targets = [f"{'blocks' if cat == 'block' else 'items'}/{x}" for x in (v if isinstance(v, list) else [v])]
                else:   # newer textures the mapping doesn't know: same name + same size in bedrock-samples
                    t_ = f"{'blocks' if cat == 'block' else 'items'}/{name}"
                    raw_ = self.z.read(n)
                    import struct as _st
                    if t_ in dims and list(_st.unpack(">II", raw_[16:24])) == dims[t_]:
                        self.write(self.rp, f"textures/{t_}.png", raw_); n_tex += 1
                    else: skipped[cat] = skipped.get(cat, 0) + 1
                    continue
                for t in targets:
                    if not t.startswith(("blocks/", "items/")): continue
                    if t.startswith("blocks/") and terrain and f"textures/{t}" not in terrain: continue
                    self.write(self.rp, f"textures/{t}.png", self.z.read(n)); n_tex += 1
            elif rel.startswith("sounds/") and rel.endswith(".ogg"):
                if vsnd and rel[:-4] not in vsnd: skipped["sounds"] = skipped.get("sounds", 0) + 1; continue
                self.write(self.rp, rel, self.z.read(n)); n_snd += 1
            elif rel == "texts/splashes.txt":
                lines = [l for l in self.z.read(n).decode("utf-8", "replace").splitlines() if l.strip()]
                self.write(self.rp, "splashes.json", {"splashes": lines})
        self.rep["vanilla_overrides"] = {"textures": n_tex, "sounds": n_snd, "skipped": skipped}

    def run(self, item_info=None, block_props=None):
        self._item_info = item_info or {}; self._props = block_props or {}
        self.blocks_run(); self.items_run(); self.recipes_run(); self.loot_run(); self.vanilla_overrides()
        return self.lang_run()
