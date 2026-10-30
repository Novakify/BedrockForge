#!/usr/bin/env python3
"""GeckoBridge converter (M0 prototype): GeckoLib entity assets in a mod jar -> Bedrock RP + BP.

    python -I tools/GeckoBridge/convert.py spec.json OUT_DIR

spec.json:
{
  "jar": "path/to/mod.jar", "namespace": "sculkhorde", "pack_name": "GeckoBridge Proof",
  "glow": "overlay" | "alpha",
  "entities": [ { "id": "sculk_enderman",
                  "geo": "assets/.../geo/entity/sculk_enderman.geo.json",
                  "texture": "assets/.../textures/entity/sculk_enderman.png",
                  "animations": "assets/.../animations/entity/sculk_enderman.animation.json",
                  "play": ["idle", "idle.tendrils"],          # always-on, concurrent (GeckoLib: 1 per controller)
                  "scale": 1.0, "collision": [0.6, 2.9] } ]
}

Writes OUT_DIR/<pack>RP and OUT_DIR/<pack>BP plus OUT_DIR/report.json. The packs are built from the mod's own
assets: they inherit the mod's licence and must not be committed or shared without the author's OK.
Everything here is offline. Runtime pieces (controllers from Java code, triggered animations, bone code)
are M1+ (docs/PORT_GECKOBRIDGE.md).
"""
import io, json, sys, uuid, zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import gbanim  # noqa: E402

try:
    from PIL import Image
except ImportError:  # glow "alpha" mode needs Pillow; overlay mode doesn't
    Image = None

GB_NS = "gb"
# GeckoLib's glow pass (AutoGlowingGeoLayer; GL5 EmissiveRenderType / GL4 AutoGlowingTexture): a second pass of the
# same model with the _glowmask texture, TRANSLUCENT blend, full-sky light, no shading, no culling, no depth write.
# Bedrock match: 2nd render controller, ignore_lighting, alpha-blended material with culling off.
GLOW_MATERIAL = {"materials": {"version": "1.0.0", "gb_glow:entity_alphablend": {"+states": ["DisableCulling"]}}}


def ident_of(ns, eid): return f"{GB_NS}:{eid}"
def geo_id(ns, eid): return f"geometry.{GB_NS}.{ns}.{eid}"
def anim_id(ns, eid, name): return f"animation.{GB_NS}.{ns}.{eid}.{name.replace('animation.', '').replace(' ', '_')}"
def short(name): return name.replace("animation.", "").replace(".", "_").replace(" ", "_")


def manifest(name, desc, kind, dep=None, seed=""):
    u = lambda s: str(uuid.uuid5(uuid.NAMESPACE_URL, f"geckobridge/{name}/{kind}/{s}{seed}"))
    m = {"format_version": 2,
         "header": {"name": name, "description": desc, "uuid": u("h"), "version": [0, 1, 0], "min_engine_version": [1, 21, 0]},
         "modules": [{"type": "resources" if kind == "rp" else "data", "uuid": u("m"), "version": [0, 1, 0]}]}
    if dep: m["dependencies"] = [{"uuid": dep, "version": [0, 1, 0]}]
    return m


def convert_geometry(geo, ns, eid, report):
    """GeckoLib geometry is Bedrock geometry. Only the identifier must change: mods reuse vanilla names
    (Sculk Horde's enderman is "geometry.enderman.v1.8"), which would replace/collide with vanilla models."""
    g = json.loads(json.dumps(geo))
    geoms = g.get("minecraft:geometry", [])
    if len(geoms) != 1: report.setdefault("warnings", []).append(f"{eid}: {len(geoms)} geometries in file, using first")
    first = geoms[0]
    report.setdefault("geometry_renamed", {})[eid] = [first["description"]["identifier"], geo_id(ns, eid)]
    first["description"]["identifier"] = geo_id(ns, eid)
    fv = g.get("format_version", "1.12.0")
    if fv not in ("1.12.0", "1.16.0", "1.21.0"):
        report.setdefault("warnings", []).append(f"{eid}: geometry format {fv} -> written as 1.12.0"); fv = "1.12.0"
    return {"format_version": fv, "minecraft:geometry": [first]}


