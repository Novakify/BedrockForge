"""JavaBridge build step: a Forge mod jar -> scripts/javabridge/*.js (translated mod code + runtime + event hooks).

    from bridge import build_bridge
    build_bridge(jar, modid, ns, bp_dir, entity_map, report)

entity_map: {java class (dots): bedrock entity id}. Hooks:
  entities  baseTick/aiStep (every tick), finalizeSpawn (spawn), hurt (after damage; Java 'false' = undo),
            die, mobInteract (player interact), doHurtTarget (melee hit), playerTouch (overlap)
  blocks    onPlace, use (interact), entityInside / stepOn (player standing in/on), playerWillDestroy, randomTick (sampled)
  items     use (itemUse), finishUsingItem (itemCompleteUse: food), hurtEnemy (hit with item), inventoryTick (held/armor)
  effects   applyEffectTick for the mod's own effects (they live in the runtime's timed-effect table)
  events    @SubscribeEvent statics: PlayerTick, LivingTick, LivingDeath, LivingAttack/Hurt, ServerChat,
            RightClickBlock, BlockBreak/Place, PlayerSleepInBed / WakeUp
"""
from __future__ import annotations
import json, os, re, subprocess, sys, tempfile, zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent))
import jvm2js as J                                  # noqa: E402
from ModelCore.srg import srg_name                  # noqa: E402

ENTITY_HOOKS = {"m_6075_()V": "tick", "m_8119_()V": "tick", "m_8107_()V": "tick", "m_8024_()V": "tick",
                "m_6469_(Lnet/minecraft/world/damagesource/DamageSource;F)Z": "hurt", "m_6667_(Lnet/minecraft/world/damagesource/DamageSource;)V": "die",
                "m_6071_(Lnet/minecraft/world/entity/player/Player;Lnet/minecraft/world/InteractionHand;)Lnet/minecraft/world/InteractionResult;": "interact",
                "m_7327_(Lnet/minecraft/world/entity/Entity;)Z": "attack", "m_6123_(Lnet/minecraft/world/entity/player/Player;)V": "touch"}
SPAWN_PREFIX = "m_6518_("
BLOCK_HOOKS = {"m_6807_": "place", "m_6227_": "use", "m_7892_": "inside", "m_141947_": "stepOn", "m_5707_": "destroy", "m_213898_": "randomTick", "m_213897_": "tick", "m_6256_": "attack"}
ITEM_HOOKS = {"m_7203_": "use", "m_5922_": "finish", "m_7579_": "hurtEnemy", "m_6883_": "invTick", "m_6225_": "useOn"}
EFFECT_HOOKS = {"m_6742_": "effectTick"}
EVENTS = {"TickEvent$PlayerTickEvent": "playerTick", "LivingEvent$LivingTickEvent": "livingTick", "LivingDeathEvent": "death",
          "LivingAttackEvent": "attack", "LivingHurtEvent": "attack", "ServerChatEvent": "chat", "PlayerInteractEvent$RightClickBlock": "rightClickBlock",
          "BlockEvent$BreakEvent": "breakBlock", "BlockEvent$EntityPlaceEvent": "placeBlock", "PlayerSleepInBedEvent": "sleep", "PlayerWakeUpEvent": "wake",
          "EntityJoinLevelEvent": "join", "PlayerEvent$PlayerLoggedInEvent": "login", "PlayerEvent$PlayerRespawnEvent": "respawn",
          "PlayerEvent$PlayerChangedDimensionEvent": "changedDim", "TickEvent$ServerTickEvent": "serverTick"}


def disassemble(jar, prefixes, exclude=("/client/",)):
    tmp = Path(tempfile.mkdtemp(prefix="jb_"))
    with zipfile.ZipFile(jar) as z:
        names = [n for n in z.namelist() if n.endswith(".class") and n.startswith(prefixes) and not any(x in n for x in exclude)]
        for n in names: z.extract(n, tmp)
    classes = [n[:-6].replace("/", ".") for n in names]
    batches = [classes[i:i + 150] for i in range(0, len(classes), 150)]
    with ThreadPoolExecutor(8) as ex: texts = list(ex.map(lambda b: J.javap(tmp, b), batches))
    out = {}
    for t in texts: out.update(J.split_classes(t))
    return out, tmp


