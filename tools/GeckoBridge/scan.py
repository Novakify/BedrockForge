#!/usr/bin/env python3
"""GeckoBridge feature scan: what GeckoLib features does a mod jar actually use?

Read-only. Opens the jar as a zip, parses every GeckoLib geometry (*.geo.json) and
animation (*.animation.json / animations/*.json) file, looks at textures (glowmasks,
animated .mcmeta) and greps the class files for GeckoLib API names (no decompiling,
no class loading). Prints a JSON report.

    python -I tools/GeckoBridge/scan.py mod.jar [more.jar ...] > report.json
"""
import json, re, sys, zipfile
from collections import Counter

GECKO_API = [
    # renderers / animatable kinds
    "GeoEntityRenderer", "GeoBlockRenderer", "GeoItemRenderer", "GeoArmorRenderer", "GeoObjectRenderer",
    "GeoReplacedEntityRenderer", "DynamicGeoEntityRenderer", "GeoEntity", "GeoBlockEntity", "GeoItem",
    "GeoReplacedEntity", "SingletonGeoAnimatable",
    # layers
    "AutoGlowingGeoLayer", "BlockAndItemGeoLayer", "ItemArmorGeoLayer", "CustomBoneTextureGeoLayer",
    "TextureLayerGeoLayer", "GeoRenderLayer",
    # controller / animation API
    "AnimationController", "triggerableAnim", "triggerAnim", "setAnimationSpeed", "setSoundKeyframeHandler",
    "setParticleKeyframeHandler", "setCustomInstructionKeyframeHandler", "thenPlay", "thenLoop", "thenWait",
    "thenPlayAndHold", "thenPlayXTimes", "DefaultAnimations", "transitionLength", "setTransitionLength",
    "overrideEasingType", "setOverrideEasingType", "stopTriggeredAnimation", "receiveTriggeredAnimations",
    # code-side bone control
    "setCustomAnimations", "adjustModelBonesForRender", "getBone", "setRotX", "setRotY", "setRotZ",
    "setHidden", "setChildrenHidden", "setPosX", "setScaleX", "GeoBone",
    # model / texture choice in code
    "getModelResource", "getTextureResource", "getAnimationResource", "DefaultedEntityGeoModel",
    "GeckoLibAnimatedTexture", "AnimatableTexture",
]
QUERY_RE = re.compile(r"\b(?:q|query|v|variable|t|temp|c|context|math)\.[a-z_][a-z0-9_]*", re.I)


def anim_files(names):
    return [n for n in names if n.endswith(".json") and "/animations/" in n and n.startswith("assets/")]


def geo_files(names):
    return [n for n in names if n.endswith(".json") and ("/geo/" in n or n.endswith(".geo.json")) and n.startswith("assets/")]


def load(z, n):
    try:
        return json.loads(z.read(n).decode("utf-8-sig"))
    except Exception as e:  # noqa: BLE001 - report, don't crash
        return {"__error__": str(e)}


def scan_geo(z, names, out):
    c = Counter(); fmt = Counter(); bones = []
    for n in names:
        d = load(z, n)
        if "__error__" in d:
            c["parse_errors"] += 1; continue
        fmt[d.get("format_version", "?")] += 1
        for g in d.get("minecraft:geometry", []):
            c["geometries"] += 1
            desc = g.get("description", {})
            if desc.get("visible_bounds_width"): c["visible_bounds"] += 1
            bl = g.get("bones", [])
            bones.append(len(bl))
            for b in bl:
                c["bones"] += 1
                for k in ("binding", "locators", "poly_mesh", "texture_meshes", "mirror", "inflate", "render_group_id",
                          "material_instance", "debug", "reset"):
                    if k in b: c[f"bone.{k}"] += 1
                if b.get("rotation") and any(b["rotation"]): c["bone.rest_rotation"] += 1
                for cube in b.get("cubes", []):
                    c["cubes"] += 1
                    uv = cube.get("uv")
                    c["cube.uv_per_face" if isinstance(uv, dict) else "cube.uv_box"] += 1
                    if isinstance(uv, dict) and any("uv_rotation" in f for f in uv.values() if isinstance(f, dict)):
                        c["cube.uv_rotation"] += 1
                    for k in ("rotation", "inflate", "mirror", "pivot"):
                        if k in cube: c[f"cube.{k}"] += 1
    return {"files": len(names), "format_versions": dict(fmt), "max_bones": max(bones or [0]), **dict(c)}


