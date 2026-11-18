"""JavaPort plug-in: Figura avatars -> FiguraBridge (picked up automatically by tools/JavaPort/port.py).

Input: a Figura avatar folder or zip (avatar.json + .bbmodel models + Lua scripts, as in Figura's avatars/ folder).
Output (inside port.py's OUT):
    packs/<Name> Figura RP/          Bedrock resource pack replacing the player model (bones driven by Molang vars)
    figura/<slug>/avatar/ + rig.json  avatar data the native runtime loads (Bedrock Forge DLL, module "figurabridge")
Install: copy figura/<slug> to <instance>/mods/BedrockForgeCore/config/figura/<slug>, write
         {"avatar": "<slug>"} to config/figura/active.json, enable the RP on the world (port.py --install copies the RP).
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

KIND = "figura-avatar"
HERE = Path(__file__).resolve().parent
DEFAULT_VANILLA = [os.environ.get("BEDROCK_SAMPLES_RP", ""), "/home/claude/ref/bedrock-samples/resource_pack"]


def detect(path: Path, names: list[str]) -> bool:
    return any(n.split("/")[-1] == "avatar.json" for n in names) and any(n.endswith((".bbmodel", ".lua")) for n in names)


def _avatar_root(src: Path, tmp: Path) -> Path:
    if src.is_file():
        with zipfile.ZipFile(src) as z:
            for n in z.namelist():   # zip-slip guard
                if n.startswith("/") or ".." in Path(n).parts: raise ValueError(f"unsafe path in zip: {n}")
            z.extractall(tmp / "src")
        src = tmp / "src"
    hits = sorted(src.rglob("avatar.json"), key=lambda p: len(p.parts))
    if not hits: raise FileNotFoundError("no avatar.json")
    return hits[0].parent


def _vanilla() -> Path:
    for v in DEFAULT_VANILLA:
        if v and (Path(v) / "entity" / "player.entity.json").exists(): return Path(v)
    raise FileNotFoundError("bedrock-samples resource_pack not found (set BEDROCK_SAMPLES_RP)")


def port(path: Path, out: Path, name: str, rep: dict) -> None:
    sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent / "ModelCore"))
    import compile as FC
    slug = re.sub(r"[^a-z0-9_]+", "_", name.lower()).strip("_") or "avatar"
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        try:
            root = _avatar_root(path, tmp)
            res = FC.build(root, (tmp / "compiled").resolve(), _vanilla(), "00000000-0000-0000-0000-000000000000")
        except Exception as ex:
            rep["errors"].append(f"{path.name}: FiguraBridge compile failed: {ex}"); return
        comp = tmp / "compiled"
        pack = f"{name} Figura RP"
        dst = out / "packs" / pack
        if dst.exists(): shutil.rmtree(dst)
        shutil.copytree(comp / "rp", dst)
        rep["packs"].append(pack)
        fdst = out / "figura" / slug
        if fdst.exists(): shutil.rmtree(fdst)
        shutil.copytree(comp / "avatar", fdst / "avatar")
        shutil.copy2(comp / "rig.json", fdst / "rig.json")
    rep["converters"]["FiguraBridge"] = {"avatar": slug, "summary": {k: v for k, v in (res or {}).items() if k != "out" and not isinstance(v, (list, dict))}}
    rep["requires_dlls"].add("BedrockForgeCore")
    rep["native"].append({"source": path.name, "needs": ["FiguraBridge runtime (Bedrock Forge DLL): Lua scripts, animations, action wheel"],
                          "how": "built in (module figurabridge)"})
    rep["todo"].append(f"{path.name}: copy figura/{slug} to mods/BedrockForgeCore/config/figura/{slug}, "
                       f"set config/figura/active.json to {{\"avatar\": \"{slug}\"}}, enable '{pack}' on the world")
