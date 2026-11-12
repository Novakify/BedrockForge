#!/usr/bin/env python3
"""IrisBridge pack model: options, shaders.properties and pipeline constants of an Iris/OptiFine shaderpack.

Our own implementation of the public Iris/OptiFine shaderpack format (OptiFine doc/shaders.txt + Iris behaviour).
Nothing from the pack is copied into our outputs except compiled shaders built locally for private use.
"""
from __future__ import annotations
import re, subprocess
from dataclasses import dataclass, field
from pathlib import Path

PROGRAMS = (["shadowcomp"] + [f"shadowcomp{i}" for i in range(1, 100)] + ["prepare"] + [f"prepare{i}" for i in range(1, 100)]
            + ["shadow", "shadow_solid", "shadow_cutout"]
            + ["gbuffers_basic", "gbuffers_line", "gbuffers_textured", "gbuffers_textured_lit", "gbuffers_skybasic",
               "gbuffers_skytextured", "gbuffers_clouds", "gbuffers_terrain", "gbuffers_terrain_solid", "gbuffers_terrain_cutout_mip",
               "gbuffers_terrain_cutout", "gbuffers_damagedblock", "gbuffers_block", "gbuffers_beaconbeam", "gbuffers_item",
               "gbuffers_entities", "gbuffers_entities_glowing", "gbuffers_armor_glint", "gbuffers_spidereyes", "gbuffers_hand",
               "gbuffers_weather", "gbuffers_lightning", "gbuffers_particles", "gbuffers_water", "gbuffers_hand_water"]
            + ["deferred"] + [f"deferred{i}" for i in range(1, 100)]
            + ["composite"] + [f"composite{i}" for i in range(1, 100)] + ["final"])
# Iris fallback chain (program -> what to use when it's missing)
FALLBACK = {"gbuffers_textured": "gbuffers_basic", "gbuffers_textured_lit": "gbuffers_textured", "gbuffers_skybasic": "gbuffers_basic",
            "gbuffers_skytextured": "gbuffers_textured", "gbuffers_clouds": "gbuffers_textured", "gbuffers_terrain": "gbuffers_textured_lit",
            "gbuffers_terrain_solid": "gbuffers_terrain", "gbuffers_terrain_cutout_mip": "gbuffers_terrain",
            "gbuffers_terrain_cutout": "gbuffers_terrain", "gbuffers_damagedblock": "gbuffers_terrain", "gbuffers_block": "gbuffers_terrain",
            "gbuffers_beaconbeam": "gbuffers_textured", "gbuffers_item": "gbuffers_textured_lit", "gbuffers_entities": "gbuffers_textured_lit",
            "gbuffers_entities_glowing": "gbuffers_entities", "gbuffers_armor_glint": "gbuffers_textured",
            "gbuffers_spidereyes": "gbuffers_textured", "gbuffers_hand": "gbuffers_textured_lit", "gbuffers_weather": "gbuffers_textured_lit",
            "gbuffers_lightning": "gbuffers_entities", "gbuffers_particles": "gbuffers_textured_lit", "gbuffers_water": "gbuffers_terrain",
            "gbuffers_hand_water": "gbuffers_hand", "shadow_solid": "shadow", "shadow_cutout": "shadow"}
FORMATS = {"R8", "RG8", "RGB8", "RGBA8", "R8_SNORM", "RG8_SNORM", "RGB8_SNORM", "RGBA8_SNORM", "R16", "RG16", "RGB16", "RGBA16",
           "R16_SNORM", "RG16_SNORM", "RGB16_SNORM", "RGBA16_SNORM", "R16F", "RG16F", "RGB16F", "RGBA16F", "R32F", "RG32F", "RGB32F",
           "RGBA32F", "R11F_G11F_B10F", "RGB10_A2", "RGB5_A1", "R3_G3_B2", "RGB9_E5", "R8I", "R8UI", "R16I", "R16UI", "R32I", "R32UI",
           "RG8I", "RG8UI", "RGBA8I", "RGBA8UI", "RGBA16UI", "RGBA32UI"}
# legacy names
ALIASES = {"gcolor": "colortex0", "gdepth": "colortex1", "gnormal": "colortex2", "composite": "colortex3", "gaux1": "colortex4",
           "gaux2": "colortex5", "gaux3": "colortex6", "gaux4": "colortex7"}


@dataclass
class Option:
    name: str
    default: str
    values: list[str]
    kind: str            # define | toggle | const
    comment: str = ""


