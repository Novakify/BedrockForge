#!/usr/bin/env python3
"""IrisBridge translator (prototype) - Iris/OptiFine GLSL program -> HLSL SM6 -> DXIL.

  python translate.py <pack>/shaders <world0|world-1|world1> <program> [-o outdir] [-D NAME=VAL ...]

Pipeline (all open tools): our include resolver + Iris-style compat patcher -> glslang (GLSL 460 -> SPIR-V, OpenGL
semantics) -> spirv-opt -> SPIRV-Cross (HLSL SM6) -> dxc (DXIL). Writes <program>.<stage>.{glsl,spv,hlsl,dxil,log}.
Our own code (BedrockForge); pack sources are only read, never redistributed.
"""
from __future__ import annotations
import argparse, re, subprocess, sys
from pathlib import Path

DXC = "/opt/dxc/bin/dxc"
STAGES = {"vsh": ("vert", "vs_6_0"), "fsh": ("frag", "ps_6_0"), "gsh": ("geom", "gs_6_0"), "csh": ("comp", "cs_6_0")}
RENDER_STAGES = ["NONE", "SKY", "SUNSET", "CUSTOM_SKY", "SUN", "MOON", "STARS", "VOID", "TERRAIN_SOLID",
                 "TERRAIN_CUTOUT_MIPPED", "TERRAIN_CUTOUT", "ENTITIES", "BLOCK_ENTITIES", "DESTROY", "OUTLINE", "DEBUG",
                 "HAND_SOLID", "TERRAIN_TRANSLUCENT", "TRIPWIRE", "PARTICLES", "CLOUDS", "RAIN_SNOW", "WORLD_BORDER",
                 "HAND_TRANSLUCENT"]  # Iris renderStage values
# Iris core-profile uniforms a program may use without declaring
IMPLICIT_UNIFORMS = {"modelViewMatrix": "mat4", "projectionMatrix": "mat4", "normalMatrix": "mat3",
                     "textureMatrix": "mat4", "chunkOffset": "vec3", "modelViewMatrixInverse": "mat4",
                     "projectionMatrixInverse": "mat4"}
BASE_DEFINES = {"MC_VERSION": "12101", "MC_GL_VERSION": "460", "MC_GLSL_VERSION": "460", "IS_IRIS": "",
                "IRIS_VERSION": "10800", "IS_BEDROCK_FORGE": "", "MC_RENDER_QUALITY": "1.0", "MC_SHADOW_QUALITY": "1.0",
                "MC_TEXTURE_FORMAT_LAB_PBR": "", "MC_NORMAL_MAP": "", "MC_SPECULAR_MAP": "",
                "IRIS_FEATURE_SEPARATE_HARDWARE_SAMPLERS": "", "IRIS_FEATURE_CUSTOM_IMAGES": "", "IRIS_FEATURE_COMPUTE": ""}


# Fixed texture slots: t<N>/s<N> are the same for every program of every pack, so RF binds one descriptor table.
SLOTS = {**{f"colortex{i}": i for i in range(16)}, "gcolor": 0, "gdepth": 1, "gnormal": 2, "composite": 3,
         "gaux1": 4, "gaux2": 5, "gaux3": 6, "gaux4": 7, "depthtex0": 16, "depthtex1": 17, "depthtex2": 18,
         "shadowtex0": 19, "shadowtex1": 20, "shadowcolor0": 21, "shadowcolor1": 22, "shadow": 19, "watershadow": 19,
         "noisetex": 23, "gtexture": 24, "tex": 24, "texture": 24, "normals": 25, "specular": 26, "lightmap": 27,
         "shadowtex0HW": 28, "shadowtex1HW": 29}
CUSTOM_SLOT0 = 32   # pack custom textures / samplers of image.* from here


