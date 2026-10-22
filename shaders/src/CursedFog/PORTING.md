# Cursed Fog for Bedrock (Bedrock Forge shaderpack)

Port of **Cursed Fog V1.0.10 by LoLip_p** (https://modrinth.com/shader/cursed-fog), licensed
**CC BY 4.0** (https://creativecommons.org/licenses/by/4.0/). All look and maths are LoLip_p's; this folder
translates them to RenderDragon (HLSL, built with tools/ShaderForge). Changes are listed below.

## What maps where
| Java program (world0) | Bedrock material / pass | Status |
|---|---|---|
| gbuffers_terrain + composite (terrain branch) + composite1 + final | RenderChunk Opaque / AlphaTest / Transparent | ported 1:1 (formulas copied) |
| gbuffers_entities + composite (entity branch) + composite1 + final | Actor Opaque / AlphaTest (plain variants) | ported; light levels approximated |
| gbuffers_skybasic + final | Sky | ported 1:1 |
| gbuffers_clouds (CLOUDS 0 = hidden) | Clouds | ported (discard) |
| gbuffers_skytextured (sun/moon x (1+state)), stars | SunMoon / Stars | not yet (materials not staged) |
| gbuffers_water (waves) | RenderChunk Transparent | uses the terrain path, no waves yet |
| composite2 SSR, composite3 DOF/auto-exposure | - | off in the pack's defaults |
| composite4 motion blur (on by default) | needs the previous frame | Renderer Foundation full-screen pass (todo) |
| final: chromatic aberration, interlaced scan | need neighbouring pixels / camera motion | Renderer Foundation full-screen pass (todo) |
| final: gamma, black->purple, VHS colour noise, black stripes, CRT mask, vignette, grain, ACES, saturation 0.45, contrast 0.95 | every ported material (per pixel, using bgfx u_viewRect) | ported 1:1 |
| world-1 (Nether), world1 (End) | - | todo |

## Bedrock substitutes (Java data that RenderDragon materials don't have)
- **worldTime -> day state**: smoothTransition(worldTime) is replaced by the day level the game already applies:
  the sky column of the lightmap texture (terrain) / the fog colour (sky, entities). Same 0..1 meaning; ramps follow
  Bedrock's sunrise/sunset instead of ticks 0-1000 / 12000-13000.
- **Shadow map (shadow pass, GetShadow)**: Bedrock Fancy has no shadow pass; sunlit = full sky exposure
  (sky light 14-15). Proper shadows need a shadow pass (Renderer Foundation / Vibrant Visuals).
- **Sun direction**: LightWorldSpaceDirection when the game sets it, else a fixed sun with the pack's -45 deg tilt.
- **Normals**: flat face normal from screen-space derivatives of the world position (= Java's block-face Normal).
- **rainStrength**: 0 for now (Bedrock materials have no rain level).
- **Entity light**: Bedrock bakes block+sky light into one colour (TileLightColor); the sky level is taken from it.
- **frameTimeCounter**: ViewPositionAndTime.w on terrain; 0 on sky/entities (their grain doesn't animate yet).
