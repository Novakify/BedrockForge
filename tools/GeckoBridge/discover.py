#!/usr/bin/env python3
"""GeckoBridge entity discovery (M1): which GeckoLib mobs a mod has and where their assets are.

ModPorter's extract.py was written for MCreator mods (classes named *Entity, one registry layout). GeckoLib mods
vary, so this works from GeckoLib's own types instead (bytecode via javap, nothing is run):
  - entity classes   = classes with registerControllers (controllers.py)
  - id / size / kind = any class that passes `Class::new` (invokedynamic -> REF_newInvokeSpecial) next to a string
                       id and two floats (EntityType.Builder.sized or a helper like registerEntityType(id, F::new, cat, w, h))
  - model paths      = GeoModel<Entity> subclasses: getModelResource / getTextureResource / getAnimationResource
                       (texture variants: every return path of getTextureResource with its condition, via controllers.run_paths)
                       DefaultedEntityGeoModel(id) -> geo/entity/<id>.geo.json etc.
  - renderer         = GeoEntityRenderer<Entity>: render layers (AutoGlowingGeoLayer = <tex>_glowmask.png, custom layers)
  - stats            = ModPorter extract.parse_attributes on createAttributes
Writes an entities.json that ModPorter's generate.py understands, plus GeckoBridge fields (variants, layers, category).

    python -I tools/GeckoBridge/discover.py mod.jar -o OUT/      -> OUT/entities.json + OUT/controllers.json
"""
from __future__ import annotations
import argparse, json, os, re, sys, tempfile, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent / "ModPorter"))
import controllers as C          # noqa: E402
import extract as MP             # noqa: E402  (ModPorter: parse_attributes, methods, SRG tables)

CATEGORY_KIND = {"MONSTER": "hostile", "CREATURE": "passive", "AMBIENT": "ambient", "AXOLOTLS": "water",
                 "UNDERGROUND_WATER_CREATURE": "water", "WATER_CREATURE": "water", "WATER_AMBIENT": "water", "MISC": "misc"}
SWIM_HINTS = ("WaterAnimal", "AbstractFish", "AbstractSchoolingFish", "WaterBoundPathNavigation", "AmphibiousPathNavigation")
FLY_HINTS = ("FlyingMob", "FlyingPathNavigation", "FlyingMoveControl", "FlyingAnimal")
PROJECTILE_HINTS = ("Projectile", "AbstractArrow", "ThrowableProjectile", "AbstractHurtingProjectile", "ThrowableItemProjectile")


def snake(name):
    base = re.sub(r"Entity$", "", name)
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", base).lower()


