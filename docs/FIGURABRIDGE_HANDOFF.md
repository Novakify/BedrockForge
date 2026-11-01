# FiguraBridge - handoff for the Java -> Bedrock pipeline

Figura (Java avatar mod, 0.1.6 semantics) avatars running on Bedrock through Bedrock Forge: the avatar's own
Blockbench models, animations and Lua scripts, unchanged. Private use; credit avatar authors (Stick Figures by
VoidExpression, Gakuto1112's MIT avatars, GSAnimBlend by GrandpaScout, Figura by FiguraMC).

## How to convert + install (one pipeline)

    python3 -I tools/JavaPort/port.py <avatar folder or .zip> <out> --name "<Name>"

The FiguraBridge plug-in (`tools/FiguraBridge/javaport_plugin.py`) produces:
- `out/packs/<Name> Figura RP/` - resource pack replacing the player model (enable it on the world)
- `out/figura/<slug>/` (avatar NBT + `rig.json`) - copy to `<instance>/mods/BedrockForgeCore/config/figura/<slug>/`
  and write `{"avatar": "<slug>"}` to `config/figura/active.json`
- needs the Bedrock Forge DLL from `main` (`builds` branch); module `figurabridge`, on by default

Test knobs: `config/figura/tuning.json` (`debug`, `probe` = bone index, `simWalk`, `simCrouch`, `forceSneak`,
`wheelKey`, `wheelExec`), hot reload by touching `config/figura/reload.txt`.

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
Stick Figures (VoidExpression), CatMaid (Gakuto1112), FBTest (FiguraAvatarTemplate + feature script):
- models, all textures with transparency (cutout; real partial alpha blends), emissive layers
- Lua scripts: animations (incl. GSAnimBlend), idle/walk/sneak (real Bedrock sneak)/sit, action wheel, config
- Camera parts (billboard heads) from every angle; 1:1 size with Java (0.9375 player scale)
- script copies (`ModelPart:copy`) drawn; reparenting; hidden/detached parts
- runtime textures: `textures:newTexture` + `setPrimaryTexture("CUSTOM")`, `texture:setPixel` + `update()` on
  avatar textures (re-pushed after world join)
- UV animation (`setUV`/`setUVPixels`/`setUVMatrix`): eyes, mouths, flipbooks
- held items follow `RightItemPivot`/`LeftItemPivot`
- `SKIN` override uses the player's skin; `RESOURCE` uses Bedrock vanilla textures named in the scripts
- headless: 5 avatars, all scenarios, 0 script errors (`tests/figura/Sim.cpp`)

## Known limits (not yet on Bedrock)
- render tasks (`newText`/`newItem`/`newBlock`/`newSprite`/`newEntity`): the API works, nothing is drawn yet
- `World` parent type parts, `Cape`/`Elytra` texture overrides, Figura's own nameplate rendering
- one dup per part (a part copied twice shows once); 4 runtime texture slots (64x64 placeholders, other sizes upload
  without the in-place path)
- performance: very large avatars (SenkoSan: 611 parts, 57 textures) need many render controllers
- networking (pings to other players) is local only; Figura's server avatars are not fetched

## Trigger
The git tag `figurabridge-ready` on `main` marks the state the pipeline chat should take. Check:
`git ls-remote --tags https://github.com/Novakify/BedrockForge figurabridge-ready`
