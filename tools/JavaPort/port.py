#!/usr/bin/env python3
"""JavaPort - ONE entry point for every Java -> Bedrock conversion (Bedrock Forge).

    python3 -I tools/JavaPort/port.py <input> <out dir> [--name "Pack Name"] [--install <com.mojang dir>]

<input> may be a mod .jar, a shaderpack .zip / folder (OptiFine/Iris), a resource pack .zip, or a folder holding
several of them (each one is converted). Every part of the input is routed to our own converter:

  part of the Java input                      converter (tools/...)            Bedrock result
  ------------------------------------------  -------------------------------  ---------------------------------
  feasibility (APIs, mixins, rendering refs)  JavaModAnalyzer                  report: what each part needs
  GeckoLib/AzureLib/vanilla-model mobs,       GeckoBridge (+ ModelCore)        RP geometry/animations/controllers
    armour, held items, animated textures
  blocks, items, recipes, loot, sounds,       ModPorter (via GeckoBridge)      BP/RP content, worldgen script
    particles, dimensions, structures, fog
  the mod's Java code (procedures, hooks,     JavaBridge                       Script API JS (scripts/javabridge)
    events, commands)
  mob AI (goals, targeting, pathfinding)      MobAI (GeckoBridge + JavaBridge) behaviours + GoalRunner
  core shaders / shaderpacks                  ShaderForge + VVPackGen          renderer/materials (needs BF + RF)
  client rendering mixins (GL/Blaze3D)        Bedrock Forge / Renderer         native feature list (report)
                                                Foundation (C++ DLLs)

Output: <out>/packs/<Name> BP|RP (ready dev packs), <out>/PORT_REPORT.md, <out>/port.json (install manifest:
packs, required DLLs, shader work). See docs/CONVERT_A_MOD.md.
"""
from __future__ import annotations
import argparse, json, re, shutil, subprocess, sys, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(TOOLS / "JavaModAnalyzer"))

SHADER_PROGRAMS = {   # OptiFine/Iris program -> Bedrock RenderDragon material that ShaderForge patches
    "gbuffers_terrain": "RenderChunk", "gbuffers_water": "RenderChunk (water variant)", "gbuffers_entities": "Entity",
    "gbuffers_hand": "ItemInHandColor", "gbuffers_skybasic": "Sky", "gbuffers_skytextured": "SunMoon",
    "gbuffers_clouds": "Clouds", "gbuffers_weather": "Weather", "gbuffers_basic": "Basic",
    "composite": "ColorPostProcessing / custom RF pass", "deferred": "RF full-screen pass", "final": "ColorPostProcessing",
    "shadow": "RF shadow pass (Renderer Foundation)",
}


def kind_of(p: Path) -> str:
    """mod | shaderpack | resourcepack | unknown"""
    names = []
    if p.is_dir():
        names = [str(x.relative_to(p)).replace("\\", "/") for x in p.rglob("*") if x.is_file()]
    elif p.suffix in (".jar", ".zip"):
        try:
            with zipfile.ZipFile(p) as z: names = z.namelist()
        except zipfile.BadZipFile: return "unknown"
    else: return "unknown"
    s = set(names)
    if any(n.endswith(".class") for n in names) or "META-INF/mods.toml" in s or "fabric.mod.json" in s or "META-INF/neoforge.mods.toml" in s:
        return "mod"
    if any(re.match(r"(.*/)?shaders/[^/]+\.(fsh|vsh|glsl)$", n) for n in names) or any(n.endswith("shaders.properties") for n in names):
        return "shaderpack"
    if any(n.endswith("pack.mcmeta") for n in names) and any(n.startswith("assets/") or "/assets/" in n for n in names):
        return "resourcepack"
    return "unknown"


def entries(p: Path) -> list[str]:
    if p.is_dir(): return [str(x.relative_to(p)).replace("\\", "/") for x in p.rglob("*") if x.is_file()]
    with zipfile.ZipFile(p) as z: return z.namelist()


