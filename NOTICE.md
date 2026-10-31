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

## Spatial GUI + Cinematic Interactions (ported into this mod)
The in-world container screens (`src/SpatialPanel.cpp`, `src/SpatialCamera.cpp`) and the station scenes
(`src/Cinematic.cpp`, `src/CinematicGame.cpp`, `src/HammerModel.h`) are C++ ports of two Java Edition mods. Their
code is used under the MIT licence; the notices below are kept as those licences require.

### Spatial GUI
- Original work: **Spatial GUI** by **tastytrash** - https://github.com/tastytrash/Spatial-GUI
- Licence: MIT License, Copyright (c) 2026 tastytrash.

### Cinematic Interactions
- Original work: **Cinematic Interactions** by **Southware** - https://github.com/dcvke/CinematicInteractions
- Licence: MIT License, Copyright (c) 2026 Southware.

### "Just a Hammer" (the anvil scene's hammer, `src/HammerModel.h`)
- Creator: **RDiamonds** - https://sketchfab.com/3d-models/just-a-hammer-0b069177ed664af1b8efec018bc55cee
- Licence: Creative Commons Attribution 4.0 International (CC BY 4.0) - https://creativecommons.org/licenses/by/4.0/
- Changes: Cinematic Interactions converted the glTF geometry to Minecraft JSON, remapped the UVs, renamed the
  textures and adjusted scale and grip; this port converts that JSON model and its textures into a C++ header
  (`tools/gen_hammer.py`). The source textures are unchanged. No endorsement by the creator is implied.

### MIT License (applies to the Spatial GUI and Cinematic Interactions code named above)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Block textures shown in the scenes (the grindstone's wheel, the stonecutter's blade) are read at runtime from the
game's own installed resource pack; none are redistributed here. Minecraft is a trademark of Mojang Studios / Microsoft.

## LyricsFlow (world-space lyrics) - personal-use port
`src/LyricsPass.*`, `src/LyricsCamera.*`, `shaders/glyph.hlsl` draw the lyric text Bedrock Forge builds from its port of
**LyricsFlow v2.0 by 374m** (MIT; modification permitted for Fernando's personal use only, not distributed). The RF
code here is original; see Bedrock Forge `third_party/lyricsflow/NOTICE.md` for the full credit and licences.