def registry(tmp, all_classes):
    """id -> {class, size, category} from Class::new constructor references next to string ids."""
    out = {}
    for cls in all_classes:
        raw = Path(tmp, cls.replace(".", "/") + ".class").read_bytes()
        if b"REF_newInvokeSpecial" not in raw and b"EntityType" not in raw: continue
        if b"EntityType" not in raw: continue
        dis = C.javap(tmp, cls); C.bootstrap_targets(C.javap(tmp, cls, verbose=True)); boot = dict(C.FULL_TARGETS)
        methods = C.parse_methods(dis)
        helpers = {}    # helper(String id, float w, float h) that builds the EntityType itself: id comes from each caller
        for mname, code in methods.items():
            last_str, i, last_id = None, 0, None
            sig = re.search(rf"\b{re.escape(mname.split('#')[0])}\(([^)]*)\)", dis)
            takes_id = bool(sig and sig.group(1).split(",")[0].strip() == "java.lang.String")
            while i < len(code):
                off, op, rest = code[i]
                v = C.const_of(op, rest) if op.startswith(("ldc", "iconst", "bipush", "sipush", "fconst")) else None
                if isinstance(v, str) and re.fullmatch(r"[a-z0-9_]+", v): last_str = v
                if op == "invokedynamic":
                    mk = re.search(r"InvokeDynamic #(\d+):", rest)
                    t = boot.get(int(mk.group(1))) if mk else None
                    if t and t[0] == "newInvokeSpecial":
                        floats, cat, after_str = [], None, None
                        for off2, op2, rest2 in code[i + 1:i + 24]:
                            if "MobCategory." in rest2: cat = re.search(r"MobCategory\.(\w+)", rest2).group(1)
                            if op2.startswith(("ldc", "fconst")):
                                f = C.const_of(op2, rest2)
                                if isinstance(f, float) and len(floats) < 2: floats.append(f)
                                if isinstance(f, str) and re.fullmatch(r"[a-z0-9_]+", f) and not after_str: after_str = f
                            if op2 == "invokedynamic": break       # next registration
                        eid = last_str or after_str               # helper(id, F::new, ...) or Builder.of(F::new).sized(..).build(id)
                        if takes_id and any(o.startswith("aload_0") or o == "aload" and r.strip() == "0" for _, o, r in code[:i]) \
                                and not any(op2.startswith("ldc") and isinstance(C.const_of(op2, r2), str) and C.const_of(op2, r2) == eid and "build" in " ".join(x for _, _, x in code[j:j + 3])
                                            for j, (_, op2, r2) in enumerate(code)):
                            helpers[mname.split("#")[0]] = {"class": t[1], "category": cat}; i += 1; continue
                        if eid: out.setdefault(eid, {"class": t[1], "size": floats[:2] if len(floats) >= 2 else None, "category": cat}); last_id = eid
                if op == "putstatic" and last_id and ("RegistryObject" in rest or "EntityType" in rest or "Supplier" in rest):
                    out[last_id].setdefault("field", C.member(rest)[1]); last_id = None
                i += 1
        # callers of the helpers: ldc "id", [floats], invokestatic helper
        for code in (methods.values() if helpers else []):
            for i, (off, op, rest) in enumerate(code):
                if op != "invokestatic" or C.member(rest)[1] not in helpers: continue
                h = helpers[C.member(rest)[1]]; prev = code[max(0, i - 8):i]
                ids = [C.const_of(o, r) for _, o, r in prev if o.startswith("ldc") and isinstance(C.const_of(o, r), str)]
                fl = [C.const_of(o, r) for _, o, r in prev if o.startswith(("ldc", "fconst")) and isinstance(C.const_of(o, r), float)]
                if ids and re.fullmatch(r"[a-z0-9_]+", ids[-1]):
                    out.setdefault(ids[-1], {"class": h["class"], "size": fl[-2:] if len(fl) >= 2 else None, "category": h["category"]})
                    nxt = code[i + 1] if i + 1 < len(code) else None
                    if nxt and nxt[1] == "putstatic": out[ids[-1]].setdefault("field", C.member(nxt[2])[1])
    return out


def generic_target(dis, base):
    m = re.search(rf"extends [\w.$]*{base}<([\w.$]+)", dis)
    return m.group(1) if m else None


