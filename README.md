# Bedrock Forge

A Fabric/Forge-style modding framework for Minecraft Bedrock Edition, built on [LeviLamina](https://github.com/LiteLDev/LeviLamina).

**Target:** Minecraft Bedrock 1.26.51.01 (GDK, Windows x64) + LeviLamina Client 26.51.x

## What's in v0.1 (`BedrockForgeCore`)
| Prototype | Feature | Status |
|---|---|---|
| 1 | Startup log, join/exit/tick event callbacks, clean disable | Code written; not yet verified in game |
| 2 | Custom HUD box (title, FPS, tick count), toggled with **J** (rebindable in Controls) | Code written; not yet verified in game |
| 3 | Impact frames when YOU hit a mob: white flash → ink frame + speed lines → vignette fade + impact sound. **K** toggles. Tunable in config.json | Code written; not yet verified in game |

SDK v0 (`src/bf/`): `Events.h` (RAII listeners), `Input.h` (keybinds), `Config.h` (JSON config), `Hud.h` (HUD drawing).

## Build
GitHub Actions builds it on every push. Download the artifact from the Actions tab.

Local: `xmake repo -u && xmake f -a x64 -m release -p windows -y && xmake`. Output lands in `bin/`.

## Install (test instance only)
Copy `bin/BedrockForgeCore/` into `<LeviLauncher instance>/mods/`. Logs go to the LeviLamina console and `logs/`.

## Docs
See `docs/`: PROJECT_STATUS, RESEARCH_INDEX, ARCHITECTURE, LICENSE_AUDIT, and the rest.

License: TBD (see docs/LICENSE_AUDIT.md). The template base is CC0. The HUD drawing approach was learned from Lamium (LGPL-3.0) and re-implemented here, not copied.
