#!/usr/bin/env python3
"""Bedrock Forge - JavaModAnalyzer v0

Reads a Fabric / Forge / NeoForge mod .jar WITHOUT running it and writes a
porting-feasibility report (Markdown + JSON).

    python analyze.py path/to/mod.jar [-o report_dir]

What it looks at:
  * loader metadata   fabric.mod.json | META-INF/mods.toml | META-INF/neoforge.mods.toml | quilt.mod.json
  * mixins            *.mixins.json configs + target classes (client vs common)
  * code references   constant-pool scan of every .class for net/minecraft, Fabric API,
                      Forge/NeoForge APIs, rendering (Blaze3D/GL), networking
  * assets            textures / sounds / models / lang that could feed AssetConverter
Then maps what it finds onto Bedrock Forge capability buckets.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import tomllib
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

# ---------------------------------------------------------------- class-file parsing
def class_refs(data: bytes) -> tuple[str, set[str]]:
    """Return (this_class, referenced internal names) from a .class constant pool."""
    if data[:4] != b"\xca\xfe\xba\xbe":
        return "", set()
    count = struct.unpack(">H", data[8:10])[0]
    i, idx = 10, 1
    utf8: dict[int, str] = {}
    classes: dict[int, int] = {}
    while idx < count:
        tag = data[i]
        if tag == 1:  # Utf8
            ln = struct.unpack(">H", data[i + 1 : i + 3])[0]
            utf8[idx] = data[i + 3 : i + 3 + ln].decode("utf-8", "replace")
            i += 3 + ln
        elif tag == 7:  # Class
            classes[idx] = struct.unpack(">H", data[i + 1 : i + 3])[0]
            i += 3
        elif tag in (8, 16, 19, 20):
            i += 3
        elif tag == 15:
            i += 4
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):
            i += 5
        elif tag in (5, 6):
            i += 9
            idx += 1
        else:
            break  # unknown tag; stop safely
        idx += 1
    access_end = i
    this_idx = struct.unpack(">H", data[access_end + 2 : access_end + 4])[0] if len(data) > access_end + 4 else 0
    this_name = utf8.get(classes.get(this_idx, 0), "")
    refs = {utf8[v] for v in classes.values() if v in utf8}
    # descriptors like Lnet/minecraft/...; also hold type refs
    for s in utf8.values():
        for m in re.finditer(r"L([a-zA-Z0-9_/$]+);", s):
            refs.add(m.group(1))
    return this_name, refs


# ---------------------------------------------------------------- capability buckets
# (prefix, bucket, bedrock_status)
BUCKETS = [
    ("com/mojang/blaze3d", "Rendering (Blaze3D/GL)", "native-only: needs RenderingAPI (not built yet)"),
    ("org/lwjgl", "Rendering (LWJGL/GL)", "native-only: needs RenderingAPI (not built yet)"),
    ("net/minecraft/client/render", "Rendering (world/entity renderers)", "native-only: RenderingAPI"),
    ("net/minecraft/client/renderer", "Rendering (world/entity renderers)", "native-only: RenderingAPI"),
    ("net/minecraft/client/gui", "UI / HUD / screens", "partial: bf::hud (v0) covers HUD text/boxes"),
    ("net/fabricmc/fabric/api/client/rendering", "Rendering (Fabric hooks)", "native-only: RenderingAPI"),
    ("net/fabricmc/fabric/api/client/keybinding", "Keybinds", "supported: bf::input::bindKey"),
    ("net/minecraft/client/option/KeyBinding", "Keybinds", "supported: bf::input::bindKey"),
    ("net/minecraft/client/KeyMapping", "Keybinds", "supported: bf::input::bindKey"),
    ("net/minecraft/client/sound", "Audio", "pack + native: AudioAPI (planned)"),
    ("net/minecraft/sounds", "Audio", "pack: resource-pack sound_definitions"),
    ("net/minecraft/sound", "Audio", "pack: resource-pack sound_definitions"),
    ("net/minecraft/client/Camera", "Camera", "native: CameraAPI (planned)"),
    ("net/minecraft/client/render/Camera", "Camera", "native: CameraAPI (planned)"),
    ("net/minecraft/world/entity/ai", "Entity AI", "pack: behavior-pack components + Script API"),
    ("net/minecraft/entity/ai", "Entity AI", "pack: behavior-pack components + Script API"),
    ("net/minecraft/world/entity", "Entities", "pack: custom entities (behavior + resource pack)"),
    ("net/minecraft/entity", "Entities", "pack: custom entities (behavior + resource pack)"),
    ("net/minecraft/world/level/block", "Blocks", "pack: custom blocks"),
    ("net/minecraft/block", "Blocks", "pack: custom blocks"),
    ("net/minecraft/world/item", "Items", "pack: custom items"),
    ("net/minecraft/item", "Items", "pack: custom items"),
    ("net/minecraft/world/level/levelgen", "World generation", "pack: features/biomes (limited)"),
    ("net/minecraft/world/gen", "World generation", "pack: features/biomes (limited)"),
    ("net/minecraft/network/chat", "Text components", "supported: plain/formatted strings"),
    ("net/minecraft/network", "Networking", "hard: Bedrock protocol differs; local worlds only"),
    ("net/fabricmc/fabric/api/networking", "Networking", "hard: Bedrock protocol differs"),
    ("net/minecraftforge/network", "Networking", "hard: Bedrock protocol differs"),
    ("net/neoforged/neoforge/network", "Networking", "hard: Bedrock protocol differs"),
    ("net/minecraft/commands", "Commands", "supported: LeviLamina command API / Script API"),
    ("net/minecraft/server/command", "Commands", "supported: LeviLamina command API / Script API"),
    ("net/minecraft/client/particle", "Particles", "pack: particle JSON + native trigger"),
    ("net/minecraft/core/particles", "Particles", "pack: particle JSON"),
    ("net/minecraft/particle", "Particles", "pack: particle JSON"),
    ("net/fabricmc/fabric/api/event", "Fabric events", "partial: map to ll::event / bf::events"),
    ("net/minecraftforge/eventbus", "Forge events", "partial: map to ll::event / bf::events"),
    ("net/minecraftforge/event", "Forge events", "partial: map to ll::event / bf::events"),
    ("net/neoforged/bus", "NeoForge events", "partial: map to ll::event / bf::events"),
    ("net/neoforged/neoforge/event", "NeoForge events", "partial: map to ll::event / bf::events"),
    ("net/minecraft/client/MinecraftClient", "Client core", "native: LeviLamina ClientInstance"),
    ("net/minecraft/client/Minecraft", "Client core", "native: LeviLamina ClientInstance"),
]
STATUS_SCORE = {"supported": 1.0, "pack": 0.7, "partial": 0.6, "native": 0.4, "pack + native": 0.5,
                "native-only": 0.2, "hard": 0.05}


def bucket_for(ref: str):
    best = None
    for prefix, bucket, status in BUCKETS:
        if ref.startswith(prefix) and (best is None or len(prefix) > len(best[0])):
            best = (prefix, bucket, status)
    return best


# ---------------------------------------------------------------- metadata
def read_metadata(z: zipfile.ZipFile) -> dict:
    names = set(z.namelist())
    meta: dict = {"loader": "unknown"}
    if "fabric.mod.json" in names:
        j = json.loads(z.read("fabric.mod.json").decode("utf-8", "replace"))
        meta.update(loader="fabric", id=j.get("id"), name=j.get("name"), version=j.get("version"),
                    environment=j.get("environment", "*"), license=j.get("license"),
                    depends=j.get("depends", {}), entrypoints=j.get("entrypoints", {}),
                    mixin_configs=[m if isinstance(m, str) else m.get("config") for m in j.get("mixins", [])],
                    access_widener=j.get("accessWidener"))
    elif "quilt.mod.json" in names:
        j = json.loads(z.read("quilt.mod.json").decode("utf-8", "replace"))
        ql = j.get("quilt_loader", {})
        meta.update(loader="quilt", id=ql.get("id"), version=ql.get("version"),
                    name=ql.get("metadata", {}).get("name"), mixin_configs=j.get("mixin", []) or [])
    else:
        for path, loader in (("META-INF/neoforge.mods.toml", "neoforge"), ("META-INF/mods.toml", "forge")):
            if path in names:
                t = tomllib.loads(z.read(path).decode("utf-8", "replace"))
                mod = (t.get("mods") or [{}])[0]
                deps = t.get("dependencies", {}).get(mod.get("modId", ""), [])
                meta.update(loader=loader, id=mod.get("modId"), name=mod.get("displayName"),
                            version=mod.get("version"), license=t.get("license"),
                            depends={d.get("modId"): d.get("versionRange") for d in deps if isinstance(d, dict)},
                            mixin_configs=[m.get("config") for m in t.get("mixins", []) if isinstance(m, dict)])
                break
    if not meta.get("mixin_configs"):
        meta["mixin_configs"] = sorted(n for n in names if n.endswith(".mixins.json") and "/" not in n)
    return meta


def read_mixins(z: zipfile.ZipFile, configs: list[str]) -> list[dict]:
    out = []
    for cfg in configs or []:
        if not cfg or cfg not in z.namelist():
            continue
        j = json.loads(z.read(cfg).decode("utf-8", "replace"))
        pkg = j.get("package", "")
        for side in ("mixins", "client", "server"):
            for cls in j.get(side, []) or []:
                out.append({"config": cfg, "side": "common" if side == "mixins" else side, "class": f"{pkg}.{cls}"})
    return out


def mixin_targets(z: zipfile.ZipFile, mixins: list[dict]) -> None:
    """Best effort: pull @Mixin target class names from each mixin's constant pool."""
    for m in mixins:
        path = m["class"].replace(".", "/") + ".class"
        if path not in z.namelist():
            continue
        _, refs = class_refs(z.read(path))
        m["targets"] = sorted(r for r in refs if r.startswith(("net/minecraft/", "com/mojang/")))[:6]