def model_info(tmp, cls, modid, names):
    """GeoModel class -> geo / animation paths and texture variant paths."""
    dis = C.javap(tmp, cls); ms = C.parse_methods(dis); C.bootstrap_targets(C.javap(tmp, cls, verbose=True))
    def strings(mname):
        return [C.const_of(op, rest) for off, op, rest in ms.get(mname, []) if op.startswith("ldc") and isinstance(C.const_of(op, rest), str)]
    def asset(s):
        if s is None: return None
        full = f"assets/{s.split(':', 1)[0]}/{s.split(':', 1)[1]}" if ":" in s else f"assets/{modid}/{s}"
        if "*" in full:   # string built at runtime (config suffix etc.): the shortest matching file = default choice
            rx = re.compile("^" + re.escape(full).replace("\\*", ".*") + "$")
            hits = sorted((n for n in names if rx.match(n)), key=len)
            if hits: patterns[hits[0]] = full
            return hits[0] if hits else full
        return full
    patterns = {}
    info = {"model_class": cls}
    reserved = {}
    GL5 = {"geo": ("geckolib/models/", ".geo.json"), "animations": ("geckolib/animations/", ".animation.json")}

    def resolve(s, want):
        """a path string from the model -> jar asset. GeckoLib 5 ids are bare ('entity/mochi'): GeckoLib adds the
        geckolib/models/ (or animations/) folder and the .geo.json suffix itself."""
        if not isinstance(s, str) or (want == "textures" and not (".png" in s or "textures" in s)): return None
        if want in s or (want == "textures"): return asset(s)
        if want in GL5 and "." not in s.rsplit("/", 1)[-1]:
            ns, pth = s.split(":", 1) if ":" in s else (None, s)
            pre, suf = GL5[want]
            cand = asset((f"{ns}:" if ns else "") + pre + pth + suf)
            if cand in names or (cand and "*" in cand): return cand
            c2 = asset((f"{ns}:" if ns else "") + {"geo": "geo/", "animations": "animations/"}[want] + pth + suf)
            if c2 in names: return c2       # GeckoLib 4 layout reached with a bare id
        return None

    # static ResourceLocation fields set in <clinit>: field -> its path string
    sfields, lasts = {}, []
    for off, op, rest in ms.get("<clinit>", []):
        if op.startswith("ldc"):
            v = C.const_of(op, rest)
            if isinstance(v, str): lasts.append(v)
        elif op == "invokedynamic":
            mk = re.search(r"InvokeDynamic #(\d+):", rest)
            if mk and C.CONCAT.get(int(mk.group(1))): lasts.append(C.CONCAT[int(mk.group(1))])
        elif op == "putstatic":
            if "ResourceLocation" in rest and lasts:
                sfields[C.member(rest)[1]] = (lasts[-2] + ":" + lasts[-1]) if len(lasts) > 1 and "/" not in lasts[-2] and ":" not in lasts[-1] else lasts[-1]
            lasts = []

    def returns(mname, want):
        """Every return path of a resource getter -> [{"when", "path"}] (conditions in Molang)."""
        code = ms.get(mname)
        if not code: return []
        # static ResourceLocation[] indexed by a variant getter: TEXTURES[getVariant()]
        arr = next((C.member(rest)[1] for off, op, rest in code if op == "getstatic" and "[L" in rest and "ResourceLocation" in rest), None)
        if arr and any(op == "aaload" for _, op, _ in code):
            init = [C.const_of(op, rest) for off, op, rest in ms.get("<clinit>", []) if op.startswith("ldc")]
            paths = [x for x in init if isinstance(x, str) and want in x]
            return [{"when": f"(q.variant == {i})", "path": asset(x)} for i, x in enumerate(paths)]
        out = []
        for p in C.run_paths(code, {}):
            r = p.ret
            path = None
            if isinstance(r, tuple) and r[0] == "obj":
                strs = [a[1] for a in r[2] if isinstance(a, tuple) and a[0] == "const" and isinstance(a[1], str)]
                ns = strs[0] if len(strs) > 1 and "/" not in strs[0] and ":" not in strs[0] else None
                path = next((x for x in (resolve((f"{ns}:" if ns else "") + y, want) for y in strs[::-1] if y != ns) if x), None)
            elif isinstance(r, tuple) and r[0] == "field" and r[2] in sfields:
                path = resolve(sfields[r[2]], want)
            terms = C.path_terms(p.conds, reserved) if path else None
            if path and terms is not None:
                out.append({"when": " && ".join(terms) or "1", "path": path})
        if not out:   # constant fallback: any matching string literal in the method
            out = [{"when": "1", "path": asset(x)} for x in strings(mname) if want in x][:1]
        return out

    def grouped(rows):
        """same asset on several paths -> one row with the conditions OR-ed; keeps first-seen order."""
        g = {}
        for r in rows: g.setdefault(r["path"], []).append(r["when"])
        return [{"when": "1" if "1" in ws else " || ".join(f"({w})" for w in ws), "path": pth} for pth, ws in g.items()]

    geos = grouped(returns("getModelResource", "geo"))
    anims = grouped(returns("getAnimationResource", "animations"))
    variants = [{"when": r["when"], "texture": r["path"]} for r in grouped(returns("getTextureResource", "textures"))]
    def default(rows): return (next((r for r in rows if r["when"] == "1"), rows[-1])["path"]) if rows else None
    info["geo"], info["animation_file"] = default(geos), default(anims)
    info["geo_variants"] = geos if len(geos) > 1 else []
    info["animation_files"] = [r["path"] for r in anims if r["path"] in names] if len(anims) > 1 else []
    info["reserved"] = reserved
    # DefaultedEntityGeoModel(new ResourceLocation(modid, "name")): paths by convention
    if "DefaultedEntityGeoModel" in dis or "DefaultedGeoModel" in dis:
        nm = next((s for s in sum((strings(k) for k in ms if k.startswith("<init>")), []) if s and s != modid and "/" not in s and "." not in s), None)
        if nm:
            def first(*c): return next((x for x in c if x in names), c[0])
            info["geo"] = info["geo"] or first(f"assets/{modid}/geo/entity/{nm}.geo.json", f"assets/{modid}/geckolib/models/entity/{nm}.geo.json")
            info["animation_file"] = info["animation_file"] or first(f"assets/{modid}/animations/entity/{nm}.animation.json",
                                                                     f"assets/{modid}/geckolib/animations/entity/{nm}.animation.json")
            if not variants: variants = [{"when": "1", "texture": f"assets/{modid}/textures/entity/{nm}.png"}]
    variants = enum_variants(tmp, ms.get("getTextureResource", []), variants, patterns, names)
    variants = synced_texture(tmp, ms.get("getTextureResource", []), variants, patterns, names)
    info["variants"] = [v for v in variants if v["texture"] in names] or variants
    return info