def overlays_of(jar, prefixes):
    """Client GUI overlays (RenderGuiEvent): [{"proc": display procedure fid, "texture": asset path}]."""
    tmp = Path(tempfile.mkdtemp(prefix="jb_ov_")); out = []
    with zipfile.ZipFile(jar) as z:
        names = [n for n in z.namelist() if n.endswith("Overlay.class") and "/client/" in n and n.startswith(tuple(prefixes))]
        for n in names: z.extract(n, tmp)
    if not names: return out
    text = J.javap(tmp, [n[:-6].replace("/", ".") for n in names])
    for cn, t in J.split_classes(text).items():
        proc = re.search(r"invokestatic\s+#\d+\s+// Method ([\w/$]+Procedure)\.execute:(\([^)]*\)Z)", t)
        tex = re.search(r"// String (\w+:textures/screens/[\w/.]+\.png)", t)
        if proc and tex:
            out.append({"cls": cn, "proc": f"{proc.group(1)}.execute{proc.group(2)}", "texture": tex.group(1)})
    return out


MCDATA_J2B = Path("/home/claude/tools/mcdata/package/minecraft-data/data/bedrock/1.26.30/blocksJ2B.json")   # minecraft-data (MIT)


GEYSER_SOUNDS = Path("/home/claude/tools/geyser/sounds.json")   # GeyserMC/mappings sounds.json (MIT), fetched locally


def vanilla_sounds():
    """Java sound event -> Bedrock playsound name (entity.zombie.ambient -> mob.zombie.say)."""
    if not GEYSER_SOUNDS.exists(): return {}
    import json as _j
    return {k: v["playsound_mapping"] for k, v in _j.loads(GEYSER_SOUNDS.read_text()).items() if v.get("playsound_mapping")}


def block_j2b():
    """Java block id -> Bedrock block id where the names differ (snow_block -> snow, ...), for commands and setBlock."""
    if not MCDATA_J2B.exists(): return {}
    import json as _j
    votes = {}
    for j, b in _j.loads(MCDATA_J2B.read_text()).items():
        jn, bn = j.split("[")[0].replace("minecraft:", ""), b.split("[")[0].replace("minecraft:", "")
        votes.setdefault(jn, {}); votes[jn][bn] = votes[jn].get(bn, 0) + 1
    # a Java id some state of which keeps its name in Bedrock is not renamed (water[level=0] -> water, not flowing_water)
    return {j: max(v, key=v.get) for j, v in votes.items() if j not in v}

ARG_TYPES = {"StringArgumentType": "string", "BoolArgumentType": "bool", "IntegerArgumentType": "int", "DoubleArgumentType": "float",
             "FloatArgumentType": "float", "EntityArgument": "entity", "BlockPosArgument": "pos", "Vec3Argument": "pos"}


def commands_of(T):
    """Brigadier trees built in RegisterCommandsEvent handlers -> [{"path": [literal|<arg:type>...], "fn": lambda fid}].
    Symbolic run of the builder calls: literal()/argument() push a node, executes() attaches the Command lambda,
    then() hangs the child under its parent, register() closes the tree."""
    out = []
    for cn, info in T.classes.items():
        for key, m in info.methods.items():
            code = m["code"]
            if not any("RegisterCommandsEvent" in r or "CommandDispatcher.register" in r for _, _, r in code): continue
            stack, last_str, last_type = [], None, None
            def walk(node, path):
                p2 = path + [node["lit"]]
                if node.get("fn"): out.append({"path": p2, "fn": node["fn"]})
                for c in node["kids"]: walk(c, p2)
            for _, op, rest in code:
                if op.startswith("ldc"):
                    sm = re.search(r"// String (\S+)", rest); last_str = sm.group(1) if sm else last_str
                elif op == "invokestatic" and ("Commands.m_82127_" in rest or "Commands.literal" in rest):
                    stack.append({"lit": last_str, "kids": [], "fn": None})
                elif op == "invokestatic" and ("Commands.m_82129_" in rest or "Commands.argument" in rest):
                    stack.append({"lit": f"<{last_str}:{last_type or 'string'}>", "kids": [], "fn": None}); last_type = None
                elif op == "invokestatic" and any(f"/{k}." in rest for k in ARG_TYPES):
                    last_type = next(v for k, v in ARG_TYPES.items() if f"/{k}." in rest)
                elif op == "invokedynamic":
                    bm = re.search(r"InvokeDynamic #(\d+):(\w+)", rest)
                    b = info.boot.get(int(bm.group(1)), {}) if bm else {}
                    if bm and bm.group(2) == "run" and b.get("target"):
                        t = b["target"]; stack.append({"lambda": f"{t[0]}.{t[1]}{t[2]}"})
                    else: stack.append({"other": True})
                elif op in ("invokevirtual", "invokeinterface"):
                    if ".executes:" in rest and len(stack) >= 2 and "lambda" in stack[-1]:
                        lam = stack.pop(); stack[-1]["fn"] = lam["lambda"]
                    elif ".requires:" in rest and stack and "other" in stack[-1]: stack.pop()
                    elif ".then:" in rest and len(stack) >= 2 and "lit" in stack[-1] and "lit" in stack[-2]:
                        child = stack.pop(); stack[-1]["kids"].append(child)
                    elif "CommandDispatcher.register" in rest and stack and "lit" in stack[-1]:
                        walk(stack.pop(), [])
    return out



