# Glass Armor (Bedrock Forge + Renderer Foundation) - plan

Status 2026-11-06 03:15: **look approved** (preview v6); M0 + M0b probes run in game; **M1a built** (glass-armor/rf 79d6cdf, CI green, merged into the live RF line, off by default) - not yet tested in game (PC went offline).

## The look Fernando signed off (docs/glass_armor/preview_v6.html)
- Shape and geometry **1-to-1 vanilla**: Mojang's `geometry.player.armor.*` cubes (helmet inflate 1.0 + hat layer
  1.5, chest 1.01, arms 1.0, leggings 0.5 / legs 0.49, boots 1.0, mirrored left limbs) and the painted pixels of
  the vanilla texture decide what shows (eye slits, neck notch, boot cut-outs). Each material has its own shape.
- Clear glass (almost no frost) with a **fixed twist** baked into each face: what is behind is bent like warped
  glass. No animation, no wobble. Warp levels Subtle / Medium (default) / Strong.
- **Your own body under the glass is never warped** - only the world further behind (depth check). This was the
  "my limbs are shaking" bug in preview v3-v4.
- Client-menu glass details: lens bend + small chromatic split at the rim, white rim that brightens on the side the
  real sun/torch is on, neon outline in the material colour that also glows into the glass from the edges.
- Enchanted = **edge flow** (light travelling round the outline). The light-sweep glint was rejected.
- Leather takes the item's dye colour (Hive team colours).
- Server use (Hive) is the target; Fancy first, then Vibrant Visuals. No resource-pack-only fallback (rejected).

## How it draws (design)
Draw it **inside the game's frame, on the game's own armor draw**, not over the finished frame:
1. RF already tags game pipelines by pixel-shader hash at `CreateGraphicsPipelineState` and hooks the command list
   (UiCapture targets: SetPipelineState, OMSetRenderTargets, Draw*, root-signature state tracking + restore, barriers)
   - sky stage 2 and Spatial GUI use this. Glass armor reuses it.
2. Before the first armor draw of a frame: copy the bound render target (world + bodies so far) and the depth buffer.
3. For each armor draw: bind our root signature = the game's root signature **+ one extra descriptor table**
   (t0 scene, t1 depth, t2 sdf, t3 face map, b0 params), re-apply the game's root arguments, and draw with a PSO
   made from **the game's own vertex shader** + `glass_armor.hlsl`. Same vertex buffers, same depth test ->
   exact pose for every player wearing armor, first person sleeves included, occlusion correct.
4. Restore the game's root signature / PSO / arguments so the rest of the frame is untouched.

Why not over the finished frame (like LyricsPass / Cinematic)? No depth there: blocks in front of you in F5 and
other players would be painted over, and the body-stays-clean check is impossible.

## Open questions (M0 probe answers them in game)
1. **Which pipelines draw armor?** Attachables use material `armor` / `armor_enchanted` (`armor_leather`,
   `armor_leather_enchanted` for dyed leather). Find their Entity.material.bin pass/variant hashes on Fancy and VV.
   If the same PSO also draws other entities, tell them apart by the bound texture (64x32 armor texture SRV) or by a
   marker in our pack's armor textures.
2. **Game VS outputs**: does the armor VS output the texture UV as a plain TEXCOORD we can read? (D3DReflect the
   DXIL output signature.) Our PS must match the VS output signature.
3. **Depth convention** (reversed Z?) and projection params for linear depth; RF already reads this for the sky.
4. Dye colour + enchanted state per draw: from the material variant (`armor_leather*`, `*_enchanted`) and the
   per-draw constants (the overlay / change colour the game passes for leather).
5. Hive: does Hive's server pack replace player armor rendering (custom attachables / cosmetics)? Test in a lobby.

## M0 probe results (2026-11-06, MC 1.26.51.01, Fancy, full diamond, F5 front) - docs/glass_armor/probe_m0_20261106.txt
- **Armor draws on `Actor.material.bin`**, pixel shader `71f7073b9d471dae` / vertex shader `26002122658a193e`
  (the alpha-test actor variant: cull none, depth less + write, blend off). The ON-only Actor pipeline drew
  9 draws / 344 triangles. The same PS also draws other actors (mobs, the player), so the pipeline alone can't
  single out armor -> M0b: tell armor draws apart per draw (bound texture size / descriptor, draw size).
- **The VS hands the PS the texture UV as `TEXCOORD0.xy` (o5)** - exactly what glass_armor.hlsl needs.
  Full output: SV_Position0, COLOR1, COLOR0, COLOR2, COLOR3, TEXCOORD0.xy, TEXCOORD3.xyz.
