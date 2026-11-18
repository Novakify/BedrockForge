# Roadmap

| Phase | Goal | Exit criteria |
|---|---|---|
| 1 | Env + isolated 1.26.51.01 instance + LeviLamina + HelloWorld | Mod logs on start, event callback fires, disables cleanly — **in game** |
| 2 | SDK skeleton + Custom HUD (keybind toggle) | HUD draws, toggles, no FPS hit, controls intact |
| 3 | Impact Frame demo | flash + sound + shake on hit, settings, perf measured |
| 4 | Horror Encounter | custom entity (pack) + native screen effects bridged |
| 5 | Java analysis tooling | analyzer reads a simple open-source Fabric mod → feasibility report |
| 6+ | Rendering API, porting assistant, launcher UX | — |

Java compat ranking (preliminary, to be formalized at Task 10):
1. **B – Semi-automated conversion** — best long-term value; analyzer + scaffolds + "unsupported" report.
2. **A – Manual/native porting** — always works, used for every flagship port.
3. **C – Embedded JVM** — lowest feasibility; a JVM doesn't bring Minecraft Java's engine classes. Parked.
