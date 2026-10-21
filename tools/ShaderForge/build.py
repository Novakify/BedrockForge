#!/usr/bin/env python3
"""ShaderForge - build a Bedrock Forge shaderpack from HLSL + the game's own materials.

  python build.py <game materials dir> <pack dir: shaders/src/<Pack>> -o <out>/<Pack>

A pack source folder contains <Material>/<Pass>.Fragment.hlsl (or <Material>/Fragment.hlsl for every pass).
The HLSL body is written against semantic names (see shaders/src/README.md); ShaderForge:
  1. loads the vanilla <Material>.material.bin (lazurite + 1.26 layout fix, lz.py)
  2. for every variant of that pass, reads the vanilla SM6.0 / SM6.5 fragment's input signature (dxc -dumpbin)
  3. generates the matching input struct (same elements, order, interpolation), compiles with dxc, and keeps the
     result ONLY if the compiled signature (name, index, mask, register) equals vanilla - otherwise that variant
     stays vanilla, so a pack can never break pipeline linkage
  4. packs the material and writes <out>/renderer/materials/<Material>.material.bin
SM4.0 / SM5.0 (D3D11) blobs are left vanilla.
"""
from __future__ import annotations
import argparse, re, subprocess, sys, tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import lz  # noqa: E402

DXC = "/opt/dxc/bin/dxc"
PROFILES = {"Direct3D_SM60": "ps_6_3", "Direct3D_SM65": "ps_6_5"}
WIDTH = {"x": 1, "xy": 2, "xyz": 3, "xyzw": 4, "y": 1, "z": 1, "w": 1, "yz": 2, "zw": 2, "yzw": 3}


def signature(blob: bytes):
    with tempfile.NamedTemporaryFile(suffix=".dxil", delete=False) as f:
        f.write(blob); path = f.name
    out = subprocess.run([DXC, "-dumpbin", path], capture_output=True, text=True).stdout
    rows, interp, sec = [], {}, None
    for line in out.splitlines():
        if line.startswith("; Input signature:"): sec = "in" if sec is None else "in2"
        elif line.startswith("; Output signature:"): sec = "out" if sec == "in" else "out2"
        m = re.match(r";\s+(\w+)\s+(\d+)\s+(\w+)\s+(\d+)\s+(\w+)\s+(\w+)", line)
        if m and sec == "in":
            rows.append((m[1], int(m[2]), m[3], int(m[4]), m[5]))
        m2 = re.match(r";\s+(\w+)\s+(\d+)\s+([a-z]+(?:\s[a-z]+)?)\s*$", line)
        if m2 and sec == "in2":
            interp[(m2[1], int(m2[2]))] = m2[3]
    shader_model = re.search(r'!"ps", i32 (\d), i32 (\d)', out)
    return rows, interp, shader_model and f"{shader_model[1]}_{shader_model[2]}"


def struct_for(rows, interp, order):
    fields = []
    for name, idx, mask, _reg, sysv in sorted(rows, key=lambda r: order.index((r[0], r[1]))):
        if name == "SV_Position":
            fields.append("    float4 position : SV_Position;"); continue
        mode = interp.get((name, idx), "linear")
        q = {"centroid": "centroid ", "nointerpolation": "nointerpolation ", "noperspective": "noperspective "}.get(mode, "")
        n = WIDTH.get(mask, 4)
        fields.append(f"    {q}float{n if n > 1 else ''} {name.lower()}{idx} : {name}{idx};")
    return "struct PSIn {\n" + "\n".join(fields) + "\n};\n"


def element_order(blob: bytes):
    # element (= loadInput id) order comes from the second, interpolation-mode table
    with tempfile.NamedTemporaryFile(suffix=".dxil", delete=False) as f:
        f.write(blob); path = f.name
    out = subprocess.run([DXC, "-dumpbin", path], capture_output=True, text=True).stdout
    order, sec = [], 0
    for line in out.splitlines():
        if line.startswith("; Input signature:"): sec += 1
        if line.startswith("; Output signature:") and sec == 2: break
        m = re.match(r";\s+(\w+)\s+(\d+)\s+([a-z]+(?:\s[a-z]+)?)\s*$", line)
        if m and sec == 2: order.append((m[1], int(m[2])))
    return order


def compile_variant(body: str, rows, interp, order, profile, defines, workdir: Path):
    src = struct_for(rows, interp, order) + body
    have = {f"{n}{i}" for n, i, *_ in rows}
    defs = [f"-D{d}" for d in defines] + [f"-DHAS_{h}=1" for h in have]
    for packing in ([], ["-pack-optimized"]):
        hl = workdir / "v.hlsl"; hl.write_text(src)
        out = workdir / "v.dxil"
        r = subprocess.run([DXC, "-T", profile, "-E", "main", "-O3", *packing, *defs, "-Fo", str(out), str(hl)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, r.stderr[-600:]
        got, _, _ = signature(out.read_bytes())
        if [(n, i, m, g) for n, i, m, g, _ in got] == [(n, i, m, g) for n, i, m, g, _ in rows]:
            return out.read_bytes(), None
    return None, "signature mismatch"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("materials", type=Path); ap.add_argument("pack", type=Path); ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args(argv)
    outdir = a.out / "renderer" / "materials"; outdir.mkdir(parents=True, exist_ok=True)
    common = (a.pack / "common.hlsli").read_text() if (a.pack / "common.hlsli").exists() else ""
    report = []
    for mat_dir in sorted(p for p in a.pack.iterdir() if p.is_dir()):
        name = mat_dir.name
        vanilla = a.materials / f"{name}.material.bin"
        if not vanilla.exists(): report.append(f"{name}: no vanilla material"); continue
        mat = lz.load(str(vanilla))
        replaced = kept = 0
        with tempfile.TemporaryDirectory() as td:
            for p in mat.passes:
                src = mat_dir / f"{p.name}.Fragment.hlsl"
                if not src.exists(): src = mat_dir / "Fragment.hlsl"
                if not src.exists(): continue
                body = common + src.read_text()
                for v in p.variants:
                    flags = getattr(v, "flags", {}) or {}
                    if flags.get("Seasons") == "On": kept += 1; continue  # different code path, keep vanilla
                    defines = [f"PASS_{p.name.upper()}=1"] + [f"FLAG_{k.upper()}_{str(val).upper()}=1" for k, val in flags.items()]
                    for s in v.shaders:
                        if s.stage.name != "Fragment" or s.platform.name not in PROFILES: continue
                        blob = s.bgfx_shader.shader_bytes
                        rows, interp, sm = signature(blob)
                        order = element_order(blob)
                        new, err = compile_variant(body, rows, interp, order, f"ps_{sm}" if sm else PROFILES[s.platform.name], defines, Path(td))
                        if new is None:
                            kept += 1
                            report.append(f"{name}/{p.name} {flags} {s.platform.name}: kept vanilla ({err.strip().splitlines()[-1] if err else ''})")
                            continue
                        s.bgfx_shader.shader_bytes = new
                        replaced += 1
        lz.save(mat, str(outdir / f"{name}.material.bin"))
        report.append(f"{name}: {replaced} fragment blobs replaced, {kept} kept vanilla")
    (a.out / "BUILD_REPORT.txt").write_text("\n".join(report) + "\n")
    print("\n".join(r for r in report if "kept vanilla (" not in r))
    bad = [r for r in report if "kept vanilla (" in r]
    if bad: print(f"{len(bad)} variants kept vanilla, see BUILD_REPORT.txt; first: {bad[0]}")


if __name__ == "__main__":
    main()
