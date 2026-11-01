"""GeckoLib armor (GeoArmorRenderer) -> Bedrock attachables.

GeckoLib armor geometry follows a fixed template: bipedHead/bipedBody/bipedRightArm/bipedLeftArm/bipedRightLeg/
bipedLeftLeg anchor bones, with the visible parts under armorHead, armorBody, armorRightArm, armorLeftArm,
armorRightLeg, armorLeftLeg, armorRightBoot, armorLeftBoot (GeoArmorRenderer shows the ones for the worn slot).
Bedrock attachables bind bones to the wearer by name, so the anchors are renamed to the player skeleton
(head, body, rightArm, leftArm, rightLeg, leftLeg) and one geometry per slot keeps only that slot's parts.
Pivots already sit where the vanilla model parts are, so nothing moves.

Items: content.py already made the item (icon, name); here it gains minecraft:wearable + durability + enchantable.
"""
import copy
import json
import re

ANCHOR = {"bipedhead": "head", "bipedbody": "body", "bipedrightarm": "rightArm", "bipedleftarm": "leftArm",
          "bipedrightleg": "rightLeg", "bipedleftleg": "leftLeg"}
SLOT_PARTS = {"head": ["armorhead"], "chest": ["armorbody", "armorrightarm", "armorleftarm"],
              "legs": ["armorrightleg", "armorleftleg"], "feet": ["armorrightboot", "armorleftboot"]}
SLOT_OF_SUFFIX = {"helmet": "head", "hat": "head", "hood": "head", "mask": "head", "crown": "head", "cap": "head",
                  "chestplate": "chest", "chest": "chest", "tunic": "chest", "robe": "chest", "jacket": "chest",
                  "leggings": "legs", "pants": "legs", "legs": "legs",
                  "boots": "feet", "shoes": "feet", "feet": "feet"}
BEDROCK = {"head": ("slot.armor.head", "helmet_layer_visible", 3, 363), "chest": ("slot.armor.chest", "chest_layer_visible", 8, 528),
           "legs": ("slot.armor.legs", "leg_layer_visible", 6, 495), "feet": ("slot.armor.feet", "boot_layer_visible", 3, 429)}
ENCHANT_SLOT = {"head": "armor_head", "chest": "armor_torso", "legs": "armor_legs", "feet": "armor_feet"}


def is_armor_geo(geo):
    names = {b["name"].lower() for g in geo.get("minecraft:geometry", []) for b in g.get("bones", [])}
    return bool(names & {"armorhead", "armorbody", "armorrightleg", "armorrightboot"})


def slot_geometry(geo, slot, identifier):
    """Keep the anchors + the slot's armour parts (+ any extra bones hung under them), anchors renamed."""
    g = copy.deepcopy(geo["minecraft:geometry"][0])
    bones = g.get("bones", [])
    by = {b["name"]: b for b in bones}
    keep_roots = {b["name"] for b in bones if b["name"].lower() in SLOT_PARTS[slot]}
    # extra bones the mod hangs straight off an anchor next to the slot parts (capes, leg cloth): keep with legs/chest
    for b in bones:
        p = (b.get("parent") or "").lower()
        n = b["name"].lower()
        if p in ANCHOR and b["name"] not in keep_roots and n not in sum(SLOT_PARTS.values(), []):
            want = "legs" if "leg" in n else "chest" if ("body" in n or "cape" in n) else "head" if "head" in n else None
            if want == slot: keep_roots.add(b["name"])
    # bones only drawn when Epic Fight is installed (duplicate capes/skirts): GeckoLib renderers hide them otherwise
    skip = {b["name"] for b in bones if "epicfight" in b["name"].lower()}
    def under(name):
        n0 = name
        while name:
            if name in skip: return False
            name = by.get(name, {}).get("parent")
        name = n0
        while name:
            if name in keep_roots: return True
            name = by.get(name, {}).get("parent")
        return False
    out = []
    for b in bones:
        low = b["name"].lower()
        if low in ANCHOR:
            nb = {k: v for k, v in b.items() if k not in ("cubes",)}; nb["name"] = ANCHOR[low]; out.append(nb)
        elif under(b["name"]):
            nb = dict(b)
            if (nb.get("parent") or "").lower() in ANCHOR: nb["parent"] = ANCHOR[nb["parent"].lower()]
            out.append(nb)
    g["bones"] = out
    g["description"] = {**g.get("description", {}), "identifier": identifier}
    return {"format_version": "1.12.0", "minecraft:geometry": [g]}


