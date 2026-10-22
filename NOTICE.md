# Notice

**Renderer Foundation** is a modified version ("fork") of **BetterRenderDragon**.

- Original work: BetterRenderDragon, Copyright (C) ddf8196 and the BetterRenderDragon contributors
  (DDF/ddf8196, ENDERMANYK, Shrey, Arctrometa, jqms and others - see the git history of this branch).
  Upstream: https://github.com/ddf8196/BetterRenderDragon
- Modifications: Copyright (C) 2026 Novak (Bedrock Forge project).

Both are licensed under the **GNU Affero General Public License v3** (see `LICENSE`). The full upstream history
is kept in this branch. If you distribute this mod (or let people use a modified version over a network),
you must also offer its complete source code under the same licence.

## What changed from BetterRenderDragon v1.4.4 (2026-10-21)
- Rebuilt as a **LeviLamina client mod** for Minecraft Bedrock **1.26.51.01** (GDK/Win32) instead of a UWP-era
  injected DLL. Game functions come from LeviLamina's symbol table instead of per-version byte signatures.
- **MaterialBinLoader**: the 1.20/1.21 `readFile` signature hook is replaced by a version-independent file-open
  redirect; resource-pack materials are still fetched through `ResourcePackManager::load` like upstream.
  New: shaderpack folders (`mods/RendererFoundation/shaderpacks/<pack>`) selectable in the overlay.
- **Reload shaders**: upstream's frame-discard + shader-blob free used 1.20/1.21 struct offsets; the 1.26 port uses
  `MinecraftGame::cleanReloadMaterials` (experimental).
- **Vanilla2Deferred / Force Deferred Technical Preview** removed: on 1.26 the deferred renderer ships as
  Vibrant Visuals, so the overlay switches the real graphics mode (only where the game allows it).
- **Overlay**: ImGui via a throw-away D3D12 probe (works even if the swapchain already exists), Present + Present1
  hooks, D3D12 queue captured from `ExecuteCommandLists`, Win32 input instead of WinRT. Options live in the mod's
  config folder (`config/RendererFoundation.json`).
- Removed bundled Detours / ImGui / GSL copies (xmake packages instead).
