"""Block / item properties from a Forge mod's bytecode (javap), named through ModelCore.srg.

registries(tmp, classes)  -> {"block": {regname: class}, "item": {regname: class}}
block_props(tmp, cls)     -> {"hardness", "resistance", "light", "no_collision", "no_occlusion", "sound"}
item_info(tmp, cls)       -> {"components": {...Bedrock item components}, "category"}
"""
from __future__ import annotations
import re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "GeckoBridge")); sys.path.insert(0, str(HERE.parent))
import controllers as C                          # noqa: E402  (javap + method parsing helpers)
from ModelCore.srg import srg_name               # noqa: E402


def calls(code):
    """[(mojang name, [preceding numeric consts])] for every invoke in a method."""
    out, consts = [], []
    for off, op, rest in code:
        if op.startswith(("ldc", "iconst", "bipush", "sipush", "fconst", "dconst")):
            v = C.const_of(op, rest)
            if isinstance(v, (int, float)) and not isinstance(v, bool): consts.append(v)
        if op.startswith("invoke"):
            own, nm, desc = C.member(rest)
            out.append((srg_name(nm), list(consts), own, desc)); consts = []
        if op in ("new",): consts = []
    return out


def registries(tmp, classes):
    """MCreator/DeferredRegister: REGISTRY.register("name", () -> new XBlock()) in <clinit>. Each name is the ldc right
    before an invokedynamic; that call site's bootstrap method (javap -v BootstrapMethods) names the lambda that
    builds the object. (Pairing the n-th string with lambda$static$n was off by one - the modid "the_wonderland" is
    an ldc too - so every block / item / effect got its neighbour's Java class: teleporters ran other blocks' code.)"""
    import subprocess
    reg = {"block": {}, "item": {}, "entity": {}, "effect": {}, "sound": {}}
    for cls in classes:
        low = cls.split(".")[-1].lower()
        kind = "block" if low.endswith("blocks") else "item" if low.endswith("items") else "effect" if low.endswith("mobeffects") else None
        if not kind: continue
        ms = C.parse_methods(C.javap(tmp, cls))
        verbose = subprocess.run(["javap", "-v", "-p", "-cp", str(tmp), cls], capture_output=True, text=True).stdout
        boot, cur = {}, None                      # bootstrap index -> lambda method name
        if "BootstrapMethods:" in verbose:
            for line in verbose.split("BootstrapMethods:", 1)[1].splitlines():
                m = re.match(r"^\s+(\d+): #\d+ REF_invokeStatic", line)
                if m: cur = int(m.group(1)); continue
                m = re.search(r"REF_invoke\w+ [\w/$]+\.(lambda\$[\w$]+):", line)
                if m and cur is not None and cur not in boot: boot[cur] = m.group(1)
        code = ms.get("<clinit>", [])
        pending = None
        for off, op, rest in code:
            if op.startswith("ldc"):
                v = C.const_of(op, rest); pending = v if isinstance(v, str) and re.fullmatch(r"[a-z0-9_]+", v) else None
                continue
            if op == "invokedynamic" and pending:
                m = re.search(r"InvokeDynamic #(\d+):", rest)
                lam = boot.get(int(m.group(1))) if m else None
                body = ms.get(lam) if lam else None
                new = next((C.comment(r).split("class ")[-1].replace("/", ".") for o, op2, r in body if op2 == "new"), None) if body else None
                if new: reg[kind][pending] = new
            pending = None
    return reg


def block_props(tmp, cls):
    p = {}
    try: ms = C.parse_methods(C.javap(tmp, cls))
    except Exception: return p
    for mname, code in ms.items():
        if mname not in ("<init>",) and not mname.startswith("lambda$"): continue
        for name, consts, own, desc in calls(code):
            if name == "strength" and consts:
                p["hardness"] = float(consts[0]); p["resistance"] = float(consts[1] if len(consts) > 1 else consts[0])
            elif name in ("instabreak",): p["hardness"] = 0
            elif name == "noCollission": p["no_collision"] = True
            elif name == "noOcclusion": p["no_occlusion"] = True
            elif name == "friction" and consts: p["friction"] = consts[0]
            elif name == "speedFactor" and consts: p["speed"] = consts[0]
    # lightLevel(s -> N): the lambda returns a constant
    for mname, code in ms.items():
        if mname.startswith("lambda$") and any(op == "ireturn" for _, op, _ in code):
            vals = [C.const_of(op, r) for _, op, r in code if op.startswith(("iconst", "bipush"))]
            if vals and isinstance(vals[-1], int) and 0 < vals[-1] <= 15: p["light"] = vals[-1]
    return p


SUPER_KIND = {"SwordItem": "sword", "PickaxeItem": "pickaxe", "AxeItem": "axe", "ShovelItem": "shovel", "HoeItem": "hoe",
              "ArmorItem": "armor", "BowItem": "bow", "ShieldItem": "shield", "RecordItem": "record", "BlockItem": "block"}


def item_info(tmp, cls):
    info = {"components": {}}
    try: dis = C.javap(tmp, cls); ms = C.parse_methods(dis)
    except Exception: return info
    sup = re.search(r"extends ([\w.$]+)", dis); sup = sup.group(1).split(".")[-1] if sup else "Item"
    info["kind"] = SUPER_KIND.get(sup, "item")
    comps = info["components"]; food = {}
    for mname, code in ms.items():
        for name, consts, own, desc in calls(code):
            if name == "stacksTo" and consts: comps["minecraft:max_stack_size"] = int(consts[0])
            elif name == "durability" and consts: comps["minecraft:durability"] = {"max_durability": int(consts[0])}; comps["minecraft:max_stack_size"] = 1
            elif name == "fireResistant": comps["minecraft:fire_resistant"] = {"value": True} if False else comps.get("minecraft:fire_resistant", {})
            elif name == "nutrition" and consts: food["nutrition"] = int(consts[0])
            elif name == "saturationMod" and consts: food["saturation_modifier"] = float(consts[0])
            elif name == "alwaysEat": food["can_always_eat"] = True
            elif name == "rarity": pass
        if mname == "<init>" and info["kind"] in ("sword", "pickaxe", "axe", "shovel", "hoe"):
            sup_call = [c for c in calls(code) if c[0] == "<init>" and sup in (c[2] or "")]
            if sup_call and sup_call[-1][1]:
                info["attack"] = float(sup_call[-1][1][0])
    comps.pop("minecraft:fire_resistant", None)
    if food:
        comps["minecraft:food"] = {"nutrition": max(0, food.get("nutrition", 2)), "saturation_modifier": max(0.0, food.get("saturation_modifier", 0.3)),
                                   "can_always_eat": food.get("can_always_eat", False)}
        comps["minecraft:use_animation"] = "eat"; comps["minecraft:use_modifiers"] = {"use_duration": 1.6, "movement_modifier": 0.35}
        info["category"] = "nature"
    if info["kind"] in ("sword", "pickaxe", "axe", "shovel", "hoe"):
        comps["minecraft:hand_equipped"] = True; comps["minecraft:max_stack_size"] = 1
        comps["minecraft:damage"] = int(4 + info.get("attack", 0)); info["category"] = "equipment"
        comps.setdefault("minecraft:durability", {"max_durability": 250})
    if "Tick" in dis or "m_6883_" in dis or "use" in dis: pass
    return info