@dataclass
class Pack:
    root: Path
    options: dict[str, Option] = field(default_factory=dict)
    props: dict[str, str] = field(default_factory=dict)           # resolved key -> value (after #if)
    profiles: dict[str, dict[str, str]] = field(default_factory=dict)
    uniforms: list[tuple[str, str, str, bool]] = field(default_factory=list)  # (type, name, expr, is_variable)

    # ---- options -------------------------------------------------------------------------------------------------
    def scan_options(self):
        rx_def = re.compile(r"^\s*(//)?\s*#define\s+(\w+)(?:\s+([^/\s][^/]*?))?\s*(?://\s*(.*))?$")
        rx_const = re.compile(r"^\s*const\s+(?:int|float|bool)\s+(\w+)\s*=\s*([^;]+);\s*//\s*\[([^\]]*)\]")
        for f in sorted(self.root.rglob("*")):
            if f.suffix not in (".glsl", ".fsh", ".vsh", ".gsh", ".csh", ".inc"): continue
            for line in f.read_text(errors="replace").splitlines():
                m = rx_def.match(line)
                if m:
                    commented, name, val, tail = m[1], m[2], (m[3] or "").strip(), m[4] or ""
                    vals = re.search(r"\[([^\]]*)\]", tail)
                    if val and vals and name not in self.options:
                        self.options[name] = Option(name, val, vals[1].split(), "define", tail)
                    elif not val and name not in self.options and (commented or "[" not in tail) and name.isupper():
                        # boolean toggle: "#define X" (on) / "//#define X" (off); only well-formed option names
                        if commented and not re.match(r"^\s*//\s*#define\s+\w+\s*(//.*)?$", line): continue
                        self.options[name] = Option(name, "false" if commented else "true", ["true", "false"], "toggle", tail)
                    continue
                m = rx_const.match(line)
                if m and m[1] not in self.options:
                    self.options[m[1]] = Option(m[1], m[2].strip(), m[3].split(), "const")
        return self

    def option_defines(self, overrides: dict[str, str] | None = None) -> dict[str, str]:
        d = {}
        for o in self.options.values():
            v = (overrides or {}).get(o.name, o.default)
            if o.kind == "toggle":
                if v == "true": d[o.name] = ""
            elif o.kind == "define":
                d[o.name] = v
        return d

    # ---- shaders.properties --------------------------------------------------------------------------------------
    def load_properties(self, defines: dict[str, str]):
        f = self.root / "shaders.properties"
        if not f.exists(): return self
        text = re.sub(r"\\\n", " ", f.read_text(errors="replace"))
        lines = conditionals(text, defines).splitlines()
        for line in lines:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line: continue
            k, v = (x.strip() for x in line.split("=", 1))
            self.props[k] = v
            m = re.match(r"(uniform|variable)\.(\w+)\.(\w+)$", k)
            if m: self.uniforms.append((m[2], m[3], v, m[1] == "variable"))
            m = re.match(r"profile\.(\w+)$", k)
            if m:
                prof = {}
                for tok in v.split():
                    if tok.startswith("!"): prof[tok[1:]] = "false"
                    elif "=" in tok: a, b = tok.split("=", 1); prof[a] = b
                    elif tok.startswith("profile."): prof.update(self.profiles.get(tok[8:], {}))
                    else: prof[tok] = "true"
                self.profiles[m[1]] = prof
        return self

    def program_enabled(self, world: str, prog: str, defines: dict[str, str]) -> bool:
        v = self.props.get(f"program.{world}/{prog}.enabled", self.props.get(f"program.{prog}.enabled"))
        if v is None: return True
        v = v.strip()
        if v in ("true", "false"): return v == "true"
        neg = v.startswith("!"); name = v.lstrip("!")         # enabled=OPTION (a toggle)
        on = name in defines
        return on != neg

    def blend(self, prog: str):
        return {k.split(".", 2)[2] if k.count(".") > 1 else "*": v for k, v in self.props.items()
                if k == f"blend.{prog}" or k.startswith(f"blend.{prog}.")}


def preprocess(text: str, defines: dict[str, str]) -> str:
    """C preprocessor for shaders.properties / GLSL, comments kept (needed for DRAWBUFFERS / RENDERTARGETS)."""
    args = ["cpp", "-P", "-C", "-undef", "-nostdinc", "-traditional-cpp", "-w"]
    args = ["cpp", "-P", "-C", "-undef", "-nostdinc", "-w"] + [f"-D{k}={v}" if v != "" else f"-D{k}" for k, v in defines.items()]
    # cpp chokes on lone apostrophes in property comments; keep the text intact otherwise
    r = subprocess.run(args, input=text, capture_output=True, text=True)
    return r.stdout


