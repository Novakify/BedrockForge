#!/usr/bin/env python3
"""IrisBridge bundle: whole Iris/OptiFine pack -> <out>/ (DXIL per program/stage + manifest.json) for Renderer Foundation.

  python bundle.py <pack>/shaders <out> [--profile HIGH] [--set NAME=VALUE ...]

manifest.json: options (+ values/profiles), per world: enabled programs in pipeline order with stage DXIL, $Globals layout,
texture slots, render targets, blend/alpha; pipeline constants (buffer formats, clears, shadow settings); custom-uniform
bytecode. RF reads only this file + the .dxil blobs; it never touches GLSL.
"""
from __future__ import annotations
import argparse, json, re, subprocess, shutil, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from translate import translate, BASE_DEFINES, DXC, SLOTS
from pack import Pack, PROGRAMS, FALLBACK, preprocess, pipeline_consts, targets
from expr import build as build_uniforms

STAGE_EXT = {"vert": "vsh", "frag": "fsh", "geom": "gsh", "comp": "csh"}


def reflect(dxil: Path) -> dict:
    out = subprocess.run([DXC, "-dumpbin", str(dxil)], capture_output=True, text=True).stdout
    cb, res, outs, ins, sec = [], [], [], [], None
    for line in out.splitlines():
        m = re.match(r";\s+(?:column_major\s+|row_major\s+)?(\w+)\s+(\w+)(\[\d+\])?;\s*; Offset:\s*(\d+)", line)
        if m: cb.append({"name": m[2], "type": m[1] + (m[3] or ""), "offset": int(m[4])}); continue
        m = re.match(r";\s+\} \$Globals;\s+; Offset:\s+\d+ Size:\s+(\d+)", line)
        if m: cb_size = int(m[1])
        m = re.match(r";\s+(\S+)\s+(cbuffer|sampler|texture|UAV)\s+\S+\s+\S+\s+\S+\s+([a-z]+)(\d+)", line)
        if m: res.append({"name": m[1], "kind": m[2], "reg": m[3], "slot": int(m[4])}); continue
        if line.startswith("; Input signature"): sec = "in" if sec is None else "done"
        elif line.startswith("; Output signature"): sec = "out" if sec == "in" else "done"
        m = re.match(r";\s+(\w+)\s+(\d+)\s+(\w+)\s+(\d+)\s+(\w+)\s+(\w+)", line)
        if m and sec in ("in", "out"):
            (ins if sec == "in" else outs).append({"sem": m[1], "index": int(m[2]), "mask": m[3], "reg": int(m[4]), "fmt": m[6]})
    return {"globals": cb, "globals_size": locals().get("cb_size", 0), "resources": res, "inputs": ins, "outputs": outs}


def emit_runtime(man: dict) -> str:
    """Line format read by RF (src/IrisBundle.h): one record per line, space separated, no JSON parser needed in RF."""
    L = [f"irisbridge 1", f"pack {man['pack']}"]
    for k, v in man["consts"].items(): L.append(f"const {k} {v}")
    u = man["uniforms"]
    L.append(f"funcs {' '.join(u['funcs'])}")
    L.append(f"smooth {u['smooth_states']}")
    for i, n in enumerate(u["inputs"]): L.append(f"uinput {i} {n}")
    for i, sl in enumerate(u["slots"]):
        L.append(f"uslot {i} {sl['name']} {sl['uniform']} {sl['comp']} {int(sl['export'])}")
        L.append(f"ucode {i} {len(u['code'][i])} " + " ".join(f"{op} {repr(float(a))}" for op, a in u["code"][i]))
    for world, progs in man["worlds"].items():
        for p in progs:
            kind = "final" if p["program"] == "final" else "composite" if p["program"].startswith("composite") else \
                   "deferred" if p["program"].startswith("deferred") else "prepare" if p["program"].startswith("prepare") else \
                   "shadowcomp" if p["program"].startswith("shadowcomp") else "shadow" if p["program"].startswith("shadow") else "gbuffers"
            if not all(st["ok"] for st in p["stages"].values()): continue
            L.append(f"pass {world} {p['program']} {kind} {p['source']}")
            if p.get("targets") is not None: L.append("targets " + " ".join(map(str, p["targets"])) if p["targets"] else "targets")
            for k, v in (p.get("blend") or {}).items(): L.append(f"blend {k} {v}")
            if p.get("alphaTest"): L.append(f"alphatest {p['alphaTest']}")
            for sname, st in p["stages"].items():
                L.append(f"stage {sname} {st['dxil']} {st.get('globals_size', 0)}")
                for g in st["globals"]: L.append(f"g {g['name']} {g['type']} {g['offset']}")
                for r in st["resources"]: L.append(f"res {r['name']} {r['kind']} {r['reg']} {r['slot']}")
                for i in st["inputs"]:
                    if sname == "vert": L.append(f"in {i['sem']} {i['index']} {i['mask']} {i['fmt']}")
                for k, v in (st.get("defaults") or {}).items(): L.append(f"def {k} {v.replace(' ', '')}")
            L.append("end")
    return "\n".join(L) + "\n"