def synced_texture(tmp, code, variants, patterns, names):
    """MCreator: "textures/entities/" + entity.getTexture() + ".png", where getTexture() reads a synced String
    (entityData.define(TEXTURE, "the_sphere_calm")). Without this the gap was filled by the shortest matching file."""
    if not any(v["texture"] in patterns for v in variants): return variants
    call = next(((C.member(rest)[0], C.member(rest)[1]) for off, op, rest in code
                 if op == "invokevirtual" and C.member(rest)[2].endswith(")Ljava/lang/String;")
                 and not C.member(rest)[0].startswith(("java/", "net/minecraft/"))), None)
    if not call: return variants
    ecls, getter = call
    dis = C.javap(tmp, ecls.replace("/", ".")); ms = C.parse_methods(dis)
    field = next((C.member(rest)[1] for off, op, rest in ms.get(getter, []) if op == "getstatic"), None)
    if not field: return variants
    default = None
    for code2 in ms.values():
        for i, (off, op, rest) in enumerate(code2):
            if op == "getstatic" and C.member(rest)[1] == field and i + 2 < len(code2) and code2[i + 1][1].startswith("ldc"):
                v = C.const_of(code2[i + 1][1], code2[i + 1][2])
                if isinstance(v, str) and code2[i + 2][1] == "invokevirtual": default = default or v
    if not default: return variants
    out = []
    for v in variants:
        pat = patterns.get(v["texture"])
        t = pat.replace("*", default, 1) if pat else None
        out.append({**v, "texture": t} if t and t in names else v)
    return out


def enum_variants(tmp, code, variants, patterns, names):
    """texture path built from an enum looked up by the variant id (MochiVariant.byId(id).textureName()):
    one texture row per enum constant, picked with q.variant == ordinal (the constant's lower-cased name fills the gap)."""
    enum = next((C.member(rest)[0] for off, op, rest in code if op == "invokestatic" and C.member(rest)[1] in ("byId", "byOrdinal", "fromId", "values", "byIndex")), None)
    if not enum or not any(v["texture"] in patterns for v in variants): return variants
    dis = C.javap(tmp, enum.replace("/", "."))
    if "extends java.lang.Enum" not in dis: return variants
    consts = re.findall(r"public static final [\w.$]+ (\w+);", dis.split("{", 1)[1].split("(")[0]) if "{" in dis else []
    if len(consts) < 2: return variants
    out = []
    for v in variants:
        pat = patterns.get(v["texture"])
        if not pat: out.append(v); continue
        base = " && ".join(t for t in re.split(r"\s*&&\s*", v["when"].split(" || ")[0].strip("()")) if "gb_variant" not in t) or "1"
        for i, c in enumerate(consts):
            t = pat.replace("*", c.lower(), 1)
            if t in names: out.append({"when": f"(q.variant == {i})" + ("" if base == "1" else f" && {base}"), "texture": t})
    return out if len(out) > len(variants) else variants


def renderer_info(tmp, cls, modid, names):
    dis = C.javap(tmp, cls)
    layers = []
    if "AutoGlowingGeoLayer" in dis: layers.append({"kind": "glowmask"})
    # custom layers drawn with RenderType.eyes (SRG m_110488_) / entityTranslucentEmissive (m_234338_) are emissive
    for m in re.finditer(r"// class ([\w/$]+Layer\w*)\b", dis):
        n = m.group(1).replace("/", ".")
        if "geckolib" not in n and "azurelib" not in n: layers.append({"kind": "custom", "class": n})
    model = re.search(r"// class ([\w/$]+Model)\b", dis)
    tex = [s for s in re.findall(r"// String ([\w/.:-]+\.png)", dis)]
    return {"renderer_class": cls, "model_class": model.group(1).replace("/", ".") if model else None, "layers": layers,
            "renderer_textures": [f"assets/{modid}/{t}" if ":" not in t else f"assets/{t.split(':')[0]}/{t.split(':')[1]}" for t in tex]}


def string_switch(code):
    """javac's switch on a String (hashCode + equals + tableswitch) -> {string: static field read in that case}."""
    idx, pending, cases, fields_by_case = {}, None, {}, {}
    for i, (off, op, rest) in enumerate(code):
        if op.startswith("ldc"):
            v = C.const_of(op, rest)
            if isinstance(v, str) and i + 1 < len(code) and C.member(code[i + 1][2])[1] == "equals": pending = v
        if pending and (op.startswith("iconst_") or op == "bipush") and i > 1 and code[i - 1][1] == "ifeq":
            idx[pending] = int(op.split("_")[1]) if op.startswith("iconst_") and op[-1].isdigit() else int(rest.strip().split()[0]); pending = None
    sw = next((i for i, (_, op, _) in enumerate(code) if op == "tableswitch"), None)
    if sw is None or not idx: return {}
    for off, op, rest in code[sw + 1:]:
        if not op.isdigit(): break
        cases[int(off)] = int(op)
    offs = {off: i for i, (off, _, _) in enumerate(code)}
    for n, target in cases.items():
        j = offs.get(target)
        if j is None: continue
        for off, op, rest in code[j:j + 4]:
            if op == "getstatic" and "ResourceLocation" in rest: fields_by_case[n] = C.member(rest)[1]; break
    return {sname: fields_by_case[n] for sname, n in idx.items() if n in fields_by_case}


