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
