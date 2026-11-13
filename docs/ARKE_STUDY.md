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

## Progress 2026-11-12 (later): the full offline pipeline + RF runtime M1

### Bedrock Forge `tools/IrisBridge/` (research/iris-bridge)
- `pack.py`: options (431 in Spooklementary: sliders, toggles, consts), profiles, `shaders.properties` with Iris-style
  conditional-only preprocessing, program enable rules, blend / alphaTest, `image.*`, custom uniforms; program lookup with
  Iris fallbacks (world folder -> root -> fallback chain).
- `expr.py`: custom-uniform language -> bytecode + reference VM. `smooth()` matches Iris exactly (checked against Iris's
  open source: half-life = fade time / 10).
- `translate.py`: Iris compat patcher, option overrides by rewriting the pack's own `#define` / `const` lines, **fixed
  texture slots** (one root signature for every pass of every pack: our upgrade over auto-mapping), SSBO / image slots,
  integer-texture sampling, a SPIRV-Cross storage-buffer store bug worked around in our HLSL post-fix, uniform defaults
  kept, comparison samplers marked, fixed vertex-attribute locations.
- `bundle.py`: whole pack -> DXIL (signed by DXC 1.8's validator) + `manifest.json` (human) + `runtime.txt` (RF).
- Results: default profile 168/168 stages over 3 dimensions; every profile POTATO..ULTRA 100% (incl. colored lighting:
  voxel images + storage buffers). Tests: `test_irisbridge.py` 28/28.

### Renderer Foundation `irisbridge/rf` (built on worldfx/rf)
- `src/IrisBridge.{h,cpp}`: runs the pack's deferred*, composite*, final at the World FX hook (opaque world done, depth
  intact). Iris semantics: colortex ping-pong, RENDERTARGETS, buffer formats + clears, noisetex, world colour imported
  into colortex0 (gamma -> linear), depth -> depthtex0/1, Iris built-in uniforms from the game camera, custom uniforms.
- Orientation: buffers are stored top-down (D3D); the pack gets Y-flipped projection matrices + flipped quad UVs, so its
  uv <-> NDC <-> view maths stays consistent. Bedrock depth (1 - near/z) == GL window depth with an infinite projection.
- `src/IrisBundle.h` + `IrisUniformVM.h`: header-only; C++ vs Python VM parity 66/66 on Spooklementary's 30 uniforms.
- F6 > IrisBridge. Install: put a bundle folder in `mods/RendererFoundation/irisbridge/<name>/`.

### Honest limits of M1 (next milestones)
- No gbuffers / shadow pass yet: normals, material masks and shadow maps read empty (shadows = fully lit). Packs that
  light the world in deferred from gbuffer data will look different until M2.
- Bedrock's colour is LDR 8-bit; the pack tonemaps it again (we decode gamma first; exposure slider in F6).
- No real time of day / weather / biome bridge yet (F6 time slider). Fancy renderer only. colortex sizes full screen,
  no mipmaps. Varying locations are matched by declaration order (fine for Complementary; name-pinning is next).
- M2: gbuffers via ShaderForge material remap (normals + material IDs from Bedrock's terrain), mc_Entity block IDs from
  block.properties mapped to Bedrock blocks. M3: the shadow pass (re-draw terrain from the sun). M4: time/weather/biome bridge
  from Bedrock Forge, packs' options in the glass menu, Vibrant Visuals.

## Progress 2026-11-13: M2 terrain + M3 shadows

- M2 (gbuffers_terrain) verified in game: 66/66 RenderChunk draws replayed every frame through the pack's own
  gbuffers_terrain, correct block atlas, rebuilt face normals, depth-tested against Bedrock's depth.
- M3 (shadow) root cause found: the bundle listed the game's `BfGameVS` cbuffer members (b8) as `$Globals`, so RF
  wrote zeros over the pack's matrices at the same offsets (row 0 of `shadowModelViewInverse` -> every vertex on one
  plane -> nothing rasterised). Fixed in `bundle.py` (reflect only `$Globals`) and RF (skip `_<id>_bf_*`).
  After the fix the shadow map fills with depth (debug 200). Terrain shared the bug (its `iris_ProjMat` row 0 was
  being zeroed too), so M2 should also be re-checked for subtle offsets.
- Known gap: we replay what the *camera* drew, so Bedrock has already frustum-culled the chunks; casters behind or
  above the camera are missing from the shadow map. Fix = record the uncull'd chunk list (or keep a per-chunk
  draw cache keyed by vertex buffer and replay every cached chunk inside shadowDistance).