def layer_info(tmp, cls, modid, names, ctor_textures=()):
    """Custom GeoRenderLayer -> [{"when", "texture"}]: every path through render() that draws a texture, with its condition."""
    dis = C.javap(tmp, cls); ms = C.parse_methods(dis); C.bootstrap_targets(C.javap(tmp, cls, verbose=True))
    def asset(x):
        if not x: return None
        full = f"assets/{x.split(':', 1)[0]}/{x.split(':', 1)[1]}" if ":" in x else f"assets/{modid}/{x}"
        if "*" in full:
            rx = re.compile("^" + re.escape(full).replace("\\*", ".*") + "$"); hits = sorted((n for n in names if rx.match(n)), key=len)
            return hits[0] if hits else None
        return full if full in names else None
    # static ResourceLocation fields: <clinit> string -> putstatic
    fields, last = {}, None
    for off, op, rest in ms.get("<clinit>", []):
        if op.startswith("ldc"):
            v = C.const_of(op, rest)
            if isinstance(v, str) and (".png" in v or "textures/" in v): last = v
        if op == "invokedynamic":
            mk = re.search(r"InvokeDynamic #(\d+):", rest)
            if mk and C.CONCAT.get(int(mk.group(1))): last = C.CONCAT[int(mk.group(1))]
        if op == "putstatic" and "ResourceLocation" in rest and last: fields[C.member(rest)[1]] = last; last = None
    rows, reserved = [], {}
    for mname, code in ms.items():
        if not mname.startswith("render"): continue
        sw = string_switch(code)
        if sw and "m_7755_" in " ".join(r for _, _, r in code):     # skins chosen by the mob's name (Tortoise TMNT)
            for sname, f in sw.items():
                t = asset(fields.get(f))
                if t:
                    var = "v.gb_name_" + re.sub("[^a-z0-9]", "", sname.lower())[:20]
                    C.RESERVED_INFO[var] = {"java": "m_7755_.getString.equals", "chain": ["m_7755_", "getString", "equals"],
                                            "consts": [sname], "fields": [], "case_sensitive": True}
                    rows.append({"when": f"({var})", "texture": t, "name": sname})
            if rows: break
    for mname, code in ([] if rows else ms.items()):
        if not mname.startswith("render") or "#" in mname and not any(op == "invokestatic" or op == "getstatic" for _, op, _ in code): continue
        for pth in C.run_paths(code, {}):
            texs = [asset(fields.get(r[1])) if r[0] == "field" else (ctor_textures[0] if ctor_textures else None) if r[0] == "ctor"
                    else asset(r[1]) for r in pth.res]
            texs = [t for t in texs if t]
            if not texs: continue
            terms = C.path_terms(pth.conds, reserved)
            if terms is None: continue
            rows.append({"when": " && ".join(terms) or "1", "texture": texs[-1]})
        if rows: break
    g = {}
    for r in rows: g.setdefault(r["texture"], []).append(r["when"])
    emissive = "m_110488_" in dis or "eyes" in dis or "Emissive" in dis or "m_234338_" in dis
    names_ = {r["texture"]: r["name"] for r in rows if r.get("name")}
    return [{"when": "1" if "1" in ws else " || ".join(f"({w})" for w in ws), "texture": t, **({"name": names_[t]} if t in names_ else {})}
            for t, ws in g.items()], emissive


def layer_textures(tmp, cls, modid, names):
    dis = C.javap(tmp, cls)
    C.bootstrap_targets(C.javap(tmp, cls, verbose=True))
    out = []
    for recipe in C.CONCAT.values():
        if recipe and ".png" in recipe:
            rx = re.compile("^" + re.escape(f"assets/{modid}/{recipe}").replace("\\*", ".*") + "$")
            hits = sorted((n for n in names if rx.match(n)), key=len)
            if hits: out.append(hits[0])
    for t in re.findall(r"// String ([\w/.:-]+\.png)", dis):
        p = f"assets/{modid}/{t}" if ":" not in t else f"assets/{t.split(':')[0]}/{t.split(':')[1]}"
        if p in names: out.append(p)
    return out


