# Next session handoff (written 2026-10-21 12:50 CT)
State: all work committed + pushed to github.com/Novakify/BedrockForge (main). CI builds DLL to the `builds` branch.
PC instance: C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01\mods\BedrockForgeCore
(installed = b8 / commit cd1133c, md5 30239c4c...). Logs: mods/BedrockForgeCore/logs/diag.txt + shaderpacks.txt (rewritten every ~10 s).
See docs/EXPERIMENT_LOG.md E-019 for test results.

Priority order:
1. Fullbright on 1.26: createBaseLightTextureData hook fires (brightened) but no visual change; buildImage ran only twice.
   Find the real 1.26 lightmap path (GPU pass?), or add a brightness path in the Anime RenderChunk shader.
2. "ForgeRender" (our own BetterRenderDragon replacement, original code, BRD is AGPL/reference only):
   a. shaderpack switcher in the ClickGUI + hot reload (1.26 equivalents of BgfxFrameBuilder::discardFrame / CompiledMaterialManager::freeShaderBlobs)
   b. material.bin loaded from resource packs (Java->Bedrock converted packs ship their shaders)
   c. Vibrant Visuals / deferred toggle
3. Untested: zoom (hold C), hit colour, impact frames (K + hit a mob), chat compact, The Dweller (/ib:spawn), Angel of Reaping.
   Aim with `/tp @s ~ ~ ~ facing @e[type=pig,c=1]`; attack with left_mouse_down/up (plain clicks don't register).
4. Actor AlphaTest cel shader made alpha-tested mobs invisible (disabled) - debug.
5. Remaining planned Blueberry-parity modules (freelook, toggle sprint/sneak, block outline, nametags, hitboxes, pack switcher, movable HUD).
Computer-use notes: close the game with alt+F4 (its X can be under Task Manager); launch ONCE from LeviLauncher; Play -> click + Enter;
world card -> mouse down/up; hold_key rshift or key rshift opens the menu.