# ---------------------------------------------------------------- main analysis
def analyze(jar: Path) -> dict:
    with zipfile.ZipFile(jar) as z:
        meta = read_metadata(z)
        mixins = read_mixins(z, meta.get("mixin_configs", []))
        mixin_targets(z, mixins)
        # The mod's own package = entrypoint / mixin package, not "whatever class zips first".
        own_prefix = None
        eps = [e if isinstance(e, str) else e.get("value", "") for v in (meta.get("entrypoints") or {}).values() for e in v]
        seeds = eps + [m["class"] for m in mixins]
        if seeds:
            own_prefix = "/".join(seeds[0].replace(".", "/").split("/")[:2])
        ext_refs: Counter = Counter()
        n_classes = 0
        for n in z.namelist():
            if not n.endswith(".class") or n.startswith("META-INF/"):
                continue
            n_classes += 1
            this, refs = class_refs(z.read(n))
            for r in refs:
                ext_refs[r] += 1
        assets = Counter()
        for n in z.namelist():
            parts = n.split("/")
            if parts[0] in ("assets", "data") and len(parts) >= 4 and not n.endswith("/"):
                assets[f"{parts[0]}/{parts[1]}/{parts[2]} ({Path(n).suffix.lower() or 'no ext'})"] += 1

    buckets: dict[str, dict] = defaultdict(lambda: {"refs": 0, "status": "", "examples": set()})
    for ref, c in ext_refs.items():
        if own_prefix and ref.startswith(own_prefix):
            continue
        b = bucket_for(ref)
        if not b:
            continue
        e = buckets[b[1]]
        e["refs"] += c
        e["status"] = b[2]
        if len(e["examples"]) < 4:
            e["examples"].add(ref.replace("/", "."))

    # feasibility score: weighted by reference counts
    total = sum(v["refs"] for v in buckets.values()) or 1
    score = sum(v["refs"] * STATUS_SCORE.get(v["status"].split(":")[0], 0.3) for v in buckets.values()) / total
    mixin_penalty = min(0.3, 0.02 * len(mixins))
    score = max(0.0, score - mixin_penalty)
    verdict = ("Good candidate for semi-automated port" if score >= 0.6 else
               "Portable with native work" if score >= 0.35 else
               "Hard: needs systems Bedrock Forge doesn't have yet")

    # Datapack-driven logic (mcfunction/advancements/predicates) - Bedrock has functions too.
    datapack = {"functions": 0, "commands": Counter(), "advancements": 0, "predicates": 0, "structures": 0}
    with zipfile.ZipFile(jar) as z:
        for n in z.namelist():
            if n.endswith(".mcfunction"):
                datapack["functions"] += 1
                for line in z.read(n).decode("utf-8", "replace").splitlines():
                    line = line.strip()
                    if line and not line.startswith("#"):
                        datapack["commands"][line.split()[0]] += 1
            elif n.startswith("data/") and "/advancements/" in n: datapack["advancements"] += 1
            elif n.startswith("data/") and "/predicates/" in n: datapack["predicates"] += 1
            elif n.endswith(".nbt"): datapack["structures"] += 1
        geo = sum(1 for n in z.namelist() if n.endswith(".geo.json"))
        anim = sum(1 for n in z.namelist() if n.endswith(".animation.json"))
    if datapack["functions"] and n_classes < 20:
        # mostly datapack: score on command portability instead of Java API surface
        java_only = {"data", "item", "attribute", "advancement", "forceload", "team", "loot"}
        cmds = datapack["commands"]; tot = sum(cmds.values()) or 1
        score = 1 - 0.6 * sum(v for k, v in cmds.items() if k in java_only) / tot - 0.25  # -0.25: syntax differs everywhere
        verdict = "Datapack mod: port by translating functions to Bedrock commands/Script API"
    datapack["commands"] = dict(datapack["commands"].most_common(15))

    return {
        "jar": jar.name, "meta": meta, "classes": n_classes, "mixins": mixins,
        "datapack": datapack, "geckolib_models": geo, "geckolib_animations": anim,
        "buckets": {k: {**v, "examples": sorted(v["examples"])} for k, v in
                    sorted(buckets.items(), key=lambda kv: -kv[1]["refs"])},
        "assets": dict(assets.most_common(20)),
        "score": round(score, 2), "verdict": verdict,
    }


