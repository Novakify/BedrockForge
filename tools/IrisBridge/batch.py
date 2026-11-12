#!/usr/bin/env python3
"""Translate every program of a pack's world folder and print a coverage table."""
import sys, json, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from translate import translate
shaders, world, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)
progs = sorted({p.stem for p in (shaders / world).iterdir() if p.suffix in (".fsh", ".vsh", ".csh", ".gsh")})
allres = {}
for p in progs: allres.update(translate(shaders, world, p, out, {}))
ok = sum(v["ok"] for v in allres.values())
print(f"{ok}/{len(allres)} stages compiled to DXIL")
errs = collections.Counter()
for k, v in sorted(allres.items()):
    if not v["ok"]:
        e = (v["error"] or "").strip()[:160]; errs[(v["failed_at"], e.split(":", 3)[-1].strip()[:110])] += 1
        print(f"  FAIL {k:34} @ {v['failed_at']}: {e[:150]}")
print("\nTop error kinds:"); [print(f"  {n:3}x {s} | {e}") for (s, e), n in errs.most_common(12)]
(out / "report.json").write_text(json.dumps(allres, indent=1))
