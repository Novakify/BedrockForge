#!/usr/bin/env python3
"""Read-only GeckoLib 4/5 JAR asset audit. Python 3.10+, standard library only."""
import argparse
import collections
import json
import pathlib
import zipfile

def audit(path):
    count = collections.Counter()
    warnings = []
    with zipfile.ZipFile(path) as archive:
        files = archive.namelist()
        for file in files:
            if file.endswith(".class"):
                count["classes"] += 1
                raw = archive.read(file)
                if b"software/bernie/geckolib/" in raw:
                    count["classes_geckolib4"] += 1
                if b"com/geckolib/" in raw:
                    count["classes_geckolib5"] += 1
            if file.endswith(".geo.json"):
                count["geometry_files"] += 1
        for file in files:
            if not file.endswith(".animation.json"):
                continue
            count["animation_files"] += 1
            try:
                document = json.loads(archive.read(file))
            except (ValueError, UnicodeError) as exc:
                warnings.append(f"{file}: {exc}")
                continue
            for name, animation in document.get("animations", {}).items():
                count["animations"] += 1
                if animation.get("loop") == "hold_on_last_frame":
                    count["hold_last_frame"] += 1
                for channel in ("sound_effects", "particle_effects", "timeline"):
                    if channel in animation:
                        count[channel] += 1
                for bone, channels in animation.get("bones", {}).items():
                    for channel, values in channels.items():
                        count["bone_channels"] += 1
                        if channel not in ("position", "rotation", "scale"):
                            count["unknown_channels"] += 1
                        if not isinstance(values, dict):
                            count["constant_other"] += 1
                            continue
                        if "vector" in values:
                            count["constant_vector"] += 1
                            continue
                        for timestamp, value in values.items():
                            try:
                                float(timestamp)
                            except (ValueError, TypeError):
                                count["unexpected_keyframe_keys"] += 1
                                continue
                            count["keyframes"] += 1
                            if not isinstance(value, dict):
                                count["simple_keyframe"] += 1
                                continue
                            for k in ("pre", "post", "vector", "easing", "easingArgs"):
                                if k in value:
                                    count[k + "_entries"] += 1
                            if value.get("lerp_mode") == "catmullrom":
                                count["catmullrom"] += 1
                            for k in ("pre", "post"):
                                if isinstance(value.get(k), dict) and "vector" in value[k]:
                                    count["nested_" + k + "_vector"] += 1
    return {
        "jar": pathlib.Path(path).name,
        "geckolib_namespace": "5" if count["classes_geckolib5"] else ("4" if count["classes_geckolib4"] else "not observed"),
        "counts": dict(sorted(count.items())),
        "warnings": warnings,
        "verified": "JAR bytes and animation JSON parsed only; no conversion or in-game validation"
    }

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jars", nargs="+")
    parser.add_argument("--output", help="Output JSON path")
    args = parser.parse_args()
    result = {"schema_version": 1, "audits": [audit(j) for j in args.jars]}
    text = json.dumps(result, indent=2)
    if args.output:
        pathlib.Path(args.output).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
