# Research Index — Project Bedrock Forge

Snapshot: 2026-10-20. Shallow clones; commit hashes recorded below.
Evidence labels: **[Source]** verified in source · **[Docs]** documented but untested · **[Theory]** inference.

## Headline conclusions

1. **Target version: Minecraft Bedrock 1.26.51.01 (client) + LeviLamina Client 26.51.x.** This is the only combo where both LeviLamina and Lamium line up. [Source: LeviLamina tooth.json client variant → bedrock-runtime-data 26.51.1-client.11; Lamium README + VALIDATION.md baseline]
2. **LeviLamina is confirmed as the native foundation.** Active (commit 2026-10-19, release 26.51.7 on 2026-10-12), has a dedicated `src-client` tree, LGPL-3.0, official mod template + package manager. [Source]
3. **Lamium is the best client reference.** It already does HUD, camera, overlays, keybinds, world-space rendering on LeviLamina Client — exactly our Prototype 2/3 territory. LGPL-3.0. [Source]
4. **LeviLauncher handles isolation.** Separate instances per version, GDK + UWP, auto-installs LeviLamina on supported GDK versions. GPL-3.0 (we only use it as a tool). [Docs]
5. **Amethyst is NOT a viable base right now.** Targets 1.26.42 (not 1.26.51), and for 1.26.x the author made it mixed/closed-source ("bring your own types"). Reference only. [Source]
6. **BetterRenderDragon is stale.** Last commit 2024-09-09, claims 1.20.30+. Do not expect it to run on 1.26.51. Useful only for studying RenderDragon/shader-loading concepts, and it's AGPL-3.0 — read, don't copy. [Source]
7. **Universal Java .jar execution is not realistic.** Confirmed direction: native SDK first, semi-automated porting tools later (see roadmap). [Theory]

## A. LeviLamina — `LiteLDev/LeviLamina` @ 4ca13569 (2026-10-19)
- Version 26.51.7 (2026-10-12). Server variant pins BDS 1.26.51; client variant pins bedrock-runtime-data `26.51.1-client.11`.
- Layout: `src/ll/api` (base, chrono, command, coro, data, event, form, i18n, io, memory, mod, network, reflection, service, thread, ui, utils), `src-client` (client engine headers incl. ClientInstance, FrameBuilder), `src-server`.
- Hooks via `LL_TYPE_INSTANCE_HOOK` macros. Event system in `ll/api/event`. Mod manifests = `manifest.json`; packages = `tooth.json` via **lip**.
- Depends on **PreLoader** and **PeEditor** — closed-source, under LeviMC EULA: no reverse engineering of those two. Fine for our use.
- Install on client: LeviLauncher → install lip → install supported version → Mods → "Install LeviLamina". Or `lip install github.com/LiteLDev/LeviLamina#client@26.51.7`. [Docs]
- Open: which client events exist out of the box vs need custom hooks; unload/hot-reload behavior. → verify in Prototype 1.

## B. Lamium — `amatouhake/Lamium` @ f94e0b9 (2026-10-20)
- v0.2.3, early. Supported: MC 1.26.51.01, LeviLamina Client 26.51.x, Win x64.
- Source areas: `app` (runtime, capability checks, versions), `features` (camera, map, schematic, visuals, lighting, inventory…), `input` (custom keybinds), `overlay` (world-space meshes, chunk borders, hitboxes), `ui`, `settings`.
- HUD drawing goes through `MinecraftUIRenderContext` / `ScreenContext` / `Tessellator` hooks (235 / 103 / 116 references). This is our path for Prototype 2. [Source]
- Good pattern to copy *conceptually*: per-feature "capability" gating — features auto-pause on unverified game versions instead of crashing.
- Has its own validation log discipline (VALIDATION.md) — we mirror that in EXPERIMENT_LOG.md.

## C. LeviLauncher — `LiteLDev/LeviLauncher` @ 64f5878 (2026-10-19)
- Go + Wails desktop app. GDK and UWP. Instances isolated; Release + Preview.
- Needs: Microsoft Gaming Services + GameInput (GDK); Dev Mode for UWP. Checks/offers VC++ runtime.
- Native loader x64 only. Auto LeviLamina install limited to supported GDK versions.

## D. Amethyst — `FrederoxDev/Amethyst` @ e9179a6 (2026-10-16)
- Runtime 2.2.9, game build 1.26.42. Apache-2.0 for the open parts. 1.21.x line fully open.
- Verdict: cannot co-target 1.26.51 with LeviLamina; do not load both. Reference for alternative mod-registration design only.

## E. BetterRenderDragon — `ddf8196/BetterRenderDragon` @ fc4e663 (2024-09-09)
- Features: deferred rendering toggle, disable RT, load RenderDragon shaders from resource packs, ImGui menu (F6). Launch via .bat injector.
- Verdict: stale for 1.26.x; AGPL-3.0. Concepts only. Our post-processing (impact frames, grain, VHS) must be researched fresh against 1.26.51 — LeviLamina's `FrameBuilder` header is the first lead. [Theory]

## F. Blueberry Client
- Waiting on executable. Proprietary — public docs only; no RE. Non-executing metadata inspection only after license check.

## G. Template — `LiteLDev/levilamina-mod-template` @ 3df96f2 (2026-10-18)
- CC0. Build: `xmake f -y -p windows -a x64 -m release --target_type=client` then `xmake`. Output in `bin/`.
- Toolchain: **clang-cl** (LLVM) + MSVC/Windows SDK, C++20, `/EHa`, runtime MD. Template's tooth.json still says LeviLamina 26.20.* — bump to 26.51.* for us.
