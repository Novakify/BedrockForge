#!/usr/bin/env python3
"""Read-only GeckoLib source/Bedrock conversion inventory comparison (stdlib only).

Usage: python tools/geckobridge_compare.py --jar mod.jar --bedrock-rp path/to/RP
No binaries are executed. Reports missing animations/channels and curve risks.
"""
import argparse
import json
import pathlib
import zipfile

def animations_from_jar(path):
    found = {}
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            if not name.endswith(".animation.json"):
                continue
            root = json.loads(z.read(name))
            for anim_name, anim in root.get("animations", {}).items():
                found[anim_name] = {"source": name, "bones": anim.get("bones", {}), "loop": anim.get("loop", False),
                                    "event_types": [k for k in ("sound_effects", "particle_effects", "timeline") if k in anim]}
    return found

def animations_from_rp(path):
    found = {}
    for file in pathlib.Path(path).rglob("*.json"):
        if "animation" not in file.parts and "animations" not in file.parts:
            continue
        try:
            root = json.loads(file.read_text(encoding="utf-8"))
        except (UnicodeError, ValueError):
            continue
        for anim_name, anim in root.get("animations", {}).items():
            found[anim_name] = {"source": str(file), "bones": anim.get("bones", {}), "loop": anim.get("loop", False),
                                "event_types": [k for k in ("sound_effects", "particle_effects", "timeline") if k in anim]}
    return found

def compare(source, target):
    report = {"source_animations": len(source), "output_animations": len(target), "missing_animations": [],
              "matched_animations": [], "unmatched_output": sorted(set(target) - set(source)),
              "important_limitations": [
                  "Animation identifiers may be renamed by a converter; missing names are not proof of loss.",
                  "Matching animation and channel counts do not prove visual or timing fidelity.",
                  "No Java-side controller, procedural bone, render-layer or entity gameplay analysis.",
                  "No interpolation curve reconstruction or in-game testing."]}
    for name, src in source.items():
        if name not in target:
            report["missing_animations"].append({"name": name, "source": src["source"]})
            continue
        dst = target[name]
        src_bones = src["bones"]; dst_bones = dst["bones"]
        missing_bones = sorted(set(src_bones) - set(dst_bones))
        missing_channels = {bone: sorted(set(chans) - set(dst_bones.get(bone, {})))
                            for bone, chans in src_bones.items() if bone in dst_bones and set(chans) - set(dst_bones[bone])}
        report["matched_animations"].append({"name": name, "missing_bones": missing_bones,
                                              "missing_channels": missing_channels,
                                              "source_events": src["event_types"], "output_events": dst["event_types"],
                                              "loop_source": src["loop"], "loop_output": dst["loop"]})
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jar", required=True)
    parser.add_argument("--bedrock-rp", required=True)
    parser.add_argument("--output")
    args = parser.parse_args()
    result = compare(animations_from_jar(args.jar), animations_from_rp(args.bedrock_rp))
    rendered = json.dumps(result, indent=2)
    if args.output: pathlib.Path(args.output).write_text(rendered + "\n", encoding="utf-8")
    else: print(rendered)

if __name__ == "__main__":
    main()
