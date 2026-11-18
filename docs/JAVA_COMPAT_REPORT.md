# Java → Bedrock compatibility: approach ranking (v1)

Evidence: JavaModAnalyzer v0 on the "Impact frames" Fabric mod 1.2.1.5 (MC 26.2, 44 classes, 18 mixins)
and a synthetic test mod, plus building the Bedrock equivalent of Impact Frames by hand (Prototype 3).
Scores are 1 (poor) to 5 (best).

| | A. Manual / native port | B. Semi-automated conversion | C. Embedded JVM / bytecode |
|---|---|---|---|
| Feasibility | **5** (done once already: Impact Frames) | **4** for metadata, assets, keybinds, config, simple events; 1 for render mixins | **1** |
| Implementation complexity | 3 per mod | 3 once, then cheap per mod | 1 (enormous) |
| Performance | **5** native C++ / Script API | 5 (generates native/script code) | 2 (JNI hops + a second runtime) |
| Compatibility potential | 4 (anything the engine allows) | 3 (grows with the SDK) | 1 (a JVM doesn't bring Minecraft Java's engine classes, registries, renderer or Mixin targets) |
| Maintenance | 2 (each port tracks its own mod) | 4 (fix the mapping once) | 1 (two moving engines) |
| Automation | 1 | **4** | 3 in theory, 0 in practice |
| Long-term value | 3 | **5** | 1 |
| **Total** | 23 | **28** | 10 |

## Verdict
1. **B (semi-automated)** is the backbone. The analyzer already does steps 1-5 and 10 of the plan
   (metadata, dependencies, API-usage map, mixins, assets, feasibility score). The next steps are
   scaffold generation (Fabric keybind → `bf::input::bindKey`, config → `bf::config`, events →
   `bf::events` / Script API events) and asset conversion. That last one only applies where the licence allows.
2. **A (manual)** for flagship ports and anything render-heavy. Impact Frames showed why: 77% of its
   API surface is rendering mixins with no automatic equivalent. Hand-built Bedrock versions
   (Script API colour grading + native overlay) get the look with no Java code at all.
3. **C (JVM)** is parked. There's no path to running Fabric/Forge jars on Bedrock: their bytecode targets
   Java Edition's classes, which don't exist in Bedrock's C++ engine.

## What Impact Frames taught us about the "Bedrock path"
| Java mechanism | Bedrock Forge equivalent | Status |
|---|---|---|
| Post-effect shader (invert/threshold) | Per-player colour grading via `@minecraft/server-graphics` (official, VV only) | Built (type-checked) |
| Frame textures from resource packs | `bf::hud::drawTexture` + swappable RP frames | Built (compiled) |
| `AttackEntityCallback` / hit detection | `ActorHurtEvent` (native) / `entityHurt` (Script API) | Built |
| KeyMapping | `bf::input::bindKey` → appears in Controls | Built |
| Screen shake | vanilla `/camerashake` | Built |
| Entity silhouettes (solid black/white mobs) | RenderDragon material redirect (docs/RENDERING_PLAN.md) | Planned |
| Options screens (Mod Menu) | config.json now; in-game settings UI later | Partial |

Rule that falls out of this: **check the official Script API first**. Bedrock keeps adding
engine-level features (colour grading, camera, fog, lighting) that Java mods had to hack in with mixins.
