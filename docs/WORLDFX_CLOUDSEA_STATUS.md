# Cloud Sea (standalone Hive atmosphere): status

Branches: BF `worldfx-cloudsea/bf` (pack generator, shaders, pack), RF `worldfx/rf` (sky, cloud sea, world pass).
Nothing is merged into LyricsForge or main until every standalone look is finished (Fernando, 2026-11-10).

## What the atmosphere is made of
| part | where | state |
|---|---|---|
| sunset sky + high clouds + cloud sea | RF `shaders/cloud_sea*.hlsl`, `SkyPass.cpp` | in game; square-edge bug fixed and verified |
| sea only on sky pixels, local-top skip (perf) | RF 6f32e3c | built, installed, **not yet seen in game** |
| shadows / AO / rim world pass | RF `shaders/worldfx_pass.hlsl` | in game |
| block lighting per material | BF `shaders/src/CloudSea` (RenderChunk) | in game |
| 118 block textures | `tools/WorldFx/atmos/cloudsea.py` -> `packs/WorldFxCloudSea` | built, installed, **not yet seen in game** |
| beds knitted like the team wool | `atmos/beds.py` | built, **not yet seen in game** |
| 3D swords / pickaxes / axes, 6 tiers each + icons | `atmos/equip.py` | built, **placement not yet seen in game** |
| armor repaint (iron, gold, diamond, netherite, leather) | `atmos/armor.py` | built, **not yet seen in game** |
| metal/crystal equipment lighting (material id in alpha) | `CloudSea/Actor/AlphaTest.Fragment.hlsl` | compiled, **not yet seen in game** |
| enchant glint as pale warm light | `CloudSea/ActorGlint/AlphaTest.Fragment.hlsl` | compiled, **not yet seen in game** |

Build everything: `python3 -I tools/WorldFx/atmos/build_pack.py cloudsea --sheet review.png`;
shaders: `tools/ShaderForge/build.py <materials> shaders/src/CloudSea -o out`.
Offline review renders: `atmos/diorama.py` (island), `atmos/preview3d.py` (models).

## On the PC (LeviLauncher instance)
- Global packs: `Cloud Sea by Novak` (c10d5ea0...) above `World FX Materials` (9453eb52...).
- Shader pack: `CloudSea`; RF config `WorldFx.txt`: `cloudsea 1`.
- `%APPDATA%\levilauncher.exe\worldfx\`: `Switch to LyricsForge.bat` / `Switch to Cloud Sea.bat` (close the game
  first), the two RF builds, and the pack zip.

## Open, in order
1. In game: equipment placement (first and third person), wool, beds, armor, glint, perf of the new sea path.
2. Art reviews 2 and 3 on in-game frames; fix what fails.
3. Hive test (global packs on Hive maps; the cloud sea needs the vanilla clouds setting on).
4. Glass refraction (RF), hit particles, the second atmosphere (Novak Glass) through the same formula.

## Game-aware World FX (2026-11-10) - what no pack can do
A pack can only key its look on time of day and biome. Bedrock Forge knows the match, so the atmosphere reacts to it.
BF `src/client/MatchFx.cpp` exports `BF_GetMatchFx` (phase, beds + team colours, nearby block events); RF reads it
every frame (SkyPass `matchFrame`, cbuffer b1 of worldfx_pass.hlsl).
- **Living match sky** - golden hour at the start, dusk after the first bed breaks, moonlit night with stars when one
  bed (or none) is left. The game's own time of day follows (vanilla lighting / torches). Verified in game.
- **Team beacons + island light** - every bed throws a column of its team colour; a broken bed's beacon flares,
  gutters and goes out. Verified in game (single player, red beds).
- **Block shards** - a broken block bursts into 8 pieces of its colour with a pixel grain, lands on what was under it,
  settles; over the void they fall away. Verified in game.
- Offline sky review: `tools/skypreview/preview.js` in RF renders the real HLSL at any phase.
- Not yet checked: The Hive (chat fallback, many team colours, real bridges), placement pop, perf in a full match.