GOAL_METHODS = {"canUse": "m_8036_()Z", "canContinueToUse": "m_8045_()Z", "start": "m_8056_()V", "tick": "m_8037_()V", "stop": "m_8041_()V"}


def build_bridge(jar, modid, ns, bp, entity_map, report, registry_classes=None, structures=None, block_rename=None, mod_prefixes=None, goals=None):
    rep = report.setdefault("javabridge", {})
    with zipfile.ZipFile(jar) as z:
        pkgs = {n.rsplit("/", 1)[0] for n in z.namelist() if n.endswith(".class") and "/" in n}
    # the mod's root package: where its main @Mod class lives (deepest common package of the registry classes)
    prefixes = mod_prefixes or sorted({"/".join(n.split("/")[:3]) + "/" for n in pkgs if not n.startswith(("software/", "com/mojang", "net/minecraft/"))})
    texts, tmp = disassemble(jar, tuple(prefixes))
    T = J.Transpiler(prefixes)
    for name, text in texts.items(): T.add(J.ClassInfo(name, text))
    # per-method JS + references
    meth_js, refs = {}, {}
    for cn, info in T.classes.items():
        for key, m in info.methods.items():
            fid = f"{cn}.{key}"
            try: js = T.method(cn, m)
            except Exception as ex: rep.setdefault("errors", []).append(f"{fid}: {ex}"); continue
            if not js: continue
            meth_js[fid] = js
            r = set(re.findall(r'@@(.+?)@@', js))
            for k in re.findall(r'M\.vcall\(o,"([^"]+)"', js):
                r |= {f"{c}.{k}" for c, i in T.classes.items() if k in i.methods}
            for c in re.findall(r'M\.sget\("([^"]+)"', js): r.add(f"{c}.<clinit>()V")
            refs[fid] = r
    # roots: hooks
    hooks = {"entity": {}, "block": {}, "item": {}, "effect": {}, "events": []}
    cls_type = {c.replace(".", "/"): t for c, t in entity_map.items()}
    for cn, info in T.classes.items():
        if cn in cls_type:
            hk = {}
            for key in info.methods:
                if key in ENTITY_HOOKS: hk.setdefault(ENTITY_HOOKS[key], []).append(f"{cn}.{key}")
                if key.startswith(SPAWN_PREFIX): hk.setdefault("spawn", []).append(f"{cn}.{key}")
            if hk: hooks["entity"][cls_type[cn]] = hk
        low = cn.split("/")[-1]
        for key in info.methods:
            nm = key.split("(")[0]
            if low.endswith("Block") and nm in BLOCK_HOOKS: hooks["block"].setdefault(cn, {})[BLOCK_HOOKS[nm]] = f"{cn}.{key}"
            if low.endswith("Item") and nm in ITEM_HOOKS: hooks["item"].setdefault(cn, {})[ITEM_HOOKS[nm]] = f"{cn}.{key}"
            if low.endswith("MobEffect") and nm in EFFECT_HOOKS: hooks["effect"][cn] = f"{cn}.{key}"
            m = info.methods[key]
            if m["static"]:
                a, _ = J.arg_types(m["desc"])
                if len(a) == 1 and a[0].startswith("Lnet/minecraftforge/event"):
                    ev = a[0][1:-1].split("/")[-1]
                    if ev in EVENTS: hooks["events"].append({"kind": EVENTS[ev], "fn": f"{cn}.{key}"})
    roots = set()
    for d in hooks["entity"].values():
        for v in d.values(): roots |= set(v)
    for d in list(hooks["block"].values()) + list(hooks["item"].values()): roots |= set(d.values())
    roots |= set(hooks["effect"].values()) | {e["fn"] for e in hooks["events"]}
    # MobAI GoalRunner: the mod's goal methods (anonymous MCreator goals, goals written from scratch) - see docs/PORT_MOBAI.md
    hooks["goals"] = {}
    for etype, specs in (goals or {}).items():
        lst = []
        for g in specs:
            fids = {m: f"{g['cls']}.{GOAL_METHODS[m]}" for m in GOAL_METHODS if g.get("cls") and f"{g['cls']}.{GOAL_METHODS[m]}" in meth_js}
            if not fids: rep.setdefault("goal_missing", []).append(f"{etype}:{g.get('cls')}"); continue
            roots |= set(fids.values())
            lst.append({"mode": g["mode"], "cls": g["cls"], "priority": g["priority"], "index": g.get("index"), "fn": fids})
        if lst: hooks["goals"][etype] = lst
    hooks["commands"] = commands_of(T)
    roots |= {c["fn"] for c in hooks["commands"]}
    ovs = overlays_of(jar, prefixes)
    hooks["overlays"] = [{"proc": o["proc"], "key": "§" + "§".join(f"{i:02d}") + "§r", "tex": o["texture"]} for i, o in enumerate(ovs)]
    roots |= {o["proc"] for o in ovs}
    for cn in T.classes:                                  # static data the runtime may need
        if cn.endswith(("Variables$MapVariables", "Variables$PlayerVariables")): roots |= {f"{cn}.{k}" for k in T.classes[cn].methods}
        if cn.split("/")[-1].endswith("Configs") or "/configuration/" in cn: roots.add(f"{cn}.<clinit>()V")
    seen, todo = set(), list(roots)
    while todo:
        f = todo.pop()
        if f in seen or f not in meth_js: continue
        seen.add(f); todo += list(refs.get(f, ()))
    order = sorted(seen); idx = {f: i for i, f in enumerate(order)}
    def target(f):
        if re.search(r"Variables\$(MapVariables|WorldVariables)\.get\(", f): return "R.mapVars"     # SavedData -> world dynamic properties
        return f"FX[{idx[f]}]" if f in idx else f"M.miss({json.dumps(f)})"
    def link(js):   # direct calls through the FX table (no string lookups at run time)
        return re.sub(r"@@(.+?)@@", lambda m: target(m.group(1)), js)
    body = ["const FX=[];"] + [f"FX[{idx[f]}]={link(meth_js[f])};M.def({json.dumps(f)},FX[{idx[f]}]);" for f in order]
    body += [f"M.cls({json.dumps(cn)},{json.dumps(i.super)},{json.dumps(list(i.methods))});" for cn, i in T.classes.items()]
    # registries (simple class name -> kind), entity class <-> type maps, variable defaults
    regs = registry_classes or {}
    for cn in T.classes:
        s = cn.split("/")[-1]
        for suf, kind in (("Blocks", "block"), ("Items", "item"), ("Entities", "entity"), ("MobEffects", "effect"), ("Sounds", "sound"), ("ParticleTypes", "particle"), ("Potions", "potion")):
            if s.endswith(suf) and s.startswith(tuple(p.split("/")[-2].title() for p in prefixes) + ("TheWonderland", "Mod")): regs[s] = kind
    cfg = {"ns": ns, "modid": modid, "registries": regs, "typeToClass": {t: c for c, t in cls_type.items()}, "classToType": cls_type,
           "structures": structures or {}, "blockRename": block_rename or {}, "hooks": hooks,
           "pvDefaults": field_defaults(T, "Variables$PlayerVariables"), "mvDefaults": field_defaults(T, "Variables$MapVariables"), "particles": PARTICLES, "blockJ2B": block_j2b(), "vanillaSounds": vanilla_sounds(),
           # MCreator's Mod.queueServerWork(ticks, runnable): run on Bedrock's scheduler instead of the ServerTickEvent queue
           "workQueue": sorted(f"{cn}.{k}" for cn, info in T.classes.items() for k in info.methods if k == "queueServerWork(ILjava/lang/Runnable;)V")}
    out = Path(bp) / "scripts" / "javabridge"; out.mkdir(parents=True, exist_ok=True)
    (out / "runtime.js").write_text((HERE / "runtime.js").read_text())
    (out / "mod.js").write_text("// JavaBridge: the mod's own code translated from its bytecode (private use only)\n"
                                "import { M, R } from './runtime.js';\n" + "\n".join(body) + "\n")
    (out / "config.js").write_text("globalThis.__JB_CFG = " + json.dumps(cfg, separators=(",", ":")) + ";\n")
    (out / "hooks.js").write_text((HERE / "hooks.js").read_text())
    (out / "main.js").write_text("import './config.js';\nimport './mod.js';\nimport './hooks.js';\n")
    rep.update({"classes": len(T.classes), "methods": len(meth_js), "reachable": len(seen),
                "hooks": {k: (len(v) if not isinstance(v, list) else len(v)) for k, v in hooks.items()},
                "unknown_ops": T.unknown, "api_calls": len(T.api), "js_bytes": sum(len(b) for b in body)})
    return rep