def to_markdown(r: dict) -> str:
    m = r["meta"]
    L = [f"# Porting feasibility: {m.get('name') or r['jar']}", "",
         f"- **Loader:** {m.get('loader')}  |  **id:** {m.get('id')}  |  **version:** {m.get('version')}",
         f"- **License:** {m.get('license') or 'not declared'}  (check before reusing any code or assets)",
         f"- **Environment:** {m.get('environment', '?')}  |  **classes:** {r['classes']}  |  **mixins:** {len(r['mixins'])}",
         f"- **Score:** {r['score']} → **{r['verdict']}**", "",
         "> Static analysis only. Labels: *Theoretical* until a port compiles and runs in game.", "",
         "## Dependencies", ""]
    for k, v in (m.get("depends") or {}).items():
        L.append(f"- `{k}` {v}")
    L += ["", "## What it touches → Bedrock Forge mapping", "", "| Area | Refs | Bedrock path | Examples |", "|---|---|---|---|"]
    for k, v in r["buckets"].items():
        L.append(f"| {k} | {v['refs']} | {v['status']} | {', '.join('`'+e.split('.')[-1]+'`' for e in v['examples'])} |")
    dp = r.get("datapack") or {}
    if dp.get("functions"):
        L += ["", "## Datapack logic", "", f"- {dp['functions']} functions, {dp['advancements']} advancements, {dp['predicates']} predicates, {dp['structures']} structures",
              "- top commands: " + ", ".join(f"`{k}` x{v}" for k, v in dp["commands"].items())]
    if r.get("geckolib_models"):
        L += ["", "## GeckoLib", "", f"- {r['geckolib_models']} .geo.json models, {r['geckolib_animations']} .animation.json files: already Bedrock format, convert near 1:1"]
    L += ["", "## Mixins (engine patches: each one needs a native hook or a redesign)", ""]
    if not r["mixins"]:
        L.append("None. That's a good sign for porting.")
    for x in r["mixins"]:
        t = ", ".join("`" + s.split("/")[-1] + "`" for s in x.get("targets", [])) or "?"
        L.append(f"- [{x['side']}] `{x['class'].split('.')[-1]}` → {t}")
    L += ["", "## Assets (candidates for AssetConverter, license permitting)", ""]
    for k, v in r["assets"].items():
        L.append(f"- {k}: {v}")
    return "\n".join(L) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("jar", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=Path("."))
    a = ap.parse_args(argv)
    r = analyze(a.jar)
    a.out.mkdir(parents=True, exist_ok=True)
    stem = a.jar.stem
    (a.out / f"{stem}.feasibility.md").write_text(to_markdown(r), encoding="utf-8")
    (a.out / f"{stem}.feasibility.json").write_text(json.dumps(r, indent=2, default=list), encoding="utf-8")
    print(f"{r['meta'].get('name') or stem}: score {r['score']} - {r['verdict']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