def discover(jar):
    tmp = tempfile.mkdtemp(prefix="gb_disc_")
    with zipfile.ZipFile(jar) as z:
        names = set(z.namelist())
        for n in names:
            if n.endswith(".class") and not n.startswith(C.GL_PREFIXES): z.extract(n, tmp)
        lang = {}
        modid = next((n.split("/")[1] for n in names if n.startswith("assets/") and n.count("/") > 2
                      and n.split("/")[1] not in ("minecraft", "xaerominimap") and "/lang/" not in n and n.split("/")[2] in ("geo", "textures", "animations")), "?")
        lp = f"assets/{modid}/lang/en_us.json"
        if lp in names:
            try: lang = MP_jload(z.read(lp))
            except Exception: lang = {}
    all_classes = [str(p.relative_to(tmp))[:-6].replace("/", ".") for p in Path(tmp).rglob("*.class")]
    C.prefetch(tmp, all_classes); C.prefetch(tmp, all_classes, verbose=True)     # one JVM per few hundred classes
    ctrl = {"jar": os.path.basename(jar), "entities": []}
    geo_classes = []
    models, renderers = {}, {}
    for c in all_classes:
        raw = Path(tmp, c.replace(".", "/") + ".class").read_bytes()
        if b"registerControllers" in raw and (b"geckolib" in raw or b"azurelib" in raw): geo_classes.append(c)
        elif b"GeoModel" in raw or b"GeoEntityRenderer" in raw:
            dis = C.javap(tmp, c)
            t = generic_target(dis, "GeoModel") or generic_target(dis, "DefaultedEntityGeoModel") or generic_target(dis, "DefaultedGeoModel")
            if t: models[t] = c
            r = generic_target(dis, "GeoEntityRenderer")
            if r: renderers[r] = c
    # AzureLib 3 ("Az" API): AzEntityRenderer<Entity> with AzEntityRendererConfig.builder(geo, texture) and a separate
    # <Name>Animator holding registerControllers. Map the animator's controllers onto the entity class.
    az = {}
    for c in all_classes:
        raw = Path(tmp, c.replace(".", "/") + ".class").read_bytes()
        if b"AzEntityRenderer" not in raw or c.endswith("Config"): continue
        dis = C.javap(tmp, c)
        t = generic_target(dis, "AzEntityRenderer")
        if not t: continue
        strs = re.findall(r"// String ([\w/.:-]+)", dis)
        geo = next((x for x in strs if x.endswith(".geo.json")), None)
        tex = next((x for x in strs if x.endswith(".png")), None)
        if not geo: continue
        stem = Path(geo).name[:-9]
        anim = next((f"assets/{modid}/{d}{stem}.animation.json" for d in ("animations/", "animations/entity/", "geckolib/animations/", "geckolib/animations/entity/") if f"assets/{modid}/{d}{stem}.animation.json" in names), None)
        animator = next((x for x in geo_classes if x.rsplit(".", 1)[1] == c.rsplit(".", 1)[1].replace("Renderer", "Animator")), None)
        az[t] = {"geo": f"assets/{modid}/{geo}", "texture": f"assets/{modid}/{tex}" if tex else None, "anim": anim, "animator": animator, "renderer": c}
    for t, a in az.items():
        if a["animator"] in geo_classes: geo_classes.remove(a["animator"])
        if t not in geo_classes and t in all_classes: geo_classes.append(t)
    for c in sorted(geo_classes):
        if c in az and az[c]["animator"]:
            ce = C.controllers_of(tmp, az[c]["animator"]); ce["class"] = c; ctrl["entities"].append(ce)
        elif c in az: ctrl["entities"].append({"class": c, "controllers": []})
        else: ctrl["entities"].append(C.controllers_of(tmp, c))
    reg = registry(tmp, all_classes)
    by_class = {}
    for eid, r in reg.items(): by_class.setdefault(r["class"], []).append(eid)
    entities = []
    for c in sorted(geo_classes):
        dis = C.javap(tmp, c)
        sup = re.search(r"extends ([\w.$]+)", dis)
        ids = by_class.get(c) or [snake(c.split(".")[-1])]
        mi = model_info(tmp, models[c], modid, names) if c in models else {}
        if c in az:
            a = az[c]; mi = {"geo": a["geo"], "animation_file": a["anim"], "geo_variants": [], "reserved": {},
                             "variants": [{"when": "1", "texture": a["texture"]}] if a["texture"] in names else []}
        ri = renderer_info(tmp, renderers[c], modid, names) if c in renderers else {"layers": []}
        if not mi and ri.get("model_class"):
            mi = model_info(tmp, ri["model_class"], modid, names)
        hier = dis + (C.javap(tmp, sup.group(1)) if sup and sup.group(1) in all_classes else "")
        is_proj = any(h in (sup.group(1) if sup else "") for h in PROJECTILE_HINTS)
        movement = "swim" if any(h in hier for h in SWIM_HINTS) else "fly" if any(h in hier for h in FLY_HINTS) else "walk"
        ms = MP.methods(MP.javap(Path(tmp), c))
        attr_m = next((k for k in ms if k in ("createAttributes", "getAttributeBuilder", "createMobAttributes", "registerAttributes", "m_21183_")), None)
        attributes = MP.parse_attributes(ms.get(attr_m, [])) if attr_m else {}
        glowmask = None
        for v in mi.get("variants", []):
            g = v["texture"].replace(".png", "_glowmask.png")
            if g in names: glowmask = g; break
        layers = []
        for L in ri.get("layers", []):
            if L["kind"] == "custom":
                t = layer_textures(tmp, L["class"], modid, names)
                base_tex = {v["texture"] for v in mi.get("variants", [])}
                ctor = [x for x in ri.get("renderer_textures", []) if x in names and x not in base_tex]
                rows, emissive = layer_info(tmp, L["class"], modid, names, ctor)
                if not rows and t: rows = [{"when": "1", "texture": x} for x in t[:1]]
                layers.append({**L, "textures": t, "rows": rows, "emissive": emissive})
            else: layers.append(L)
        for eid in ids:
            vs = mi.get("variants") or []
            if len(ids) > 1:   # one class, several entity types (birds): pick the textures named after the id
                mine = [v for v in vs if eid in Path(v["texture"]).stem]
                vs = mine or vs
            r = reg.get(eid, {})
            entities.append({
                "id": eid, "class": c.split(".")[-1], "java_class": c, "extends": sup.group(1).split(".")[-1] if sup else None,
                "geckolib": True, "is_mob": not is_proj, "category": r.get("category"),
                "kind": CATEGORY_KIND.get(r.get("category") or "", "hostile" if attributes.get("attack_damage") else "passive"),
                "size": r.get("size"), "movement": movement, "attributes": attributes,
                "geo": mi.get("geo"), "animation_file": mi.get("animation_file"), "animation_files": mi.get("animation_files", []),
                "texture": vs[-1]["texture"] if vs else None, "variants": vs,
                "glowmask": glowmask, "layers": layers, "name": lang.get(f"entity.{modid}.{eid}"),
                "geo_variants": mi.get("geo_variants", []), "reserved": mi.get("reserved", {}),
                "model_class": mi.get("model_class"), "renderer_class": ri.get("renderer_class"), "type_field": r.get("field"),
            })
    entities += vanilla_model_entities(tmp, all_classes, reg, {c for c in geo_classes}, modid, names, lang)
    type_fields = {v["field"]: k for k, v in reg.items() if v.get("field")}
    return {"mod_id": modid, "jar": os.path.basename(jar), "entity_count": len(entities), "entities": entities,
            "type_fields": type_fields, "reserved_info": {k: v for k, v in C.RESERVED_INFO.items()}}, ctrl


