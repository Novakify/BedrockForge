# World FX (Bedrock Forge + Renderer Foundation) - plan

Status 2026-11-09: **M0 built, not yet run in game.** RF branch `worldfx/rf` (CI -> `worldfx-rf-builds`), tag test
pack `packs/WorldFxTagTest`, analyzer `tools/WorldFx/`. Next: run the probe at the PC (checklist below), then M1.

## What Fernando wants (2026-11-09)
- His **Hive Bed Wars / Sky Wars games should look crazy, unique and unrecognizable** - things no Bedrock mod has
  done. It gets a much bigger reaction when it works on a server than in single player.
- Agreed start: a tag test, then **cel outlines + refraction glass**.
- His architecture idea: a **resource pack as the foundation** (it tags blocks and gives the base look) with RF's
  rendering built on top. That is exactly the plan below.

## Rules for Hive
- **Draw, don't decide.** Hive owns block placement, positions and collision. Everything here only changes how
  *our* client draws the world. Things that would change game state get faked visually instead:
  - portals: windows into a made-up scene, never a live view of another part of the map
  - bigger-on-the-inside: fake depth (bottomless pits, endless mirrors) while the floor you stand on stays real
  - physics: cosmetic debris only, like LyricsForge's falling letters
- **No information advantage.** Nothing shows what the player couldn't see anyway: no see-through walls, no
  remote camera views (that is freecam / ESP). Glass only bends what is already visible through it. Outlines come
  from the visible depth buffer only.
