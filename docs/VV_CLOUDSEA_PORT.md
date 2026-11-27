# Cloud Sea -> Vibrant Visuals port: where it stands (paused Nov 26 2026, 23:40)

Branches (all pushed, nothing merged into the live lines yet): RF `cloudsea-vv/rf` (builds: `cloudsea-vv-rf-builds`),
BF `cloudsea-vv/bf`. Test build kept on the PC as `mods/RendererFoundation/RendererFoundation.cloudsea-vv-9099019.dll`
(the installed DLL is back to cc25d38). Design notes: RF `docs/VV_LIVE.md`.

## Seen working in game (VV, test world)
- Cloud Sea sky in VV: gradient, sun, high clouds, cloud sea, moon, stars. 120 FPS.
- Sun / moon stand at the game's own compass directions (read from VV's uniforms), so VV's light fits the sky.
- VV Live patches all three groups every frame (0 missed): direct/ambient light, sky colours (lighting, sky probe,
  water), fog volume. Table of 74 shaders generated from the game's materials (`tools/gen_vvlive_table.py`).
- Match layer over VV's HDR frame: team beacon, Figura bed, bed break + shockwave, kill animation (FPS dips to ~90-107
  while one plays).
- Pack layer loads with no content-log errors: atmospherics, lighting, fog, grade, 83 biomes, 266 block texture sets,
  38 equipment texture sets. Height maps visibly shade end stone / planks.
- The cloud sea now covers ground below the deck (build 9099019).

## Not right yet
1. **Brightness.** VV pre-exposes its frame: `colour * (0.18 / lastFrameAverageLuminance + 1e-4)` (read off the
   DeferredShading shader). A fixed sky gain is right at one exposure only: golden hour OK at 0.3, dusk too dark,
   night a blue whiteout. Fix written and pushed (commit after 9099019, "VV exposure probe"): one pixel drawn on the
   game's root signature at its sun/star draw copies the 1x1 luminance texture (t0 there) into a texture of RF's; the
   sky and the match layer multiply by the same factor. **Never run in game yet.** `sky_gain` then becomes an absolute
   scale and needs one calibration (start around 1-5).
2. Not verified by eye: the crit impact frame (0.25 s, too fast for my screenshots), the fog deck from inside,
   bed-break air pulse, final-fight rising clouds, equipment texture sets, wool "cloud" look up close.
3. White "+" flakes in the air in VV (most likely the Cloud Sea pack's own spark particles drawn unlit) - check.
4. Not ported: haze / mist / ambience, motion blur, the menu panorama grab in VV (they read an 8-bit frame).
5. Art-director passes on real frames (none done yet), then merge both branches into the live lines.

## Facts learned today (corrections to VV_TESTED.md)
- Fog `max_density` is capped at 1.0 by the schema (1.6 invalidated the whole fog file). Thickness comes from the
  scattering coefficients. The earlier "a deck needs 1-3" was wrong.
- Fog heights in the pack are plain world y; the engine's HeightFogScaleBias = (-1/T, (zero - cameraY)/T).
- The game lights the world with the DeferredMixedResolution family, not DeferredShading.
- SetComputeRootConstantBufferView is command-list vtable slot 37.
- Auto-exposure: average of the brightest 20% of pixels, log-luminance clamped to [-3, 4] (data/renderer).
- BF's Sky module (modules.json sky.mode: 0 Living Skies, 1 Cloud Sea, 2 Novak Glass) picks the atmosphere; WorldFx.txt
  `cloudsea` alone gets overwritten.
- Test without the F6 panel: write one line into `mods/RendererFoundation/config/Test.txt`:
  `phase 0|1|2`, `bed`, `kill`, `impact`, `vv` (status into the log), `set <key> <value>` (VvLive.txt keys).

## To resume
Game closed -> `~/inst.sh` in the device VM (DLL from cloudsea-vv-rf-builds + pack into the test world) ->
options.txt graphics_mode 2, modules.json sky.mode 1, world_resource_packs.json with pack c10d5ea0... v0.3.0 ->
launch, world "test" (BFKnivesTest), platforms at 8..24, 226, -84..-58 with the material board at z -70.