# ------------------------------------------------------------------ non-GeckoLib entities (vanilla / Java-model renderers)
VANILLA_RENDER = {"HumanoidModel": "player", "PlayerModel": "player", "CowModel": "cow", "PigModel": "pig"}


def vanilla_model_entities(tmp, all_classes, reg, gecko, modid, names, lang):
    """Registered entity types GeckoLib discovery skipped: their renderer says how they look (vanilla model, the mod's own
    Java model class, eye layers, block/item renderers). build.py turns `vanilla_model` into geometry (vanilla.py)."""
    def asset(t): return f"assets/{modid}/{t}" if ":" not in t else f"assets/{t.split(':')[0]}/{t.split(':')[1]}"
    rend = {}
    for c in all_classes:
        if not c.endswith("Renderer") and "Renderer$" not in c: continue
        raw = Path(tmp, c.replace(".", "/") + ".class").read_bytes()
        if b"geckolib" in raw or b"azurelib" in raw: continue
        dis = C.javap(tmp, c)
        m = re.search(r"extends net\.minecraft\.client\.renderer\.entity\.(\w+)<([\w.$]+)", dis)
        if m and "$" not in c: rend[m.group(2)] = (c, m.group(1), dis)
    out = []
    done = set()
    for eid, r in sorted(reg.items()):
        c = r["class"]
        if c in gecko or c not in all_classes: continue
        dis = C.javap(tmp, c)
        sup = re.search(r"extends ([\w.$]+)", dis)
        hier = dis + (C.javap(tmp, sup.group(1)) if sup and sup.group(1) in all_classes else "")
        is_proj = any(h in (sup.group(1) if sup else "") for h in PROJECTILE_HINTS) or any(h in hier.split("{")[0] for h in PROJECTILE_HINTS)
        ms = MP.methods(MP.javap(Path(tmp), c))
        attr_m = next((k for k in ms if k in ("createAttributes", "getAttributeBuilder", "createMobAttributes", "registerAttributes", "m_21183_")), None)
        attributes = MP.parse_attributes(ms.get(attr_m, [])) if attr_m else {}
        movement = "swim" if any(h in hier for h in SWIM_HINTS) else "fly" if any(h in hier for h in FLY_HINTS) else "walk"
        rc, base, rdis = rend.get(c, (None, None, ""))
        vm = {"renderer": base, "renderer_class": rc}
        tex = [asset(t) for t in re.findall(r"// String ([\w/.:-]+\.png)", rdis)]
        vm["texture"] = next((t for t in tex if t in names), None)
        mcls = re.search(r"// class (net/[\w/$]+/client/model/\w+)\b", rdis) or re.search(r"Method ([\w/$]+)\.\"<init>\":\(Lnet/minecraft/client/model/geom/ModelPart;\)V", rdis)
        model = mcls.group(1).replace("/", ".") if mcls else None
        if model and model.startswith("net.minecraft.client.model."): vm["vanilla"] = VANILLA_RENDER.get(model.split(".")[-1], "player")
        elif model: vm["model_class"] = model
        elif base in ("HumanoidMobRenderer",): vm["vanilla"] = "player"
        elif rc and "BlockRenderDispatcher" in rdis: vm["vanilla"] = "block"
        elif base in ("ThrownItemRenderer",) or is_proj: vm["vanilla"] = "item"
        sc = re.search(r"ldc\s+#\d+\s+// float ([\d.]+)f\s*\n.*\n?.*m_85841_", rdis)
        if sc: vm["scale"] = float(sc.group(1))
        # eye / emissive layers: inner RenderLayer classes of the renderer drawing RenderType.eyes(texture)
        glows = []
        for inner in [x for x in all_classes if rc and x.startswith(rc + "$")]:
            idis = C.javap(tmp, inner)
            t = [asset(x) for x in re.findall(r"// String ([\w/.:-]+\.png)", idis)]
            if t and ("m_110488_" in idis or "m_234338_" in idis or "m_110473_" in idis): glows += [x for x in t if x in names]
        vm["glow"] = glows
        if not rc and not is_proj: vm["vanilla"] = "none"
        out.append({
            "id": eid, "class": c.split(".")[-1], "java_class": c, "extends": sup.group(1).split(".")[-1] if sup else None,
            "geckolib": False, "is_mob": not is_proj, "category": r.get("category"),
            "kind": CATEGORY_KIND.get(r.get("category") or "", "hostile" if attributes.get("attack_damage") else "passive"),
            "size": r.get("size"), "movement": movement, "attributes": attributes,
            "geo": None, "animation_file": None, "texture": vm.get("texture"),
            "variants": [{"when": "1", "texture": vm["texture"]}] if vm.get("texture") else [],
            "glowmask": None, "layers": [], "name": lang.get(f"entity.{modid}.{eid}"), "geo_variants": [], "reserved": {},
            "model_class": vm.get("model_class"), "renderer_class": rc, "type_field": r.get("field"), "vanilla_model": vm})
    return out


def MP_jload(b):
    t = re.sub(r"//[^\n]*", "", b.decode("utf-8", "replace")); t = re.sub(r",(\s*[}\]])", r"\1", t)
    return json.loads(t)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("jar"); ap.add_argument("-o", "--out", default=".")
    a = ap.parse_args()
    ents, ctrl = discover(a.jar)
    Path(a.out).mkdir(parents=True, exist_ok=True)
    Path(a.out, "entities.json").write_text(json.dumps(ents, indent=1, default=list))
    Path(a.out, "controllers.json").write_text(json.dumps(ctrl, indent=1, default=list))
    miss = [e["id"] for e in ents["entities"] if not (e["geo"] and e["texture"] and e["animation_file"])]
    print(f"{ents['mod_id']}: {ents['entity_count']} GeckoLib entities, missing assets for {len(miss)}: {miss}")
