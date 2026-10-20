# Project Status - 2026-10-20 (evening)

**Current phase:** Phase 1/2 code written. Waiting on: GitHub repo (cloud build) + you at the PC (installs, in-game test).

**Completed**
- Upstream research: 6 repos, licenses, version support (docs/RESEARCH_INDEX.md)
- `BedrockForgeCore` mod: SDK v0 (Events, Input, Config, Hud) + Prototype 1 (lifecycle/events) + Prototype 2 (HUD, J toggle)
- GitHub Actions workflow that builds the client DLL on Windows
- JavaModAnalyzer v0 (tools/JavaModAnalyzer): metadata, mixins, API-usage mapping, assets, feasibility score
- Installers in Downloads\BedrockForge-Installers: LeviLauncher (installer + portable), xmake 3.1.2, Git 2.56.0.2

**Verified**
| Item | Level |
|---|---|
| Target versions, APIs used (KeyRegistry, UIRenderEvent, Client*Event) | Verified in source |
| JavaModAnalyzer on synthetic Fabric jar | Verified by execution |
| LeviLauncher installer SHA-256 matches GitHub digest | Verified |
| BedrockForgeCore compiles | **Not yet** (needs CI or local build) |
| Anything in Minecraft | **Not yet** |

**Problems**
- The Microsoft download host is blocked from the remote session: VS Build Tools + VC++ redist must be downloaded on the PC.
- LLVM download was interrupted (partial `LLVM-23.1.4-win64.msi.part` in the installers folder; ignore it). Use LLVM 22.1.8, which matches LeviLamina's own CI.
- Browser-pane downloads left two stray `.tmp` files in Downloads (20 MB each, safe to delete).
