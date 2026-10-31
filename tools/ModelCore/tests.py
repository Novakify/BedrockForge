#!/usr/bin/env python3
"""ModelCore checks. python -I tools/ModelCore/tests.py [mod.jar ...]
1. interp matches golden.json (the contract for the native C++ evaluator).
2. every geo file in the given jars round-trips losslessly through rig.py."""
import json, sys, zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ModelCore import interp
from ModelCore.rig import roundtrip_equal

g = json.loads((Path(__file__).parent / "golden.json").read_text())
worst = 0.0
for key, pts in g["easings"].items():
    name, arg = key.split("|"); arg = None if arg == "None" else float(arg)
    for d, w in pts: worst = max(worst, abs(interp.ease(name, d, arg) - w))
for d, p0, p1, p2, p3, v in g["catmull"]: worst = max(worst, abs(interp.catmull(d, p0, p1, p2, p3) - v))
print(f"interp vs golden: {len(g['easings'])} curves, max diff {worst:.2e}", "OK" if worst < 1e-9 else "FAIL")
tot = bad = 0
for jar in sys.argv[1:]:
    z = zipfile.ZipFile(jar)
    for f in z.namelist():
        if f.startswith("assets/") and f.endswith(".json") and ("/geo/" in f or f.endswith(".geo.json")):
            tot += 1
            if not roundtrip_equal(json.loads(z.read(f).decode("utf-8-sig"))): bad += 1; print("ROUNDTRIP FAIL", jar, f)
if tot: print(f"rig round-trip: {tot} geo files, {bad} failures", "OK" if not bad else "FAIL")
sys.exit(1 if worst >= 1e-9 or bad else 0)
