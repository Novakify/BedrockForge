# Next session handoff (written 2026-10-21 ~20:30 UTC)
State: all work committed + pushed to github.com/Novakify/BedrockForge (main). CI build #39 (commit ec1ab0a) green -> DLL on `builds`.
PC instance: C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01\mods\BedrockForgeCore
(installed is still b8 / commit cd1133c). Logs: mods/BedrockForgeCore/logs/diag.txt + shaderpacks.txt (rewritten every ~10 s).
This session ran unattended with NO PC access (folder-access prompt not yet approved) -> nothing new was installed or tested.
See docs/EXPERIMENT_LOG.md E-020.

First thing next session:
0. Get folder access to the instance folder (one prompt), back up BedrockForgeCore.dll (rename to .b8.bak), install the ec1ab0a+ DLL
   from the `builds` branch (unique staged filename), launch once from LeviLauncher.

Renderer Foundation (separate AGPL-3.0 project, Fernando's call: private fork, kept separate from Bedrock Forge):
- Branch `renderer-foundation` of this repo (upstream BetterRenderDragon history kept), built DLL -> branch `rf-builds`.
  Cloud worktree: /home/claude/rf (git worktree of this clone). Move to Novakify/RendererFoundation once Fernando creates it.
- Install: copy rf-builds/RendererFoundation into <instance>/mods/ next to BedrockForgeCore. Bedrock Forge's ForgeRender then
  stops redirecting materials (it detects RendererFoundation.dll). F6 = overlay (free the mouse first with Esc).
- Test: overlay shows (D3D12 + GPU name), shaderpack combo serves Anime, "Served materials" window, resource-pack shader,
  Reload shaders, Vibrant Visuals toggle. Never copy RF code into Bedrock Forge (licence) - only coexist.

Shine 3.0 -> Bedrock conversion (tapeQ's mod, Fernando has permission; credit tapeQ, nothing is ours, keep private):
- Branch `shine-bedrock` (worktree /home/claude/shine): README, PERMISSION.md, CONVERSION.md (133 features -> routes),
  tools/convert_assets.py (jar -> Bedrock RP: 81 particles, 49 sound events, 34 textures; output in build/, gitignored).
- Jar: CurseForge file 8659331 (curseforge/forgecdn domains now allowed in the cloud). Decompiling was blocked by the
  session classifier -> waiting for tapeQ's 3.0 source (Fernando asked on Discord). Then: particle motion/spawn rules,
  shaders (bloom/rim/no-shading/post first), config -> RF overlay.

Priority order:
1. Test Fullbright v3 (tessellator light floor + relightAllRenderChunkGeometry). Enable at night / in a cave; diag.txt should show
   "tessellator light" climbing and "relights" > 0. If terrain doesn't change: the 1.26 tessellator may not use BlockTessellatorCache
   -> fall back to a shader path (needs a runtime flag channel into the Anime RenderChunk shader).
2. Test ForgeRender (menu: Visual -> ForgeRender):
   a. shaderpack slider (label shows name), then "Reload materials now" -> does shaderpacks.txt "saw N material opens" grow and
      does the look change? If cleanReloadMaterials does NOT re-open files, find the real 1.26 reload path (needs the exe;
      ideas only from BRD: discard frame + free compiled shader blobs).
   b. put a test resource pack with renderer/materials/<X>.material.bin in the global stack -> shaderpacks.txt lists it
      ("<X> <- pack") and the material is served "rp:<X>". Check that the listed stack order is bottom-to-top.
   c. Vibrant Visuals toggle (assumes GraphicsMode::Advanced == VV); status text in the module description says why if refused.
3. Actor AlphaTest: source now compiles the dither fade out (BF_ACTOR_DITHER). Rebuild Anime with ShaderForge using the PC's
   vanilla materials, rename .hlsl.disabled -> .hlsl, test mobs visible; then re-add dither.
4. Untested: zoom (hold C), hit colour, impact frames (NOW CRIT-ONLY: jump + hit a mob while falling; plain hits must NOT flash), chat compact, The Dweller (/ib:spawn), Angel of Reaping.
   Aim with `/tp @s ~ ~ ~ facing @e[type=pig,c=1]`; attack with left_mouse_down/up (plain clicks don't register).
5. Remaining modules: freelook, block outline, nametags, hitboxes, pack switcher, movable HUD. (Toggle sprint/sneak exist as vanilla
   options in 1.26 - low priority.)
Computer-use notes: close the game with alt+F4 (its X can be under Task Manager); launch ONCE from LeviLauncher; Play -> click + Enter;
world card -> mouse down/up; hold_key rshift or key rshift opens the menu. Game input from computer use is ignored (E-015).