def port_mod(jar: Path, out: Path, name: str, rep: dict):
    import analyze as JA
    feas = JA.analyze(jar)
    rep["feasibility"] = {"loader": feas.get("loader") or feas.get("metadata", {}).get("loader"), "score": feas.get("score"),
                          "buckets": {k: v["status"] for k, v in feas.get("buckets", {}).items()}}
    native = sorted({k for k, v in feas.get("buckets", {}).items() if v["status"].startswith(("native", "hard"))})
    mix = [m for m in feas.get("mixins", []) if m.get("client")] if isinstance(feas.get("mixins"), list) else []
    # whole-mod pipeline: GeckoBridge (models) + ModPorter (content) + JavaBridge (code) + MobAI
    tmp = out / "_build"
    cmd = [sys.executable, "-I", str(TOOLS / "GeckoBridge" / "build.py"), str(jar), str(tmp), "--name", name, "--tol", "0.25"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    rep["build_log_tail"] = (r.stdout + r.stderr).strip().splitlines()[-3:]
    if r.returncode != 0:
        rep["errors"].append(f"GeckoBridge build failed (exit {r.returncode}): see build_log_tail"); return
    br = json.loads((tmp / "report.json").read_text())
    packs = out / "packs"; packs.mkdir(parents=True, exist_ok=True)
    for d in tmp.iterdir():
        if d.is_dir():
            dst = packs / d.name
            if dst.exists(): shutil.rmtree(dst)
            shutil.move(str(d), str(dst)); rep["packs"].append(d.name)
    shutil.copy(tmp / "report.json", out / f"{name}.build_report.json"); shutil.rmtree(tmp, ignore_errors=True)
    rep["converters"]["GeckoBridge"] = {"mobs": len(br.get("entities", {})), "molang_errors": br.get("molang_errors")}
    rep["converters"]["MobAI"] = {k: v for k, v in (br.get("mobai") or {}).items() if k != "no_goals"}
    rep["converters"]["JavaBridge"] = {k: (br.get("javabridge") or {}).get(k) for k in ("reachable", "hooks", "errors")}
    rep["converters"]["ModPorter"] = {"content": bool(br.get("content")), "structures": bool(br.get("structures")), "dimensions": bool(br.get("dimensions"))}
    if (br.get("mobai") or {}).get("unmapped"):
        rep["todo"].append(f"MobAI: unmapped Java goals {br['mobai']['unmapped']} -> add them to GOALS in tools/GeckoBridge/build.py")
    # shaders shipped inside the mod (core shader overrides, post chains)
    sh = [n for n in entries(jar) if re.match(r"assets/[^/]+/shaders/.+\.(fsh|vsh|json|glsl)$", n)]
    if sh:
        rep["shader_work"].append({"source": jar.name, "files": sh[:40], "count": len(sh),
                                   "how": "fog.glsl tweaks are converted automatically; other core/post shaders -> hand-port to HLSL in shaders/src/<Pack>/ and build with ShaderForge"})
    if native:
        rep["native"].append({"source": jar.name, "needs": native, "client_mixins": len(mix),
                              "how": "Bedrock Forge (src/, LeviLamina hooks) and Renderer Foundation (RF line) - add a feature module, list it in port.json. "
                                     "Entity/block renderers are already covered by GeckoBridge/vanilla.py, Forge networking packets are client sync "
                                     "(no-ops on single player): only custom GL rendering, screens and client mixins need native work"})
    rep["requires_dlls"] |= {"BedrockForgeCore.dll"} if native else set()


def port_shaderpack(src: Path, out: Path, name: str, rep: dict):
    names = entries(src)
    progs = sorted({re.sub(r"\.(fsh|vsh|gsh|csh)$", "", n.split("/")[-1]) for n in names if re.search(r"shaders/(world-?\d+/)?[^/]+\.(fsh|vsh)$", n)})
    mapping = {p: next((m for k, m in SHADER_PROGRAMS.items() if p == k or p.startswith(k)), "no direct material (RF pass)") for p in progs}
    scaffold = out / "shaders_src" / name; scaffold.mkdir(parents=True, exist_ok=True)
    (scaffold / "PORTING.md").write_text(f"# {name}: ShaderForge port worksheet (generated by JavaPort)\n\n"
        "GLSL (OptiFine/Iris) is not translated automatically: port each program by hand to HLSL in this folder\n"
        "(`<Material>/<Pass>.Fragment.hlsl`, semantic names: shaders/src/README.md), then:\n\n"
        f"    python3 tools/ShaderForge/build.py <game materials dir> shaders/src/{name} -o <out>/{name}\n"
        f"    python3 tools/VVPackGen/build.py <instance> <development_resource_packs> {name}   # VV look\n\n"
        "| OptiFine program | Bedrock material / pass |\n|---|---|\n" + "".join(f"| {p} | {m} |\n" for p, m in mapping.items()))
    rep["shader_work"].append({"source": src.name, "programs": mapping, "scaffold": str(scaffold.relative_to(out)),
                               "how": "hand-port GLSL->HLSL, ShaderForge build, VVPackGen profile; needs BedrockForgeCore + RendererFoundation"})
    rep["requires_dlls"] |= {"BedrockForgeCore.dll", "RendererFoundation.dll"}


def port_resourcepack(src: Path, out: Path, name: str, rep: dict):
    names = entries(src)
    cats = {}
    for n in names:
        m = re.match(r"(?:.*/)?assets/([^/]+)/(textures|sounds|models|lang|shaders|font|particles)/", n)
        if m: cats[f"{m[1]}:{m[2]}"] = cats.get(f"{m[1]}:{m[2]}", 0) + 1
    rep["todo"].append(f"{src.name}: Java resource pack ({cats}). Vanilla retextures/sounds are converted for mods by "
                       "ModPorter content.vanilla_overrides; a stand-alone resource-pack mode is not built yet.")


def pretty(stem: str) -> str:
    """the_wonderland-3.0.3-1.20.1 -> The Wonderland, ComplementaryReimagined_r5.2.2 -> ComplementaryReimagined"""
    n = re.split(r"[-_ ][vr]?\d", stem, maxsplit=1)[0].replace("_", " ").strip() or stem
    return n.title() if n == n.lower() else n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input", type=Path); ap.add_argument("out", type=Path); ap.add_argument("--name")
    ap.add_argument("--install", type=Path, help="com.mojang folder: copy the packs into development_*_packs")
    a = ap.parse_args(argv)
    a.out.mkdir(parents=True, exist_ok=True)
    inputs = [a.input] if not a.input.is_dir() or kind_of(a.input) in ("shaderpack", "resourcepack") else \
             [p for p in sorted(a.input.iterdir()) if kind_of(p) != "unknown"]
    rep = {"inputs": [], "packs": [], "converters": {}, "shader_work": [], "native": [], "todo": [], "errors": [],
           "requires_dlls": set()}
    for p in inputs:
        k = kind_of(p); name = a.name if (a.name and len(inputs) == 1) else pretty(p.stem)
        rep["inputs"].append({"file": p.name, "kind": k, "name": name})
        if k == "mod": port_mod(p, a.out, name, rep)
        elif k == "shaderpack": port_shaderpack(p, a.out, name, rep)
        elif k == "resourcepack": port_resourcepack(p, a.out, name, rep)
        else: rep["errors"].append(f"{p.name}: not a mod jar, shaderpack or resource pack")
    rep["requires_dlls"] = sorted(rep["requires_dlls"])
    (a.out / "port.json").write_text(json.dumps(rep, indent=2, default=str))
    md = ["# JavaPort report", ""]
    for i in rep["inputs"]: md.append(f"- **{i['file']}** -> {i['kind']} (`{i['name']}`)")
    md += ["", "## Packs", *[f"- packs/{p}" for p in rep["packs"]], "", "## Converters"]
    for k, v in rep["converters"].items(): md.append(f"- **{k}**: {json.dumps(v, default=str)}")
    if rep["shader_work"]: md += ["", "## Shader work (ShaderForge / VVPackGen / Renderer Foundation)"] + [f"- {s['source']}: {s['how']}" for s in rep["shader_work"]]
    if rep["native"]: md += ["", "## Needs native Bedrock Forge / Renderer Foundation features"] + [f"- {n['source']}: {', '.join(n['needs'])}" for n in rep["native"]]
    if rep["requires_dlls"]: md += ["", "Required DLLs: " + ", ".join(rep["requires_dlls"])]
    if rep["todo"]: md += ["", "## To do"] + [f"- {t}" for t in rep["todo"]]
    if rep["errors"]: md += ["", "## Errors"] + [f"- {e}" for e in rep["errors"]]
    (a.out / "PORT_REPORT.md").write_text("\n".join(md) + "\n")
    if a.install and rep["packs"]:
        for p in rep["packs"]:
            dst = a.install / ("development_behavior_packs" if p.endswith(" BP") else "development_resource_packs") / p
            if dst.exists(): shutil.rmtree(dst)
            shutil.copytree(a.out / "packs" / p, dst)
        print(f"installed {len(rep['packs'])} packs into {a.install}")
    print("\n".join(md))
    return 1 if rep["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