def _cond_value(expr: str, defs: dict[str, str]) -> bool:
    e = re.sub(r"//.*", "", expr)
    e = re.sub(r"\bdefined\s*\(\s*(\w+)\s*\)|\bdefined\s+(\w+)", lambda m: "1" if (m[1] or m[2]) in defs else "0", e)
    def ident(m):
        v = defs.get(m[0], "0")
        for _ in range(8):                       # resolve chained defines (#define A B)
            if re.fullmatch(r"[A-Za-z_]\w*", v or ""): v = defs.get(v, "0")
        if v in ("", "true"): return "1"
        if v == "false": return "0"
        return f"({v})"
    e = re.sub(r"\b[A-Za-z_]\w*\b", ident, e)
    e = e.replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)
    e = re.sub(r"(\d+\.?\d*)[fF]\b", r"\1", e)
    try: return bool(eval(e, {"__builtins__": {}}, {}))
    except Exception: return False


def conditionals(text: str, defines: dict[str, str]) -> str:
    """Line-based #if/#ifdef/#elif/#else/#endif/#define/#undef, no macro expansion of body lines (Iris properties)."""
    defs, out, stack = dict(defines), [], []   # stack: (active, taken, parent_active)
    active = True
    for raw in text.splitlines():
        line = raw.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif|define|undef)\b\s*(.*)", line)
        if not m:
            if active: out.append(raw)
            continue
        d, rest = m[1], m[2].strip()
        if d in ("if", "ifdef", "ifndef"):
            c = (rest.split()[0] in defs) if d == "ifdef" else (rest.split()[0] not in defs) if d == "ifndef" else _cond_value(rest, defs)
            stack.append((active, active and c)); active = active and c
        elif d == "elif":
            parent, taken = stack[-1]
            c = parent and not taken and _cond_value(rest, defs)
            stack[-1] = (parent, taken or c); active = c
        elif d == "else":
            parent, taken = stack[-1]; active = parent and not taken; stack[-1] = (parent, True)
        elif d == "endif":
            active = stack.pop()[0] if stack else True
        elif active and d == "define":
            parts = rest.split(None, 1); defs[parts[0]] = parts[1].split("//")[0].strip() if len(parts) > 1 else ""
        elif active and d == "undef":
            defs.pop(rest.split()[0], None)
    return "\n".join(out)


def pipeline_consts(pre_src: str) -> dict[str, str]:
    """Iris pipeline constants read from a (preprocessed) program: buffer formats, clears, shadow settings..."""
    out = {}
    for m in re.finditer(r"\bconst\s+(?:int|float|bool|vec4|ivec2)\s+(\w+)\s*=\s*([^;]+);", pre_src):
        name, val = m[1], m[2].strip()
        if re.match(r"(colortex\d+|gaux\d|gcolor|gdepth|gnormal|composite|shadowcolor\d|shadowtex\d)"
                    r"(Format|Clear|ClearColor|MipmapEnabled)$", name) or name in (
                "shadowMapResolution", "shadowDistance", "shadowDistanceRenderMul", "shadowIntervalSize", "sunPathRotation",
                "ambientOcclusionLevel", "eyeBrightnessHalflife", "centerDepthHalflife", "wetnessHalflife", "drynessHalflife",
                "shadowHardwareFiltering", "shadowtex0Nearest", "shadowtex1Nearest", "shadowcolor0Nearest", "noiseTextureResolution",
                "generateShadowMipmap", "shadowMapFov", "voxelDistance"):
            out[name] = val
    return out


def targets(pre_src: str) -> list[int] | None:
    """Draw buffers of a fragment program: /* RENDERTARGETS: 0,3 */ or /* DRAWBUFFERS:03 */ (last one wins, like Iris)."""
    rt = re.findall(r"/\*\s*RENDERTARGETS\s*:\s*([\d,\s]+)\*/", pre_src)
    if rt: return [int(x) for x in rt[-1].replace(" ", "").split(",") if x]
    db = re.findall(r"/\*\s*DRAWBUFFERS\s*:\s*([0-9A-Fa-f]+)\s*\*/", pre_src)
    if db: return [int(c, 16) for c in db[-1]]
    return None