def resolve(path: Path, root: Path, seen: list[str], depth=0, overrides: dict | None = None) -> str:
    if depth > 40: raise RuntimeError("include depth")
    out = []
    for i, line in enumerate(path.read_text(errors="replace").splitlines()):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
        if m:
            inc = m[1]
            p = (root / inc.lstrip("/")) if inc.startswith("/") else (path.parent / inc)
            seen.append(str(p.relative_to(root)) if p.is_relative_to(root) else str(p))
            out.append(resolve(p.resolve(), root, seen, depth + 1, overrides))
        elif overrides:
            # options are set by rewriting their own #define line (like Iris / OptiFine)
            m = re.match(r"(\s*)(//)?\s*#define\s+(\w+)(\s+[^/\s][^/]*?)?(\s*//.*)?$", line)
            if m and m[3] in overrides:
                v = overrides[m[3]]
                if m[4] is None:   # toggle
                    line = f"{m[1]}{'' if v == 'true' else '//'}#define {m[3]}{m[5] or ''}"
                else:
                    line = f"{m[1]}#define {m[3]} {v}{m[5] or ''}"
            else:
                mc = re.match(r"(\s*const\s+(?:int|float|bool)\s+(\w+)\s*=\s*)([^;]+)(;.*)$", line)
                if mc and mc[2] in overrides: line = f"{mc[1]}{overrides[mc[2]]}{mc[4]}"
            out.append(line)
        else:
            out.append(line)
    return "\n".join(out)


