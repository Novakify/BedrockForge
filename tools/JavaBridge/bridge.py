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
          "PlayerEvent$PlayerChangedDimensionEvent": "changedDim"}


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


def build_bridge(jar, modid, ns, bp, entity_map, report, registry_classes=None, structures=None, block_rename=None, mod_prefixes=None):
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
           "pvDefaults": field_defaults(T, "Variables$PlayerVariables"), "mvDefaults": field_defaults(T, "Variables$MapVariables"), "particles": PARTICLES}
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
             "ash": "minecraft:basic_smoke_particle", "sculk_soul": "minecraft:sculk_soul_particle", "explosion": "minecraft:huge_explosion_emitter"}
