# Cloud Sea in Vibrant Visuals + VV Live (2026-11-26)

Two parts, both on by default once the Cloud Sea is the World FX sky and the game is in Vibrant Visuals.

## 1. The Cloud Sea sky in VV (SkyPass.cpp, shaders/cloud_sea.hlsl `PSCloudSeaVV`)
- Before: `drawWorldSky` gave up in VV ("draws on the classic renderer for now").
- Now: at the first vanilla sun / moon / star draw of the frame (pass `ForwardPBRTransparent`, the scene depth bound)
  RF draws the whole Cloud Sea sky (`csBackground(rd, true)`: gradient, sun, high clouds, moon, stars, the cloud sea)
  in linear light into VV's HDR target, depth-tested at the far plane (only sky pixels). VV tonemaps it with the frame.
- Brightness: F6 > Vibrant Visuals Live > "Cloud Sea sky brightness (VV)" (`sky_gain` in config/VvLive.txt).
  Needs calibrating in game against the terrain at golden hour, dusk and night.
- Inside the cloud deck the sky turns to the deck's lit colour, so falling into it matches VV's fog on the geometry.
- Root constants of the World FX sky: 20 -> 24 (c5 = far depth, HDR gain, VV flag, top of the deck's fog).
- Classic-only still: the World FX pass (block shadows / AO / rim, beacons, block shards, kill scenes). It needs an
  8-bit target + D24S8 depth; VV's frame is HDR with its own depth. Next step: a VV variant of that pass.

## 2. VV Live (src/VvLive.cpp)
Resource packs key VV only on time of day and biome. VV Live rewrites a few of VV's own uniforms in place, right
before the game records the pass, so VV's lighting, reflections and fog follow RF's sky and the match.

| pass (material / pass) | how RF finds it | what changes |
|---|---|---|
| DeferredShading / DoDeferredShading (fragment) | PS hash learned from the .material.bin | SkyZenith / SkyHorizon colour hue -> the Cloud Sea's (the sky's light on the world, distance haze), Sun / Moon colour, key light colour (+ a bed's team colour for a moment) |
| SkyProbeDeferredShading / DoDeferredShading | same | the same sky colours: reflections + image-based light (the sky lights the world) |
| PopulateVolume / Populate (compute) | CS hash, CreateComputePipelineState hook | AirAlbedoExtinction (fog colour, deck density), HeightFogScaleBias (the deck locked to world height under the islands), key light |

- Hue only: VV keeps its own brightness and time of day (`hueTo` keeps luminance), so pack values and BF's
  match-driven clock still decide how bright things are.
- Byte offsets are bgfx's uniform table (`reg_index`) for MC 1.26.51 (`kLayout` in VvLive.cpp). Before every patch
  RF checks the buffer really is that pass's (rotation matrices orthonormal, light directions unit length) and
  searches the bound root CBVs (+0..512 bytes) if not - a game update that moves things gets "missed", not garbage.
- Where the bytes are: a Map/Unmap hook on the game's device keeps GPU address -> CPU pointer for mapped upload
  buffers (>= 4 KB). Writes are SEH-guarded.
- Height fog: density = AirAlbedoExtinction.w * saturate(HeightFogScaleBias.x * y + .y), y camera-relative (read off
  the shader). RF sets scale = -1/T, bias = (top - cameraY)/T: nothing above the deck's top, full T blocks below.
- The deck's top: the Cloud Sea's sea base (lowest bed - 55) + 28. The final fight (one bed / team left) eases the
  clouds up by `final_rise` blocks over ~8 s and thickens them.
- Bed break: the air (fog colour, +60% density) and the key light take that team's colour, fading over ~3 s.
- Off when: Lab `vvtest`, no RF sky drew for 0.5 s, Novak Glass (not yet), F6 off.

## F6 > Vibrant Visuals Live
Sliders for every value above (saved to config/VvLive.txt) and a status line per pass: shaders learned, pipelines
tagged, patches done / missed and why. First checks in game: all three show "pipelines" > 0 and "patched" climbing.
Test without a match: F6 sky phase preview (golden -> dusk -> night) and F6 test bed (a bed breaks after 23 s).
