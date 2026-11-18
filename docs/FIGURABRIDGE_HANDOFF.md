# FiguraBridge - handoff for the Java -> Bedrock pipeline

Figura (Java avatar mod, 0.1.6 semantics) avatars running on Bedrock through Bedrock Forge: the avatar's own
Blockbench models, animations and Lua scripts, unchanged. Private use; credit avatar authors (Stick Figures by
VoidExpression, Gakuto1112's MIT avatars, GSAnimBlend by GrandpaScout, Figura by FiguraMC).

## How to convert + install (one pipeline)

    python3 -I tools/JavaPort/port.py <avatar folder or .zip> <out> --name "<Name>"

(`--vanilla` / the plug-in use a bedrock-samples `resource_pack`; entity tasks need its `entity/` and `models/entity/`.)

The FiguraBridge plug-in (`tools/FiguraBridge/javaport_plugin.py`) produces:
- `out/packs/<Name> Figura RP/` - resource pack replacing the player model (enable it on the world)
- `out/figura/<slug>/` (avatar NBT + `rig.json`) - copy to `<instance>/mods/BedrockForgeCore/config/figura/<slug>/`
  and write `{"avatar": "<slug>"}` to `config/figura/active.json`
- needs the Bedrock Forge DLL from `main` (`builds` branch); module `figurabridge`, on by default

Test knobs: `config/figura/tuning.json` (`debug`, `probe` = bone index, `simWalk`, `simCrouch`, `forceSneak`,
`wheelKey`, `wheelExec`), hot reload by touching `config/figura/reload.txt`.
Switching to a *different* avatar: install it, then leave and re-join the world (the runtime loads the installed
avatar on world join). Bedrock keeps the resource pack (geometry) it read at world load, so a mid-world switch waits
for the re-join; reloading the same avatar applies at once. `quadFlipU` flips text/sprite quads.

## Architecture
- `tools/FiguraBridge/compile.py`: avatar -> Figura NBT (port of Figura's loader) + `rig.json` + Bedrock RP.
  Every model part is a bone `p<i>` driven per frame by Molang variables `v.fb<i>*` (rotation/position/scale/
  visibility/texture). Big avatars are split into geometry chunks of <= 250 bones (Bedrock stops animating a geometry
  past ~255). Spare "dup" bones for script copies, uv_anim controllers (USE_UV_ANIM materials) for parts whose UVs
  scripts move, runtime texture slots, item pivot animation, vanilla/skin texture references.
- `src/figura/core`: native Lua 5.2 runtime with the whole Figura Lua API (coverage probe: every listed method of
  1296 present). Bone solve: each frame every part's model-space matrix is solved against its compiled parent, so
  scripts may move, reparent, hide or copy parts freely.
- `src/figura/win`: Bedrock host (player state from the actor, camera, sounds, action wheel on Z, native texture
  upload `DynTex` through the game's TextureGroup).

## Verified in game (2026-11-01)
Stick Figures (VoidExpression), CatMaid, SenkoSan (Gakuto1112), FBTest (FiguraAvatarTemplate + `fbtest.lua`
feature script):
- models, all textures with transparency (cutout; real partial alpha blends), emissive layers
- Lua scripts: animations (incl. GSAnimBlend), idle/walk/sneak (real Bedrock sneak)/sit, action wheel, config
- Camera parts (billboard heads) from every angle; 1:1 size with Java (0.9375 player scale)
- script copies (`ModelPart:copy`), several copies of one part (a part copied by path gets up to 8 spare bones);
  reparenting; hidden/detached parts
- `WORLD` parent type: parts drawn at world coordinates (16 units = 1 block) through the player's bones
- render tasks: `newItem` / `newBlock` (vanilla textures), `newEntity` (vanilla Bedrock entity geometry + default
  texture, bind pose), `newText` (8x8 public-domain font, section-sign colours, JSON text components, shadow /
  outline / background / alignment / wrap / opacity), `newSprite` (avatar textures, vanilla textures named in scripts)
- runtime textures: `textures:newTexture` + `setPrimaryTexture("CUSTOM")`, `texture:setPixel` + `update()` on
  avatar textures (re-pushed after world join); `textures:fromVanilla` for vanilla paths named in scripts
- UV animation (`setUV`/`setUVPixels`/`setUVMatrix`): eyes, mouths, flipbooks
- held items follow `RightItemPivot`/`LeftItemPivot` (rest point = Figura's default item pivot)
- `SKIN` uses the player's skin, `RESOURCE` Bedrock vanilla textures, `CAPE` the player's Bedrock cape
  (`Texture.cape`), `ELYTRA` the vanilla elytra texture
- performance: every render controller is gated on `v.fbuse<file>` (a visible bone uses that texture this frame);
  only changed bone variables are written. CatMaid 25 -> ~50 FPS, SenkoSan (667 parts, 108 textures, 450
  controllers) ~50 FPS, stick figure 58-60 FPS (with Renderer Foundation)
- installing an avatar then re-joining a world switches to it (mid-world switches wait for the re-join)
- headless: 5 avatars, all scenarios, 0 script errors (`tests/figura/Sim.cpp`; `SIMUPLOAD=<dir>` dumps runtime
  texture uploads)

## Known limits
- text uses an 8x8 bitmap font (ASCII; other characters show as `?`), not Minecraft's own glyphs
- entity tasks show the vanilla entity in its bind pose (no entity animation), item tasks a flat item quad
- 8 text/sprite tasks drawn at once (QUAD_SLOTS), 256x256 pixels each
- `World` parts are culled with the player (visible bounds widened to 96 blocks when an avatar has world parts)
- `Hud`/`Gui`/`Skull`/`Portrait`/`Arrow`/`Trident`/`Item` parent types are not drawn (no Bedrock equivalent pass)
- networking (pings to other players) is local only; Figura's server avatars are not fetched; other players see
  the normal skin

## Trigger
The git tag `figurabridge-ready` on `main` marks the state the pipeline chat should take. Check:
`git ls-remote --tags https://github.com/Novakify/BedrockForge figurabridge-ready`