def walk_channel(chan, c, easings, queries):
    """chan = value of position/rotation/scale."""
    def vec_has_molang(v):
        return isinstance(v, list) and any(isinstance(x, str) for x in v) or isinstance(v, str)

    def note_vec(v):
        if vec_has_molang(v):
            c["kf.molang"] += 1
            for x in (v if isinstance(v, list) else [v]):
                if isinstance(x, str): queries.update(m.lower() for m in QUERY_RE.findall(x))

    def note_kf(kf):
        c["keyframes"] += 1
        if isinstance(kf, dict):
            if "vector" in kf: c["kf.gecko_vector_object"] += 1; note_vec(kf["vector"])
            if "easing" in kf: easings[kf["easing"]] += 1
            if "easingArgs" in kf: c["kf.easingArgs"] += 1
            if "lerp_mode" in kf: easings["lerp_mode:" + str(kf["lerp_mode"])] += 1
            for p in ("pre", "post"):
                if p in kf:
                    c[f"kf.{p}"] += 1; note_vec(kf[p] if not isinstance(kf[p], dict) else kf[p].get("vector"))
        else:
            note_vec(kf)
            easings["linear(default)"] += 1

    if isinstance(chan, dict) and "vector" not in chan:
        for t, kf in chan.items():
            note_kf(kf)
    else:
        c["channel.single_value"] += 1; note_kf(chan)


def scan_anims(z, names, out):
    c = Counter(); easings = Counter(); loops = Counter(); queries = Counter(); anim_names = []; fmt = Counter()
    for n in names:
        d = load(z, n)
        if "__error__" in d:
            c["parse_errors"] += 1; continue
        fmt[d.get("format_version", "?")] += 1
        if "geckolib_format_version" in d: c["file.geckolib_format_version"] += 1
        for an, a in d.get("animations", {}).items():
            c["animations"] += 1; anim_names.append(an)
            loops[str(a.get("loop", "absent(play_once)"))] += 1
            for k in ("animation_length", "anim_time_update", "blend_weight", "start_delay", "loop_delay",
                      "override_previous_animation"):
                if k in a: c[f"anim.{k}"] += 1
            for k in ("sound_effects", "particle_effects", "timeline"):
                if a.get(k): c[f"anim.{k}"] += 1; c[f"anim.{k}.keys"] += len(a[k])
            for k in ("timeline",):
                for v in (a.get(k) or {}).values():
                    for s in (v if isinstance(v, list) else [v]):
                        if isinstance(s, str): queries.update(m.lower() for m in QUERY_RE.findall(s))
            for bn, b in (a.get("bones") or {}).items():
                c["bone_tracks"] += 1
                if "relative_to" in b: c["bone.relative_to"] += 1
                for ch in ("rotation", "position", "scale"):
                    if ch in b: c[f"channel.{ch}"] += 1; walk_channel(b[ch], c, easings, queries)
    roles = Counter()
    for an in anim_names:
        s = an.lower()
        for r in ("idle", "walk", "run", "attack", "death", "die", "swim", "fly", "sleep", "spawn", "hurt", "eat", "sit"):
            if r in s: roles[r] += 1
    return {"files": len(names), "format_versions": dict(fmt), **dict(c), "loops": dict(loops),
            "easings": dict(easings.most_common()), "molang_identifiers": dict(queries.most_common()),
            "role_words": dict(roles), "sample_names": anim_names[:25]}


def scan_textures(z, names):
    pngs = [n for n in names if n.endswith(".png") and n.startswith("assets/")]
    glow = [n for n in pngs if "_glowmask" in n]
    mcmeta = [n for n in names if n.endswith(".png.mcmeta") and n.startswith("assets/") and "/textures/entity" in n]
    return {"entity_pngs": sum("/textures/entity" in n for n in pngs), "glowmasks": len(glow),
            "glowmask_samples": glow[:6], "animated_entity_textures": len(mcmeta)}


def scan_classes(z, names):
    hits = Counter(); gl = Counter()
    for n in names:
        if not n.endswith(".class") or n.startswith(("software/bernie/", "com/geckolib/")): continue
        b = z.read(n)
        if b"software/bernie/geckolib" in b: gl["geckolib4_refs"] += 1
        if b"com/geckolib/" in b: gl["geckolib5_refs"] += 1
        for api in GECKO_API:
            if api.encode() in b: hits[api] += 1
    return {**dict(gl), "api_class_hits": dict(hits.most_common())}


def scan(path):
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        meta = {}
        for m in ("META-INF/mods.toml", "META-INF/neoforge.mods.toml", "fabric.mod.json"):
            if m in names:
                txt = z.read(m).decode("utf-8", "replace")
                lic = re.search(r'license\s*=\s*"([^"]+)"', txt) or re.search(r'"license"\s*:\s*"([^"]+)"', txt)
                mid = re.search(r'modId\s*=\s*"([^"]+)"', txt) or re.search(r'"id"\s*:\s*"([^"]+)"', txt)
                meta = {"manifest": m, "mod_id": mid and mid.group(1), "license": lic and lic.group(1)}
                break
        return {"jar": path.split("/")[-1], **meta,
                "geometry": scan_geo(z, geo_files(names), None),
                "animation": scan_anims(z, anim_files(names), None),
                "textures": scan_textures(z, names),
                "code": scan_classes(z, names)}


if __name__ == "__main__":
    json.dump([scan(p) for p in sys.argv[1:]], sys.stdout, indent=1)