- Arke's closed binary: not decompiled (no license / author permission). Public behaviour only, as a feature list.

## Feasibility assessment (answers to the 2026-11-13 research brief)

Legend: **E** established (done / documented APIs) · **F** feasible engineering · **X** experimental · **U** unsupported now.

1. **How close are we?** Closer than any doc implied: a real Iris pack (Spooklementary, 180/180 stages) compiles
   offline to signed DXIL and runs in game: full post chain (composite/deferred/final, 9 passes), the pack's own
   gbuffers_terrain over Bedrock's chunk geometry, and the shadow pass writing a shadow map. ~85 fps at 4K.
2. **What we already have that carries it:** RF's D3D12 hooks (UiCapture draw recording, Glass Armor root-signature
   + descriptor tracking, SkyPass hook points), the F6 glass UI, ShaderForge's HLSL tooling, the LyricsForge sky
   dome (proves we can draw our own passes inside Bedrock's frame), and the IrisBridge translator + runtime.
3. **Missing:** entities / hand / water (translucent phase) / particles / weather / block entities (all need their
   draws classified the way RenderChunk is), block IDs (`mc_Entity` = 0 today), uncull'd shadow casters, real
   weather/biome/eyeBrightness uniforms, a cloud-independent trigger (worlds without clouds skip IrisBridge),
   compute/image programs (packs using SSBOs/images are rejected), and the Arke-style pack UI.
4. **Realistic first targets (E/F):** composite/deferred/final (E, done), gbuffers_terrain (E, done), shadow (F,
   working, needs uncull'd casters), gbuffers_water (F: same RenderChunk path, translucent PSO), gbuffers_entities +
   hand (F: need vertex-format adapters per Bedrock material), block.properties IDs (F, below).
5. **GLSL->HLSL:** keep the current chain: pack preprocessor -> glslang (SPIR-V) -> spirv-opt -> SPIRV-Cross (HLSL
   SM6) -> DXC (signed DXIL), all offline, cached per option set. Compiling is not correctness: every program gets a
   golden-frame check in game (the debug views exist for this).
6. **Inputs + passes:** matrices/camera/time from RF's camera snapshot (E); Iris uniforms + custom-uniform VM with
   Iris-parity smoothing (E, 66 tests); colortex ping-pong + shadow targets (E). Passes Bedrock never runs are
   *replays* of recorded draws with our pipelines (E for terrain; F for other geometry classes).
7. **Biggest Bedrock obstacles:** (a) no vertex normals/tangents/block IDs in Bedrock's chunk format, so they must
   be rebuilt or side-loaded; (b) culling happens before we see draws; (c) Bedrock's material variants differ per
   update, so the vertex-format adapter must be data-driven; (d) Hive & other servers: client-only rendering is
   fine, but no auth/anti-cheat/VPN bypass, ever; (e) GPU cost of rendering the world twice (once vanilla, once
   ours). Long-term fix for (e): stop vanilla terrain from drawing when IrisBridge owns it (X).
8. **Architecture:** yes, a compatibility layer, but not a separate product: IrisBridge = translator (BF tools, also
   wired into `tools/JavaPort/port.py` so a Java mod's recommended shaderpack converts with it) + runtime module in
   RF. Shared pieces: one shader compile cache, one render-pass graph, one material registry used by IrisBridge,
   ShaderForge and our own effects.
9. **With our skies + gameplay FX:** yes: IrisBridge runs at the cloud hook, our sky dome and LyricsForge layers
   already composite there. Plan: a pass graph with ordered slots (pack gbuffers -> our material FX -> pack
   deferred/composite -> our cinematic/PvP overlays -> pack final) so BF effects get the pack's lighting for free.
10. **Tools/original systems to add:** block-ID side channel (atlas UV -> block name -> block.properties ID table,
    built from the resource pack: F, and it's the key to waving plants, emissive ores, glass/water materials);
    per-chunk geometry cache (shadows + uncull'd casters); a compatibility report per pack (which programs ran,
    which fell back); a frame-capture/compare tool for regression tests; the in-game pack browser (task #88).

### Block-specific + PvP visuals (from the brief)
All of these become *material rules* once block IDs exist: wool/concrete (subtle fabric/micro-normal), glass
(refraction + tinted shadowcolor, already supported by the shadow path), planks/stone/ores (POM + emissive ore
veins), obsidian/netherite (PBR specular). Items/armor/projectiles/hit FX need the entity + hand passes first; they
stay visual-only (no mechanic change, readable, no unfair info like seeing through walls).