def find_program(shaders: Path, world: str, prog: str):
    """Iris lookup: world folder first, then the pack root; then the fallback chain."""
    p = prog
    while p:
        for base in (shaders / world, shaders):
            if any((base / f"{p}.{e}").exists() for e in ("fsh", "vsh", "csh")): return p, base / p
        p = FALLBACK.get(p)
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shaders"); ap.add_argument("out"); ap.add_argument("--profile"); ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--worlds", default="world0,world-1,world1")
    a = ap.parse_args()
    shaders, out = Path(a.shaders), Path(a.out)
    if out.exists(): shutil.rmtree(out)
    out.mkdir(parents=True)

    pk = Pack(shaders).scan_options()
    pk.load_properties(pk.option_defines())                    # first pass: profiles
    ov = dict(pk.profiles.get(a.profile, {})) if a.profile else {}
    ov.update(dict(x.split("=", 1) for x in a.set))
    ov = {k: v for k, v in ov.items() if k in pk.options}
    defines = pk.option_defines(ov)
    pk = Pack(shaders, pk.options).load_properties(defines)    # second pass: with the chosen options
    pk.profiles = Pack(shaders, pk.options).load_properties(pk.option_defines()).profiles

    man = {"format": 1, "pack": shaders.parent.name, "profile": a.profile, "overrides": ov,
           "slots": SLOTS, "options": {n: {"default": o.default, "values": o.values, "kind": o.kind} for n, o in pk.options.items()},
           "profiles": pk.profiles, "props": pk.props, "worlds": {}, "consts": {}}
    total = ok = 0
    for world in a.worlds.split(","):
        wdir = out / world; wdir.mkdir()
        progs = []
        for prog in PROGRAMS:
            name, path = find_program(shaders, world, prog)
            if not name or (name != prog and not prog.startswith("gbuffers")): continue
            if not pk.program_enabled(world, prog, defines): continue
            if any(p["program"] == prog for p in progs): continue
            same = next((p for p in progs if p["program"] == name), None)
            if same is not None:                       # Iris fallback: reuse the source program's shaders
                progs.append({**same, "program": prog, "source": name, "blend": pk.blend(prog) or same["blend"],
                              "alphaTest": pk.props.get(f"alphaTest.{prog}", same["alphaTest"])})
                continue
            res = translate(shaders, world, prog, wdir, {}, ov, path)
            if not res: continue
            entry = {"program": prog, "source": name, "stages": {}, "blend": pk.blend(prog),
                     "alphaTest": pk.props.get(f"alphaTest.{prog}")}
            for key, r in res.items():
                total += 1
                stage = {"vsh": "vert", "fsh": "frag", "csh": "comp", "gsh": "geom"}[key.rsplit(".", 1)[1]]
                st = {"ok": r["ok"]}
                if r["ok"]:
                    ok += 1
                    dx = wdir / f"{prog}.{stage}.dxil"
                    st.update(reflect(dx)); st["dxil"] = f"{world}/{dx.name}"
                    if r.get("defaults"): st["defaults"] = r["defaults"]
                    for rr in st["resources"]:      # comparison samplers (hardware shadow filtering) need their own static sampler kind
                        if rr["kind"] == "sampler" and rr["slot"] in r.get("cmp", []): rr["kind"] = "samplercmp"
                    pre = preprocess((wdir / f"{prog}.{stage}.src").read_text(), BASE_DEFINES)
                    man["consts"].update(pipeline_consts(pre))
                    if stage == "frag":
                        tg = targets(pre)
                        entry["targets"] = tg if tg is not None else list(range(len(st["outputs"])))
                else:
                    st["error"] = r["error"]
                entry["stages"][stage] = st
            progs.append(entry)
        man["worlds"][world] = progs
        for f in wdir.glob("*"):
            if f.suffix not in (".dxil",): f.unlink()
    man["uniforms"] = build_uniforms(pk.uniforms)
    (out / "manifest.json").write_text(json.dumps(man, indent=1))
    (out / "runtime.txt").write_text(emit_runtime(man))
    print(f"{ok}/{total} stages -> DXIL; {sum(len(v) for v in man['worlds'].values())} programs; "
          f"{len(man['uniforms']['slots'])} custom uniform slots; consts: {len(man['consts'])}")
    for w, ps in man["worlds"].items():
        bad = [f"{p['program']}.{s}" for p in ps for s, v in p["stages"].items() if not v["ok"]]
        print(f"  {w}: {len(ps)} programs" + (f", failed: {bad}" if bad else ""))


if __name__ == "__main__":
    main()