def slot_of(item_id):
    last = item_id.rsplit("_", 1)[-1]
    return SLOT_OF_SUFFIX.get(last)


def set_name(item_id):
    return item_id.rsplit("_", 1)[0]


def convert(b):
    """b: the GeckoBridge Build. Adds attachables for armour items that have a GeckoLib armour geo."""
    geos = {}
    for n in b.names:
        if n.startswith(f"assets/{b.modid}/geo/") and n.endswith(".geo.json"):
            try: d = json.loads(b.z.read(n).decode("utf-8-sig"))
            except Exception: continue
            if is_armor_geo(d): geos[n.rsplit("/", 1)[1][:-9]] = (n, d)
    if not geos: return 0
    items_dir = b.bp / "items" / b.ns
    done = 0
    for f in sorted(items_dir.glob("*.json")) if items_dir.exists() else []:
        iid = f.stem; slot = slot_of(iid)
        if not slot: continue
        st = set_name(iid)
        key = next((k for k in (f"{st}_armor", st, f"{st}_armour") if k in geos), None)
        if not key: continue
        path, geo = geos[key]
        tex_src = next((t for t in (f"assets/{b.modid}/textures/armor/{key}.png", f"assets/{b.modid}/textures/armor/{st}.png",
                                    f"assets/{b.modid}/textures/models/armor/{key}.png") if t in b.names), None)
        if not tex_src: b.report["warnings"].append(f"armor {iid}: no texture for {key}"); continue
        tex = f"textures/models/armor/{b.ns}/{key}"
        b.write(b.rp, tex + ".png", b.z.read(tex_src))
        gid = f"geometry.gb.{b.ns}.armor.{key}.{slot}"
        b.write(b.rp, f"models/entity/{b.ns}/armor/{key}.{slot}.geo.json", slot_geometry(geo, slot, gid))
        bslot, layer, prot, dur = BEDROCK[slot]
        desc = {"identifier": f"{b.ns}:{iid}", "materials": {"default": "armor", "enchanted": "armor_enchanted"},
                "textures": {"default": tex, "enchanted": "textures/misc/enchanted_item_glint"},
                "geometry": {"default": gid}, "scripts": {"parent_setup": f"variable.{layer} = 0.0;"},
                "render_controllers": ["controller.render.armor"]}
        # animated GeoArmor (capes, wings): its looping animations play while worn (baked like mob animations)
        anim_src = next((a for a in (f"assets/{b.modid}/animations/{key}.animation.json", f"assets/{b.modid}/animations/armor/{key}.animation.json") if a in b.names), None)
        if anim_src:
            amap = b.animations(anim_src)
            loops = [nid for nm, nid in amap.items() if "idle" in nm.lower() or "loop" in nm.lower()] or list(amap.values())[:1]
            if loops:
                desc["animations"] = {f"a{i}": nid for i, nid in enumerate(loops)}
                desc["scripts"]["animate"] = list(desc["animations"])
        b.write(b.rp, f"attachables/{b.ns}/{iid}.json", {"format_version": "1.10.0", "minecraft:attachable": {"description": desc}})
        item = json.loads(f.read_text())
        comps = item["minecraft:item"]["components"]
        comps.setdefault("minecraft:wearable", {"slot": bslot, "protection": prot})
        comps.setdefault("minecraft:durability", {"max_durability": dur})
        comps.setdefault("minecraft:enchantable", {"slot": ENCHANT_SLOT[slot], "value": 15})
        comps["minecraft:max_stack_size"] = 1
        item["minecraft:item"]["description"]["menu_category"] = {"category": "equipment", "group": f"minecraft:itemGroup.name.{ {'head': 'helmet', 'chest': 'chestplate', 'legs': 'leggings', 'feet': 'boots'}[slot] }"}
        b.write(b.bp, f"items/{b.ns}/{iid}.json", item)
        done += 1
    b.report["armor_attachables"] = done
    return done
