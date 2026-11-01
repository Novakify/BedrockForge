# GeckoBridge tools (research branch `research/geckobridge`)

Offline half of GeckoBridge: GeckoLib 4/5 entity assets in a Java mod jar -> Bedrock resource + behaviour pack.
Plan, format mapping and runtime design: `docs/PORT_GECKOBRIDGE.md`.

| File | What |
|---|---|
| `scan.py` | Read-only feature inventory of a mod jar: geometry/animation features, easings, Molang names, glowmasks, GeckoLib API names in the class files |
| `gbanim.py` | Animation core: parses GeckoLib keyframes the way GeckoLib bakes them, bakes every eased segment (30+ GeckoLib easings, `easingArgs`, `step`, `catmullrom`) into linear Bedrock keyframes with an adaptive error bound, maps GeckoLib-only Molang, and measures the bake error (`gl_eval` vs `bedrock_eval`) |
| `convert.py` | M0 converter: spec.json -> RP + BP (renamed geometry, baked animations, client entity with always-on animations, glowmask layer, still display mob) + report.json |
| `preview.py` + `preview.js` | Headless render of the converted entity at given times, Bedrock keyframes vs the GeckoLib source side by side (needs Node, Playwright, three.js) |

```
python -I tools/GeckoBridge/scan.py mod.jar > scan.json
python -I tools/GeckoBridge/convert.py spec.json OUT
python -I tools/GeckoBridge/preview.py OUT/<Pack>RP <entity> poses.json --times 0,1,2 --jar mod.jar --src-anim <path> --src-names idle
THREE=/path/to/three.min.js node tools/GeckoBridge/preview.js poses.json preview.png
```

The easing maths in `gbanim.py` is ported from GeckoLib (`com.geckolib.animation.object.EasingType`, MIT, (c) GeckoLib).

**Licence rule:** the tools are ours, the converted assets are not. The repo is private (since 2026-10-30), so
converted packs for Fernando's personal use may live in `ports/`; never share them, and never commit them while the
repo is public. Test jars stay out of git (`fetch_testmods.py`).