def glow_alpha(base_png, glow_png):
    """Bake the glowmask into the base texture for material entity_emissive_alpha (alpha < 1 = emissive)."""
    base = Image.open(io.BytesIO(base_png)).convert("RGBA"); glow = Image.open(io.BytesIO(glow_png)).convert("RGBA")
    if glow.size != base.size: glow = glow.resize(base.size, Image.NEAREST)
    out = base.copy(); bp, gp, op = base.load(), glow.load(), out.load()
    for y in range(base.size[1]):
        for x in range(base.size[0]):
            r, g, b, a = gp[x, y]
            if a:
                R, G, B, A = bp[x, y]; k = a / 255
                op[x, y] = (round(R * (1 - k) + r * k), round(G * (1 - k) + g * k), round(B * (1 - k) + b * k), max(1, 255 - a))
    buf = io.BytesIO(); out.save(buf, "PNG"); return buf.getvalue()


def build(spec, out):
    z = zipfile.ZipFile(spec["jar"]); ns = spec["namespace"]; pack = spec.get("pack_name", "GeckoBridge")
    rp = out / f"{pack.replace(' ', '')}RP"; bp = out / f"{pack.replace(' ', '')}BP"
    report = {"source_jar": Path(spec["jar"]).name, "entities": {}}
    rpm = manifest(pack + " RP", "GeckoBridge conversion (private test). Assets: see CREDITS.txt", "rp")
    bpm = manifest(pack + " BP", "GeckoBridge conversion (private test)", "bp", dep=rpm["header"]["uuid"])
    for d in (rp, bp): d.mkdir(parents=True, exist_ok=True)
    (rp / "manifest.json").write_text(json.dumps(rpm, indent=1)); (bp / "manifest.json").write_text(json.dumps(bpm, indent=1))
    lang = []
    rcs = {}
    for e in spec["entities"]:
        eid = e["id"]; er = report["entities"].setdefault(eid, {})
        # geometry
        geo = convert_geometry(json.loads(z.read(e["geo"]).decode("utf-8-sig")), ns, eid, er)
        (rp / "models/entity").mkdir(parents=True, exist_ok=True)
        (rp / f"models/entity/{eid}.geo.json").write_text(json.dumps(geo))
        # textures
        tex_key = f"textures/entity/{GB_NS}/{ns}/{eid}"
        base_png = z.read(e["texture"])
        glow_name = e.get("glowmask") or e["texture"].replace(".png", "_glowmask.png")  # RenderUtil.getEmissiveResource
        has_glow = glow_name in z.namelist()
        mode = e.get("glow", spec.get("glow", "overlay"))
        (rp / f"textures/entity/{GB_NS}/{ns}").mkdir(parents=True, exist_ok=True)
        if has_glow and mode == "alpha" and Image:
            (rp / f"{tex_key}.png").write_bytes(glow_alpha(base_png, z.read(glow_name)))
        else:
            (rp / f"{tex_key}.png").write_bytes(base_png)
            if has_glow: (rp / f"{tex_key}_glowmask.png").write_bytes(z.read(glow_name))
        er["glowmask"] = (mode if has_glow else "none")
        # animations
        src = json.loads(z.read(e["animations"]).decode("utf-8-sig"))["animations"]
        anims_out, client_anims, errs = {}, {}, {}
        for name, a in src.items():
            nid = anim_id(ns, eid, name)
            conv = gbanim.convert_animation(name, a, nid, report=er)
            anims_out.update(conv)
            client_anims[short(name)] = nid
            errs[name] = gbanim.measure_error(a, conv[nid])[0]
        er["bake_error_max"] = {k: max(v[k] for v in errs.values()) for k in ("rotation", "position", "scale")} if errs else {}
        er["animations"] = len(anims_out)
        for k in ("molang_mapped",):
            if k in er: er[k] = sorted(er[k])
        (rp / "animations").mkdir(parents=True, exist_ok=True)
        (rp / f"animations/{eid}.animation.json").write_text(json.dumps({"format_version": "1.8.0", "animations": anims_out}))
        play = [short(p) for p in e.get("play", [])]
        missing = [p for p in play if p not in client_anims]
        if missing: raise SystemExit(f"{eid}: play list names unknown animations {missing}")
        # client entity
        desc = {"identifier": ident_of(ns, eid),
                "materials": {"default": "entity_emissive_alpha" if (has_glow and mode == "alpha") else "entity_alphatest"},
                "textures": {"default": tex_key}, "geometry": {"default": geo_id(ns, eid)},
                "animations": client_anims, "scripts": {"animate": play},
                "render_controllers": ["controller.render.default"],
                "spawn_egg": {"base_color": "#0d1a2e", "overlay_color": "#29dfeb"}}
        if e.get("scale", 1.0) != 1.0: desc["scripts"]["scale"] = str(e["scale"])
        if has_glow and mode == "overlay":
            desc["materials"]["glow"] = "gb_glow"  # see GLOW_MATERIAL
            desc["textures"]["glow"] = tex_key + "_glowmask"
            desc["render_controllers"].append(f"controller.render.{GB_NS}.glow")
            rcs[f"controller.render.{GB_NS}.glow"] = {"geometry": "Geometry.default", "materials": [{"*": "Material.glow"}],
                                                       "textures": ["Texture.glow"], "ignore_lighting": True}
        (rp / "entity").mkdir(parents=True, exist_ok=True)
        (rp / f"entity/{eid}.entity.json").write_text(json.dumps({"format_version": "1.10.0", "minecraft:client_entity": {"description": desc}}, indent=1))
        lang += [f"entity.{ident_of(ns, eid)}.name={e.get('name', eid.replace('_', ' ').title())} (GeckoBridge)",
                 f"item.spawn_egg.entity.{ident_of(ns, eid)}.name=Spawn {e.get('name', eid.replace('_', ' ').title())} (GeckoBridge)"]
        # behaviour: a still, harmless display mob - the proof is about looks, not AI
        w, h = e.get("collision", [0.6, 1.8])
        bpe = {"format_version": "1.21.0", "minecraft:entity": {
            "description": {"identifier": ident_of(ns, eid), "is_spawnable": True, "is_summonable": True, "is_experimental": False},
            "components": {"minecraft:type_family": {"family": ["geckobridge", "mob"]},
                           "minecraft:collision_box": {"width": w, "height": h},
                           "minecraft:health": {"value": 20, "max": 20}, "minecraft:physics": {},
                           "minecraft:pushable": {"is_pushable": False, "is_pushable_by_piston": True},
                           "minecraft:persistent": {}, "minecraft:knockback_resistance": {"value": 1.0},
                           "minecraft:nameable": {}}}}
        (bp / "entities").mkdir(parents=True, exist_ok=True)
        (bp / f"entities/{eid}.json").write_text(json.dumps(bpe, indent=1))
    if rcs:
        (rp / "materials").mkdir(exist_ok=True)
        (rp / "materials/entity.material").write_text(json.dumps(GLOW_MATERIAL, indent=1))
        (rp / "render_controllers").mkdir(parents=True, exist_ok=True)
        (rp / f"render_controllers/{GB_NS}.render_controllers.json").write_text(json.dumps({"format_version": "1.8.0", "render_controllers": rcs}, indent=1))
    (rp / "texts").mkdir(exist_ok=True)
    (rp / "texts/en_US.lang").write_text("\n".join(lang) + "\n")
    (rp / "texts/languages.json").write_text('["en_US"]')
    credits = spec.get("credits", "")
    for d in (rp, bp): (d / "CREDITS.txt").write_text(credits + "\nConverted by GeckoBridge (Bedrock Forge). Private test build, do not redistribute.\n")
    report["rp_uuid"], report["bp_uuid"] = rpm["header"]["uuid"], bpm["header"]["uuid"]
    (out / "report.json").write_text(json.dumps(report, indent=1, default=sorted))
    return report


if __name__ == "__main__":
    spec = json.loads(Path(sys.argv[1]).read_text())
    print(json.dumps(build(spec, Path(sys.argv[2])), indent=1, default=sorted))