- Fancy targets: colour RGBA8, depth **D24S8, standard Z (less)** -> zParams from a normal projection.
- All world pipelines share one root signature (00000271D0E4E2F0) -> one extended root signature covers armor.
- Probe lesson: the game keeps several identical PSO objects (frames in flight), so diffing by PSO pointer is noisy
  (chunks/clouds show up as "ARMOR?"). M0b diffs by shader hash + draw/triangle counts instead.
- No crash, no FPS change from the probe.

## M0b probe results (2026-11-06 02:48) - docs/glass_armor/probe_m0b_20261106.txt
In-world armor (third person front, full diamond) = **4 draws on the alpha-test Actor PS `71f7073b9d471dae`**,
each binding a **64x32 RGBA8** texture in root table 1 slot 0 (root table 4 mirrors it):
helmet 72 indices (head + hat cubes), chestplate 108 (body + 2 arms), leggings 108 (own texture = layer 2),
boots 72. Exactly the vanilla cube counts (12 triangles = 36 indices per cube).
- Other actors on the same PS bind other textures (52x256, 180x2048, 32x512 atlases; player skin), so **the bound
  texture identifies armor**. Many mobs are also 64x32, so the glass armor pack gives each armor texture a
  **fingerprint size** (64k x 32k with an unusual k per material/layer); RF matches the bound SRV's size.
- Descriptors: the game copies SRVs into a shader-visible heap it created before RF hooks in; RF now learns that
  heap from SetDescriptorHeaps and follows CBV/SRV CopyDescriptors* (only while a capture is armed).
- The pause screen's player preview draws armor too (an Opaque Actor draw of 360 indices = all 10 cubes at once).

## M1 plan (next)
1. Glass armor resource pack: vanilla armor textures upscaled to fingerprint sizes (looks vanilla without RF).
2. RF: hook CreateRootSignature (keep the blob of the Actor root signature) + CreateDepthStencilView
   (DSV handle -> depth resource) + CreateRenderTargetView (already tracked).
3. On the first fingerprinted armor draw of a frame: copy RT + depth into RF textures (state transitions known:
   RT in RENDER_TARGET, depth in DEPTH_WRITE).
4. Armor draw: our root signature = game's + 1 table (scene, depth, SDF, face map, params) -> re-apply the
   game's root args -> PSO = game VS bytecode (kept from notePso) + glass_armor PS (dxc, DXIL, input signature
   matching the Actor VS output: SV_Position, COLOR1, COLOR0, COLOR2, COLOR3, TEXCOORD0.xy, TEXCOORD3.xyz)
   -> draw -> restoreRoot().

## M1a (built 2026-11-06, untested in game)
- Armor textures are recognised **without a resource pack**: the first time an Actor draw binds an unknown 64x32
  texture, RF reads it back once at Present and matches its alpha bits against Mojang's 14 armor masks
  (tools/GlassArmor/vanilla_masks.json); materials that share a shape (iron/gold/copper, layer-2 leggings) are split
  by mean colour. Works with any pack that keeps the vanilla 64x32 shape; HD armor packs come later.
- Armor draws (Actor PSO + recognised texture) are drawn with our PSO: the game's Actor VS + `shaders/glass_armor.hlsl`
  (one DXIL per material x layer, tools/gen_glassarmor.py in CI), alpha blend, no depth write, cull none.
  M1a look: faint material tint, neon outline tracing the painted pixels / cube faces, outline glow into the glass.
- F6 > Glass Armor > "Glass armor on" (saved in config/GlassArmor.txt). Counters show recognised textures,
  glass draws per frame, pipelines, failures.
- Next: M1b = scene + depth copy before the first armor draw, extended root signature, the approved twist.

## Milestones
- **M0 probe (RF, F6 > Glass Armor)**: key press logs one frame - every PSO used with PS hash, material pass, VS
  output signature, bound SRV sizes, RT / DSV formats, draw counts - with and without armor on. Output under
  `mods/RendererFoundation/logs/glassarmor/`.
- **M1 test world, Fancy**: diamond only, own player, no warp (scene copy straight through) -> proves the
  in-frame swap + state restore. Then full shader.
- **M2 all materials + leather dye + enchant edge flow**, BF menu toggle (glass armor on/off, warp level) in the
  Visual page, same glass sliders as the rest of the menu.
- **M3 Vibrant Visuals** (HDR target, deferred passes).
- **M4 Hive**: lobby + Bed Wars match, first person + F5, FPS check on the RTX 4060.
- Blockers outside this feature: Blueberry + LeviLauncher mods crash together; Hive's OK on visual DLLs.

## Files
- `tools/GlassArmor/gen_masks.py` - vanilla armor textures -> outline distance fields + face map (7 s, 15 textures)
- `docs/glass_armor/glass_armor.hlsl` - pixel shader draft (port of the preview)
- `docs/glass_armor/preview_v6.html` - the approved preview (three.js; open in a browser)
