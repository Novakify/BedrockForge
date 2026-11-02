# LyricsFlow / LyricsForge sky in Vibrant Visuals (research + design, 2026-11-01)

## How Vibrant Visuals draws its sky (MC 1.26.51, from data/renderer/materials)
- Fancy mode has a sky *draw* (`Sky.material.bin`, pass Opaque) that stage 2 follows. Vibrant Visuals (deferred) has none:
  `DeferredShading.material.bin` pass `DoDeferredShading` (4 variants: GPUBlockLighting x PointLightShading, + Fallback)
  is a fullscreen lighting pass whose uniforms include SkyZenithColor / SkyHorizonColor / AtmosphericScattering /
  SunDir / MoonDir / FogSkyBlend and which reads `SceneDepth` (t5 / s5, from PSV0 + the material's sampler table):
  pixels with nothing in the depth buffer get the atmospheric-scattering sky there.
- After it, the forward transparent passes draw the sky objects into the same HDR target with the scene depth bound:
  `StarsForwardPBR` / `SunMoonForwardPBR` / `CloudsForwardPBR`, pass `ForwardPBRTransparent` (main view) and
  `ForwardPBRTransparentSkyProbe` (the reflection probe). Clouds also have `DepthOnlyAlphaTest` and `MotionOnly`.
- The mixed-resolution lighting path (`DeferredMixedResolution*`, `DeferredTiledMixedRes`) uses the same sky inputs.

## What RF does (SkyPass.cpp, UiCapture.cpp)
- Tags the main-view star / sun-moon pipelines (kind `VvSky`) and the main-view cloud pipeline (kind `Clouds`) by the
  FNV-1a 64 of their pixel shader, like the Fancy hooks (hashes: `tools/vv_sky_hashes.py`; it reproduces the Fancy
  ones exactly).
- At the first `VvSky` draw of each frame it draws the live sky dome / sky quads / "My picture" on the game's root
  signature (no resources, inputs through the vertex buffer) with the star pass's DSV + RT formats and a depth test to
  the far plane (reversed Z when the pass compares GREATER), so only pixels with nothing in front change. Vanilla
  sun / moon / stars are skipped. Weather particles keep using the cloud draw (depth-tested there).
- Colours go sRGB -> linear and get multiplied by an HDR gain (F6 > "VV sky brightness"), because the target is linear
  HDR (pre-exposed). The gain needs calibrating in game.

## Still vanilla (known)
- Reflections / the sky probe (`SkyProbeDeferredShading`, `*SkyProbe` passes), the fog that blends terrain toward the
  sky colour, volumetric light. Possible next steps: hook the SkyProbe passes the same way; tint fog via the VV
  atmospherics JSON.

## Test plan (needs the game)
1. Graphics mode Vibrant Visuals, a LyricsForge sky on, F6 > LyricsFlow sky: "Stage 2" should say
   "Vibrant Visuals: drawing in place of Bedrock's sky". If it says the star pass has no depth: trigger from the
   cloud draw instead.
2. Tune "VV sky brightness" until the sky sits right next to the terrain at noon, dusk and night; bake the default.
3. Check weather particles, My picture, underground (no sky pixels), water reflections (vanilla, expected).

## Update-proof hashes (learned at boot)

RF no longer depends only on the hard-coded 1.26.51 table. At boot `materials::install` calls
`rf::sky::learnGameMaterials(<game>/data/renderer/materials)`, which scans Sky / Stars / SunMoon / Clouds and their
ForwardPBR versions with `src/SkyMaterialScan.h` (the same rules as `tools/vv_sky_hashes.py`) and learns their pixel
shader hashes. When a resource pack or shaderpack serves one of those materials, the served file is scanned too.
`kindOfHash` checks the learned set first and falls back to the old table. F6 > LyricsFlow sky shows how many were
learned and how many game pipelines were tagged, and warns when no Vibrant Visuals sky shaders were found.

Check on Linux: `g++ -std=c++20 -Isrc tools/sky_scan_test.cpp -o sst && ./sst <materials dir>` (1.26.51: 14 shaders,
identical to the table).
