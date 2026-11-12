# Arke study + IrisBridge plan (ShaderForge upgrade)

Studied 2026-11-12. Source: github.com/LiteLDev/Arke (README, tooth.json, showcase shots).

## What Arke is
- A LeviLamina mod that runs Java Iris / OptiFine shaderpacks on Bedrock's D3D12 renderer. v0.1.3, client, win-x64.
- **Closed source.** The repo holds only README, tooth.json and screenshots; the mod ships as a prebuilt zip.
  No license is stated, so nothing of theirs gets copied, decompiled or bundled. We learn from what they publish.
- It targets LeviLamina `26.60.*`. That's newer than our 1.26.51.01 instance, so it won't load on our setup as is.

## What its public parts tell us about how it works
Third-party list -> pipeline (inferred, not documented by them):

| Component | Likely job |
|---|---|
| miniz | read zipped shaderpacks directly |
| JCPP (Iris's Java C preprocessor) | `#ifdef`/`#define` option handling that behaves exactly like Iris |
| glslang | Iris GLSL -> SPIR-V |
| SPIRV-Tools | validate / optimise SPIR-V |
| SPIRV-Cross | SPIR-V -> HLSL |
| DXC | HLSL -> DXIL for D3D12 |
| JOML + GLM | Iris's matrix math, so `gbufferModelView`, `shadowProjection` etc. match Java 1:1 |
| Dear ImGui + nlohmann/json | in-game settings window (pack options, Messages/errors), saved settings |

- Defines `IS_ARKE` / `ARKE_VERSION` alongside the Iris macros, so packs can branch for Bedrock.
- The showcase (iterationRP in the End) shows **real shadow maps** (crystal/pillar shadows across terrain), volumetric
  light and sky passes. So it adds Iris's extra render passes (shadow, deferred, composite, final), not just shader swaps.
- "0 performance loss" is their claim; untested by us.

## Where our ShaderForge is today
- Replaces vanilla `.material.bin` fragment (and vertex) bodies with hand-written HLSL and keeps vanilla signatures.
  It's safe, but every Java pack is a manual port and there are no extra passes (no shadow map, no composite chain).
- Renderer Foundation already injects its own D3D12 passes (live sky, postfx bridge, World FX), which is the hook we need.

## IrisBridge plan (our own implementation, open components only)
Spec source = Iris (LGPL-3.0, open) + OptiFine's shaders documentation.
1. **Loader:** unzip pack, parse `shaders.properties`, dimension folders, `block.properties`/`item.properties`/`entity.properties`.
2. **Preprocess:** options (`#define` sliders/toggles) + profiles; add `IS_BEDROCK_FORGE` macro.
3. **Translate:** glslang -> SPIRV-Tools -> SPIRV-Cross (HLSL SM6) -> DXC, with a disk cache keyed by pack + options hash.
4. **Uniform provider:** the Iris uniform set (camera, matrices, sun/shadow angles, time, weather, eye brightness...) from RF's camera + world state.
5. **Passes in RF:** gbuffers via material remap -> deferred -> composite chain -> final; then a **shadow pass**
   (re-draw terrain from the sun). This is the hardest and biggest win.
6. **ID mapping:** Java block/entity IDs in `block.properties` -> Bedrock blocks (needed for waving plants, water, emissives).
7. **Menu:** pack options in the Bedrock Forge glass UI (Shaders button), one-click with the matching DLL setup.

Order: 1-4 offline first (translate a pack and diff the HLSL), then composite/final in RF, then gbuffers, shadows last.
Vibrant Visuals: Iris packs assume a forward/deferred Java pipeline, so target Fancy first (matches the existing Fancy-first rule).

## Test 1: Spooklementary v2.0.4 offline translation (2026-11-12)
Pack: Spooklementary by SpacEagle17, on Complementary by EminGT. Private use with permission reported by Novak; the pack
files are never committed or shared. Tool: `tools/IrisBridge/translate.py` + `batch.py` (ours: include resolver,
Iris-style compat patcher, then glslang -> spirv-opt -> SPIRV-Cross -> dxc).

| Dimension | Stages compiled to DXIL |
|---|---|
| world0 (overworld) | 59 / 67 |
| world-1 (nether) | 59 / 67 |
| world1 (end) | 59 / 67 |

The 8 failures per dimension are all things Bedrock doesn't have or that are off by default:
- `dh_terrain`, `dh_water` (Distant Horizons mod)
- `clrwl_*` (Colorwheel mod)
- `shadowcomp` (the colored-lighting voxel pass, only when COLORED_LIGHTING > 0, which needs the custom `image.*` 3D textures)

So every gbuffers, shadow, deferred, composite and final stage of the default profile translates. About 1 MB of DXIL per dimension.

Patcher handles: `texture2D*`/`shadow2D` -> modern, `gl_FragData`/`gl_FragColor` -> located outputs, legacy matrices and
vertex attributes (`gl_Vertex`, `gl_MultiTexCoordN`, ...), Iris core attributes (`vaPosition`...), `MC_RENDER_STAGE_*`,
implicit core uniforms, `gl_Fog`. Pack attributes (`mc_Entity`, `at_tangent`, `at_midBlock`, `mc_midTexCoord`) auto-map from location 0.

### What the runtime must feed it (from the generated HLSL)
- **18 textures:** colortex0-7, depthtex0/1, shadowtex0/1, shadowcolor0/1, noisetex, gaux2/gaux4 (aliases), tex/atlas.
- **52 standard Iris uniforms:** camera and matrices (gbufferModelView/Projection plus inverses and previous; shadowModelView/Projection),
  time (worldTime, worldDay, frameCounter, frameTimeCounter, sunAngle, moonPhase), weather (rainStrength, wetness),
  player state (eyeBrightness, isEyeInWater, blindness, darknessFactor, nightVision, playerMood, heldItemId, heldBlockLightValue),
  screen (viewWidth/Height, aspectRatio, near, far), entity/block ids, skyColor, fogColor, cloudHeight, lightningBoltPosition.
- **11 pack-computed uniforms**, defined as expressions in shaders.properties (e.g. inSnowy, rainFactorUniform, waterAltitude, framemod8).
  So we need an Iris custom-uniform expression evaluator.
- **Vertex attributes:** mc_Entity (block ID, which needs the Java -> Bedrock block mapping from block.properties), at_tangent, at_midBlock, mc_midTexCoord.

### Next steps
1. Parse shaders.properties: option defaults/profiles, `program.*.enabled`, render target formats (`colortexNFormat`),
   `image.*`, `uniform.*` expressions, blend/alpha modes, DRAWBUFFERS/RENDERTARGETS.
2. Bake the inputs into a per-program table (register slot -> uniform/texture) that RF can bind.
3. RF: the post chain first (deferred1 -> composite..composite7 -> final) on Bedrock's colour + depth buffer. That's the first in-game test.
4. gbuffers through material remap, then the shadow pass.
