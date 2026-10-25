# GeckoBridge audit handoff — 2026-10-25

**Branch:** `geckobridge/compat-audit-20261025` (do not merge without review).
**Goal:** add non-destructive evidence collection before modifying the existing JavaModAnalyzer, ModPorter, BedrockForgeCore, or Renderer Foundation.
**Hardware:** no PC access from this ChatGPT session; no Bedrock runtime launch.

## Files added
- `tools/geckobridge_audit.py`: read-only JAR namespace + animation-format inventory; GeckoLib 4 (`software/bernie/geckolib/`) and GeckoLib 5 (`com/geckolib/`).
- `tools/geckobridge_compare.py`: read-only source JAR versus generated Bedrock RP animation inventory. It compares *identical IDs*, bone/channel presence, event type presence and loop flags; it does **not** establish curve fidelity or Java runtime behavior.

## Test inputs (user-provided; not committed)
- `otherworld-0.9.2.12.jar` — Forge, GeckoLib 4 namespace.
- `belowland-0.2.5.6-neo.jar` — NeoForge, GeckoLib 4 namespace.
- `cave_dweller-1.6.4-neoforge-26.3.jar` — NeoForge, GeckoLib 5 namespace.
Keep copyrighted JARs, extracted assets and conversion products off the repository unless redistribution permission is documented.

## CLI

```powershell
python tools/geckobridge_audit.py C:\\testmods\\otherworld-0.9.2.12.jar C:\\testmods\\belowland-0.2.5.6-neo.jar C:\\testmods\\cave_dweller-1.6.4-neoforge-26.3.jar --output audit.json
python tools/geckobridge_compare.py --jar C:\\testmods\\cave_dweller-1.6.4-neoforge-26.3.jar --bedrock-rp C:\\converted\\CaveDwellerRP --output comparison.json
```

## Evidence and important caveats
- Repository session notes (2026-10-22/23) say that converted Dweller animated entities were observed in-game and that Sculk Horde conversion handled GeckoLib vector keyframes, while some timeline instructions were dropped.
- Do **not** equate static JSON parse success with proper easing, curve interpolation, pose composition, scale, camera motion, material layering or events.
- GeckoLib 5 namespace detection confirms *references*, not actual GeckoLib compatibility. An optional or missing declaration is not proof of whether classes can initialize.
- The comparison script currently assumes animation IDs survive conversion. Record explicit source-to-output ID maps before judging renamed assets; do not classify unmatched IDs as silently dropped.
- Converter may output separate animation and animation-controller files. The comparison does not parse animation-controller references or event contents.
- GeckoLib 4/5 Java controllers and custom renderer APIs require separate bytecode/source inspection and runtime work.
- No build, CI, JavaModAnalyzer run, ModPorter run, or in-game tests have been executed for these new tools in this session.

## Claude continuation checklist
1. Find the actual JavaModAnalyzer and ModPorter source locations; compare their parsers with the new standalone audit.
2. Add narrow regression tests (fixture JSON) for nested `pre/post.vector`, explicit `easing/easingArgs`, Catmull–Rom, loops and sound/particle/timeline events.
3. Convert the three JARs using existing tools in an isolated output folder. Do not replace user's installed packs.
4. Produce source-to-generated ID mapping and run `geckobridge_compare.py`. Report preserved/approximated/dropped/unhandled with explicit file+animation ID evidence. Never infer fidelity from file counts alone.
5. Audit Java controllers and client renderer layers separately; prioritize Cave Dweller's eye layer and animation controllers.
6. Run renderer and world tests on an isolated LeviLauncher 1.26.51.01 instance with backups and content logs.
7. Only after green validation, propose a GeckoBridge adapter integration and obtain review before merging to main.

## Licensing
Otherworld's uploaded metadata reports All Rights Reserved. Do not publish mod assets or decompiled proprietary source into this GitHub repository. No changes were made to the user's PC or main branch.