- No strobing or flicker. Team colours stay readable. Nothing covers the crosshair area in fights.
- Fancy first, then Vibrant Visuals. FPS check on the RTX 4060.
- Blockers outside this feature (same as Glass Armor): Blueberry + LeviLauncher mods crash together; Hive's OK on
  visual DLLs (Fernando planned to confirm on Hive's Discord).

## How it draws (design)
Same proven pattern as Glass Armor: **inside the game's frame, on the game's own draws** (UiCapture hooks).
1. **World -> UI boundary pass.** Right before the first UI draw of a frame, copy the world's colour + depth and
   run full-screen passes (outlines, void lens, heat haze ...) into the frame. The HUD draws after, untouched.
   Lyrics / LyricsForge FX are drawn later over the finished frame and stay on top too.
2. **Per-block effects on the game's RenderChunk draws.** The game's own vertex shader + our pixel shader, the
   game's root signature + one extra table (scene, depth, params, tag rectangles) - like Glass Armor does on Actor
   draws. Exact geometry, correct occlusion, the rest of the frame untouched.
3. **Knowing what is what.** All blocks share one terrain atlas, so a block is recognised by **where its UV lands
   in the atlas**. RF learns the rectangles once (atlas readback) and gives the pixel shader a small table:
   - **tags (Fernando's pack idea):** the World FX pack's textures carry invisible WFX1 stamps (low 3 bits of 4 texels
     per corner, max 7/255 colour change). Works under any texture pack placed below ours, HD packs too.
   - **vanilla match:** the analyzer (and later RF) also finds unmodified vanilla glass by content - the fallback if
     Hive's server pack replaces our textures.
   - beds and other block entities are Actor draws: Glass Armor's texture recognition handles those.

## The effect menu (mapped to Bed Wars / Sky Wars)
| Effect | What you see | How | Where it shines | Milestone |
|---|---|---|---|---|
| **Anime cel world** | ink outlines on every block edge + cel light bands, Blue Lock episode look | boundary pass, depth edges | everywhere | M1 |
| **Refraction glass** | blast-proof glass and cage glass actually bend what's behind, chromatic fringe at the edges | RenderChunk glass draws + scene copy | Bed Wars glass, Sky Wars cages | M2 |
| **Black hole void** | the void under Sky Wars islands lenses like an event horizon | boundary pass (sky / void pixels + depth) | Sky Wars | M3 |
| **Gen heat haze** | air shimmers above diamond / emerald gens | boundary pass around tagged gen blocks | Bed Wars | M3 |
| Team neon edges | team wool / concrete edges glow in the team colour | RenderChunk opaque PS on tagged team blocks | bridges, bed defence | M4 |
| God rays + caustics | light beams and moving light patterns through glass | boundary pass + glass rects | lobbies, cages | M4 |
| Cosmetic bed shatter 🧪 | a broken bed bursts into tumbling pieces | block update seen by the client -> debris (LyricsForge letter physics) | Bed Wars | later |
| Fake windows 🧪 | panes that show a made-up scene (space, the Nether) | glass rects + our own sky render | lobbies | later |

## M0: the probe (built 2026-11-09)
RF `worldfx/rf` (from the live RF line `lyricsforge-cinematic/rf`), `src/WorldFx.{h,cpp}`:
F6 > **World FX** > **Probe the next frame** (needs `"uiCaptureSpike": true`, already on for Glass Armor).
2 warm-up frames for the trackers, 1 recorded frame, then files in `mods/RendererFoundation/logs/worldfx/<time>/`:
- `report.txt`: every RenderChunk pipeline (pixel / vertex shader names, **vertex shader output signature** = what
  a replacement pixel shader must accept, targets, blend, depth, draws), totals per material, textures bound by
  terrain draws, colour / depth targets of terrain draws, the world -> UI boundary.
- `colour_at_ui` / `world_colour` / `world_depth` `.raw + .json + .bmp`: the world right before the HUD + its depth.
- `atlasN` `.raw + .json + .bmp`: the top 3 atlas candidates (mip 0, up to 4 array slices), `stamps.json`: WFX1 hits.
Safety: inert until the button is pressed; game render targets are never held after the copy (a held swap-chain
buffer would make the game's window resize fail); stale tracker pointers are only touched under SEH.

Analyzer (no game needed): `python3 -I tools/WorldFx/analyze_probe.py <probe folder>` -> `analysis/verdict.md`
with M1 / M2 go / no-go, PNGs, `tags.json` (atlas rectangle per tagged texture) and **`preview_outlines.png` /
`preview_cel.png` = what M1's cel look would do to that exact frame** (a Hive match frame is the real test).

Checked in the cloud (2026-11-09): `tools/WorldFx/test_stamps.py --cpp <rf>/src/WorldFx.cpp` - all 72 textures
carry their id, a fake atlas with edge padding finds all 72 rectangles exactly with no stray hits, BGRA order works,
and the C++ `findStamps` compiled standalone finds byte-for-byte the same stamps as the Python port.
`tools/WorldFx/fake_probe.py` writes a fake probe (ray-cast Bed Wars island with the real textures) that the
analyzer reads end to end. Lesson from it for M1: depth outlines turn every cutout glass frame into ink - M1 skips
edges whose near side is a tagged glass texel.

What M0 must answer in game:
1. Which RenderChunk pipelines draw glass (plain glass is cutout = AlphaTest? stained = Transparent?), and does
   their vertex shader hand the atlas UV (TEXCOORD) to the pixel shader?
2. Is the atlas plain RGBA8 (stamps readable) or compressed? One texture or an array? Where do our stamps land?
3. Does the game keep RGB exact under alpha < 255 (soft stamps on stained glass), or premultiply (hard stamps only)?
4. At the first UI draw: is the world's depth intact and the same size as the colour (render scale)? Standard or
   reversed Z? Is the UI drawn into the same target as the world?
5. On Hive: same answers in a lobby and a Bed Wars match; does Hive's pack replace glass / wool textures?

## M0 test checklist (at the PC, Fancy first)
1. Game closed. Back up `<instance>\mods\RendererFoundation\RendererFoundation.dll`, copy the one from branch
   `worldfx-rf-builds` over it (it is the live RF line + the probe, nothing else changes).
2. Copy `packs/WorldFxTagTest` into the instance's `resource_packs` and turn it on as a **global** resource pack,
   at the top. It looks exactly like vanilla.
3. Test world: stand where plain glass, stained glass, panes and wool are in view, HUD on. Esc > F6 > World FX >
   "Probe the next frame". Wait for "done - report written" (a second or two).
4. Hive: once in the lobby, once in a Bed Wars match with glass in view (spectating is fine). Then once on VV.
5. Run the analyzer on each folder; read `verdict.md`, look at `preview_cel.png`.
6. Afterwards: put the backed-up DLL back, or merge `worldfx/rf` into the live RF line (the probe is inert).

## M1 plan: cel outlines (after M0)
- Boundary pass in RF (`WorldFx`): copy colour + depth at the first UI draw (M0 proves the copy), full-screen PS:
  depth silhouettes + creases (relative to linear depth so far edges stay thin), ink colour slightly tinted
  (not pure black), optional cel bands + saturation on solid pixels only, sky untouched.
- Skip edges on tagged glass texels (cutout frames), thinner outlines far away, width scales with resolution.
- Toggle + strength in the BF client menu Visual page (same glass sliders as the rest), F6 mirror for debugging.
- Then M2 refraction glass on the RenderChunk glass draws (Glass Armor's flush + extended root signature).

## Files
- `tools/WorldFx/wfx_stamps.py` - WFX1 stamp format (stamp / find / pair into rectangles), mirrors WorldFx.cpp
- `tools/WorldFx/make_tag_pack.py` - builds `packs/WorldFxTagTest` (72 vanilla textures: glass, panes, team wool,
  team concrete, oak planks, end stone, obsidian, diamond / emerald block) + `tag_ids.json`
- `tools/WorldFx/test_stamps.py` - self-test (+ `--cpp` parity check against RF's C++)
- `tools/WorldFx/analyze_probe.py` - probe folder -> verdict, previews, tags.json
- `tools/WorldFx/fake_probe.py` - fake probe for testing the analyzer
- RF `src/WorldFx.{h,cpp}` on `worldfx/rf`