def field_defaults(T, suffix):
    """Field initialisers in the <init> of MCreator's variable classes (constant -> putfield)."""
    out = {}
    for cn, info in T.classes.items():
        if not cn.endswith(suffix): continue
        for key, m in info.methods.items():
            if not key.startswith("<init>"): continue
            last = None
            for off, op, rest in m["code"]:
                c = J.comment(rest)
                if op.startswith(("iconst", "dconst", "fconst", "bipush", "sipush")): last = float(op.split("_")[1].replace("m1", "-1")) if "_" in op else float(rest.split()[0])
                elif op in ("ldc", "ldc_w", "ldc2_w"):
                    mm = re.match(r"String (.*)$", c); mn = re.match(r"(?:int|float|double|long) (\S+?)[dfl]?$", c)
                    last = mm.group(1) if mm else float(mn.group(1)) if mn else None
                elif op == "putfield":
                    own, nm, desc = J.member(rest)
                    if last is not None: out[nm] = (bool(last) if desc == "Z" else last)
                    last = None
                elif op not in ("aload_0", "dup"): last = None
    return out


PARTICLES = {"squid_ink": "minecraft:basic_smoke_particle", "glow_squid_ink": "minecraft:glow_particle", "damage_indicator": "minecraft:critical_hit_emitter",
             "smoke": "minecraft:basic_smoke_particle", "large_smoke": "minecraft:large_explosion", "flame": "minecraft:basic_flame_particle",
             "soul": "minecraft:soul_particle", "portal": "minecraft:portal_directional", "end_rod": "minecraft:endrod", "cloud": "minecraft:evaporation_elephant_toothpaste_vapor_particle",
             "ash": "minecraft:basic_smoke_particle", "sculk_soul": "minecraft:sculk_soul_particle", "explosion": "minecraft:huge_explosion_emitter",
             "block_dust": "minecraft:falling_dust_gravel_particle", "block": "minecraft:falling_dust_gravel_particle", "falling_dust": "minecraft:falling_dust_sand_particle",
             "dust": "minecraft:redstone_wire_dust_particle", "crit": "minecraft:critical_hit_emitter", "enchanted_hit": "minecraft:critical_hit_emitter",
             "heart": "minecraft:heart_particle", "angry_villager": "minecraft:villager_angry", "happy_villager": "minecraft:villager_happy",
             "witch": "minecraft:witchspell_emitter", "dripping_water": "minecraft:water_drip_particle", "dripping_lava": "minecraft:lava_drip_particle",
             "lava": "minecraft:lava_particle", "bubble": "minecraft:basic_bubble_particle", "splash": "minecraft:water_splash_particle",
             "note": "minecraft:note_particle", "enchant": "minecraft:enchanting_table_particle", "totem_of_undying": "minecraft:totem_particle",
             "sculk_charge": "minecraft:sculk_charge_particle", "soul_fire_flame": "minecraft:blue_flame_particle", "snowflake": "minecraft:snowflake_particle",
             "white_ash": "minecraft:white_ash_particle", "crimson_spore": "minecraft:crimson_spore_particle", "warped_spore": "minecraft:warped_spore_particle",
             "poof": "minecraft:explosion_particle", "electric_spark": "minecraft:electric_spark_particle", "glow": "minecraft:glow_particle"}