def patch(src: str, stage: str) -> tuple[str, list[str]]:
    """Iris-style compatibility transform: legacy GLSL 1.x built-ins -> explicit GLSL 460 declarations."""
    notes = []
    src = re.sub(r"^\s*#\s*version[^\n]*", "", src, flags=re.M)
    src = re.sub(r"^\s*#\s*extension[^\n]*", "", src, flags=re.M)
    head = []
    # texture functions
    for old, new in [("texture2DLod", "textureLod"), ("texture2DGradARB", "textureGrad"), ("texture2DGrad", "textureGrad"),
                     ("texture3DLod", "textureLod"), ("texture2D", "texture"), ("texture3D", "texture"), ("textureCube", "texture")]:
        src = re.sub(rf"\b{old}\s*\(", f"{new}(", src)
    if re.search(r"\bshadow2D(Lod)?\s*\(", src):
        head.append("vec4 bf_shadow2D(sampler2DShadow s, vec3 c) { return vec4(texture(s, c)); }")
        head.append("vec4 bf_shadow2DLod(sampler2DShadow s, vec3 c, float l) { return vec4(textureLod(s, c, l)); }")
        src = re.sub(r"\bshadow2D(Lod)?\s*\(", lambda m: f"bf_shadow2D{m[1] or ''}(", src)
    # legacy matrices / attributes -> Iris-named uniforms & inputs
    rep = {"gl_ModelViewMatrix": "iris_ModelViewMat", "gl_ProjectionMatrix": "iris_ProjMat",
           "gl_NormalMatrix": "iris_NormalMat", "gl_ModelViewProjectionMatrix": "(iris_ProjMat * iris_ModelViewMat)"}
    for k, v in rep.items():
        if re.search(rf"\b{k}\b", src): src = re.sub(rf"\b{k}\b", v, src)
    head += ["uniform mat4 iris_ModelViewMat;", "uniform mat4 iris_ProjMat;", "uniform mat3 iris_NormalMat;"]
    src = re.sub(r"\bgl_TextureMatrix\s*\[\s*(\d)\s*\]", r"iris_TextureMat\1", src)
    head += ["uniform mat4 iris_TextureMat0;", "uniform mat4 iris_TextureMat1;"]
    if stage == "vert":
        src = re.sub(r"\bftransform\s*\(\s*\)", "(iris_ProjMat * iris_ModelViewMat * gl_Vertex)", src)
        ins = {"gl_Vertex": "vec4 iris_Position", "gl_Color": "vec4 iris_Color", "gl_Normal": "vec3 iris_Normal",
               "gl_MultiTexCoord0": "vec4 iris_UV0", "gl_MultiTexCoord1": "vec4 iris_UV1", "gl_MultiTexCoord2": "vec4 iris_UV2"}
        for loc, (k, v) in enumerate(ins.items()):   # fixed: Position 16, Color 17, Normal 18, UV0 19, UV1 20, UV2 21 (RF knows them)
            if re.search(rf"\b{k}\b", src):
                head.append(f"layout(location={16 + loc}) in {v};")  # the pack's own attributes auto-map from 0
                src = re.sub(rf"\b{k}\b", v.split()[1], src)
        for i, (name, ty) in enumerate({"vaPosition": "vec3", "vaColor": "vec4", "vaUV0": "vec2", "vaUV1": "ivec2",
                                         "vaUV2": "ivec2", "vaNormal": "vec3"}.items()):  # Iris core-profile attributes
            if re.search(rf"\b{name}\b", src) and not re.search(rf"\bin\s+\w+\s+{name}\b", src):
                head.append(f"layout(location={24 + i}) in {ty} {name};")
        src = re.sub(r"\battribute\b", "in", src)
        src = re.sub(r"\bvarying\b", "out", src)
    else:
        src = re.sub(r"\bvarying\b", "in", src)
        src = re.sub(r"\bgl_Color\b", "vec4(1.0)", src)
        # gl_FragData[n] / gl_FragColor -> explicit outputs (DRAWBUFFERS / RENDERTARGETS order)
        idx = sorted({int(x) for x in re.findall(r"\bgl_FragData\s*\[\s*(\d+)\s*\]", src)})
        if re.search(r"\bgl_FragColor\b", src): idx = sorted(set(idx) | {0}); src = re.sub(r"\bgl_FragColor\b", "gl_FragData[0]", src)
        for n in idx: head.append(f"layout(location={n}) out vec4 iris_FragData{n};")
        src = re.sub(r"\bgl_FragData\s*\[\s*(\d+)\s*\]", r"iris_FragData\1", src)
        if idx: notes.append(f"outputs: {idx}")
    custom = [CUSTOM_SLOT0]
    def bind(m):
        name = m[3]
        slot = SLOTS.get(name)
        if slot is None: slot = custom[0]; custom[0] += 1
        return f"{m[1]}layout(binding={slot}) uniform {m[2]} {name};"
    src = re.sub(r"^(\s*)uniform\s+((?:i|u)?sampler\w+)\s+(\w+)\s*;", bind, src, flags=re.M)
    # storage buffers (bufferObject.N) -> slot 48+N; images -> 64+ (UAV registers, never overlapping textures)
    src = re.sub(r"layout\s*\(([^)]*?)\bbinding\s*=\s*(\d+)([^)]*)\)(\s*(?:(?:readonly|writeonly|coherent|restrict|volatile)\s+)*buffer\b)",
                 lambda m: f"layout({m[1]}binding = {48 + int(m[2])}{m[3]}){m[4]}", src)
    img = [64]
    def bind_img(m):
        k = img[0]; img[0] += 1
        q = m[2].strip()
        return f"{m[1]}layout({q + ', ' if q else ''}binding = {k}) uniform {m[3]} {m[4]};"
    src = re.sub(r"^(\s*)(?:uniform\s+)?layout\s*\(([^)]*)\)\s*(?:uniform\s+)?((?:i|u)?image\w+)\s+(\w+)\s*;", bind_img, src, flags=re.M)
    # integer textures can't be filtered: texture()/textureLod() on them == texelFetch at the texel (HLSL can't Sample them)
    ints = re.findall(r"uniform\s+([iu]sampler(?:2D|3D))\s+(\w+)\s*;", src)
    if ints:
        for t in {t for t, _ in ints}:
            dim = t[-2:].lower(); v = "vec2" if dim == "2d" else "vec3"; iv = "i" + v
            r = "uvec4" if t.startswith("u") else "ivec4"
            head.append(f"{r} bf_texi({t} s, {v} c) {{ return texelFetch(s, {iv}(c * {v}(textureSize(s, 0))), 0); }}")
            head.append(f"{r} bf_texi({t} s, {v} c, float l) {{ return texelFetch(s, {iv}(c * {v}(textureSize(s, 0))), 0); }}")
        for _, name in ints:
            src = re.sub(rf"\btexture(?:Lod)?\(\s*{name}\s*,", f"bf_texi({name},", src)
    if re.search(r"\bgl_Fog\b", src):
        head.append("struct bf_Fog { vec4 color; float density, start, end, scale; }; uniform bf_Fog iris_Fog;")
        src = re.sub(r"\bgl_Fog\b", "iris_Fog", src)
    for name, ty in IMPLICIT_UNIFORMS.items():
        if re.search(rf"\b{name}\b", src) and not re.search(rf"\b(uniform|in)\s+\w+\s+{name}\b", src):
            head.append(f"uniform {ty} {name};"); notes.append(f"implicit uniform {name}")
    head = [f"#define MC_RENDER_STAGE_{n} {i}" for i, n in enumerate(RENDER_STAGES)] + head
    return "\n".join(head) + "\n" + src, notes


def fix_hlsl(text: str) -> tuple[str, int]:
    """Work around SPIRV-Cross mis-flattening an array-of-vec4 store into a storage buffer:
         for (A < N) { for (B < M) { buf.Store4(B * 16 + A * 16 + base, asuint(arr[A][B])); } }
       -> for (A < N) { buf.Store4(A * 16 + base, asuint(arr[A])); }"""
    rx = re.compile(r"(\[unroll\]\s*)?for \(int (\w+) = 0; \2 < (\d+); \2\+\+\)\s*\{\s*"
                    r"(?:\[unroll\]\s*)?for \(int (\w+) = 0; \4 < \d+; \4\+\+\)\s*\{\s*"
                    r"(\w+)\.Store4\(\4 \* 16 \+ \2 \* 16 \+ ([^;]*?), asuint\((\w+)\[\2\]\[\4\]\)\);\s*\}\s*\}")
    out, n = rx.subn(lambda m: f"{m[1] or ''}for (int {m[2]} = 0; {m[2]} < {m[3]}; {m[2]}++)\n{{\n"
                               f"    {m[5]}.Store4({m[2]} * 16 + {m[6]}, asuint({m[7]}[{m[2]}]));\n}}", text)
    return out, n


def run(cmd, log):
    r = subprocess.run(cmd, capture_output=True, text=True)
    log.append("$ " + " ".join(map(str, cmd)) + "\n" + r.stdout + r.stderr)
    return r.returncode == 0


def translate(shaders: Path, world: str, prog: str, out: Path, defines: dict, overrides: dict | None = None,
              src_file: Path | None = None) -> dict:
    res = {}
    for ext, (stage, prof) in STAGES.items():
        f = (src_file or shaders / world / prog).with_suffix(f".{ext}")
        if not f.exists(): continue
        log, seen, defaults = [], [], {}
        raw = resolve(f, shaders, seen, 0, overrides)
        src, notes = patch(raw, stage)
        (out / f"{prog}.{stage}.src").write_text(raw)
        dfs = {**BASE_DEFINES, **defines}
        hdr = "#version 460 core\n#extension GL_GOOGLE_include_directive : enable\n" + \
              "".join(f"#define {k} {v}\n" for k, v in dfs.items())
        glsl = out / f"{prog}.{stage}.glsl"; glsl.write_text(hdr + src)
        spv, opt, hlsl, dxil = (out / f"{prog}.{stage}.{e}" for e in ("spv", "opt.spv", "hlsl", "dxil"))
        ok = run(["glslangValidator", "-G", "--auto-map-locations", "--auto-map-bindings", "-S", stage, "-o", spv, glsl], log)
        ok = ok and run(["spirv-opt", "-O", spv, "-o", opt], log)
        ok = ok and run(["spirv-cross", "--hlsl", "--shader-model", "60", "--hlsl-enable-compat", opt, "--output", hlsl], log)
        if ok:
            fixed, nfix = fix_hlsl(hlsl.read_text())
            if nfix: hlsl.write_text(fixed); notes.append(f"spirv-cross store fix x{nfix}")
            cmp = sorted({int(x) for x in re.findall(r"SamplerComparisonState\s+\w+\s*:\s*register\(s(\d+)", fixed)})
            defaults = dict(re.findall(r"^uniform\s+\w+\s+(\w+)\s*=\s*([^;]+);", fixed, re.M))
        ok = ok and run([DXC, "-T", prof, "-E", "main", "-Fo", dxil, hlsl], log)
        stepfail = None if ok else log[-1].splitlines()[0][2:].split()[0]
        (out / f"{prog}.{stage}.log").write_text("\n".join(log))
        res[f"{prog}.{ext}"] = {"defaults": defaults, "cmp": locals().get("cmp", []) if ok else [], "ok": ok, "failed_at": stepfail, "includes": len(seen), "notes": notes,
                                "error": None if ok else next((l for l in log[-1].splitlines() if "ERROR" in l or "error" in l), log[-1][-300:])}
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("shaders"); ap.add_argument("world"); ap.add_argument("program")
    ap.add_argument("-o", default="out"); ap.add_argument("-D", action="append", default=[])
    a = ap.parse_args()
    out = Path(a.o); out.mkdir(parents=True, exist_ok=True)
    d = dict((x.split("=", 1) + [""])[:2] for x in a.D)
    for k, v in translate(Path(a.shaders), a.world, a.program, out, d).items():
        print(k, "OK" if v["ok"] else f"FAIL @ {v['failed_at']}: {v['error']}")
