# Experiment Log

Format: `E-### | date | commit | environment | what was done | what was seen | label`

E-001 | 2026-10-20 | — | cloud workspace | Cloned 6 upstream repos, read manifests/licenses/READMEs | Target set to 1.26.51.01 + LeviLamina 26.51.x | Verified in source
E-002 | 2026-10-20 | 9227715 | cloud | Wrote BedrockForgeCore (SDK v0, P1+P2) | Not compiled yet | Theoretical
E-003 | 2026-10-20 | - | cloud | JavaModAnalyzer v0 on synthetic Fabric jar (keybind+HUD+Blaze3D+1 mixin) | Correct buckets, score 0.6 "Portable with native work"; fixed own-package + asset-grouping bugs | Verified by execution
E-004 | 2026-10-20 | - | cloud | Wrote Prototype 3 ImpactFrames (ActorHurtEvent → atomics → AfterUIRenderEvent overlay + playUI) | Not compiled yet | Theoretical
E-005 | 2026-10-20 | - | cloud | Analyzed user's impact-frames-1.2.1.5.jar (Fabric, MC 26.2, 44 classes, 18 mixins, ARR) | Score 0.14: mostly rendering mixins. Reworked P3 to alternating-frame model + trigger rules. Fixed analyzer chat-component misclassification | Verified by execution (analyzer); P3 Theoretical
E-006 | 2026-10-20 | 21e992e/5ff7389 | GitHub Actions windows-latest | First CI builds | Both green; DLL 569 KB x64 PE. Found manifest platform '${modPlatform}' (would be rejected by ModLoadPlanner) -> fixed in 9ed54e5 + patched in v0.1 package | Verified by compilation
E-007 | 2026-10-20 | - | cloud | BP script (entityHurt -> per-player colour grading posterize, bf.impact SFX, /camerashake) | tsc --checkJs against @minecraft/server 2.3.0 + server-graphics 1.0.0 typings: clean | Verified by type-check (not in game)
E-008 | 2026-10-20 | - | cloud | Batch analysis of 11 entity mods from "I Couldnt Escape it.." pack | 0 mixins everywhere; From The Fog is datapack-driven (253 functions); GeckoLib models are Bedrock format | Verified by execution
E-009 | 2026-10-20 | - | cloud | ModPorter on From The Caves 0.5.6b6 | 50 entities / 18 geo / 18 anim / 44 sounds generated, validator 0 errors; headless three.js render of all 28 distinct models looks correct | Verified by validation + render (not in game)
E-010 | 2026-10-20 | - | cloud | FTC behaviour overlay v0 (phases, Behind, Front, possession, Wooden Cross) | tsc --checkJs clean vs @minecraft/server 2.3.0 | Verified by type-check (not in game)
E-011 | 2026-10-20 | - | cloud | The Dweller (ice_bear_2026 1.7.0, MIT): extractor generalized (entities/ + registry/, local-var attributes, single-entity fallbacks); hand-authored AI overlay (state/pose component groups, synced properties, 9-state anim controller, emissive eyes, spawn/noise timers, roll system, stare/chase/flee, crawl/crouch, stuck teleport) | validator 0/0, tsc clean, 3-angle render correct, FTC re-extract improved (50/50) | Verified by validation + type-check + render (not in game)
E-012 | 2026-10-21 | - | cloud + user PC | PC setup: LeviLauncher 0.3.13, isolated instance 1.26.51.01 (msixvc md5 verified), lip 0.34.9, LeviLamina 26.51.7; BedrockForgeCore + Impact packs + The Dweller installed into the instance only (backup first) | LeviLamina banner + "0 mod(s) loaded" on first boot (mod copied in after that launch) | Verified on hardware (loader); mod not yet tested in game
E-013 | 2026-10-21 | - | cloud | Sculk Horde (TeamPeril, Apache-2.0, ~80k lines): new SOURCE-mode extractor (extract_src.py: registrations, attributes through class chain + vanilla parents, fly/swim/walk, GeckoLib paths, glowmasks, sounds/goals via src_details.py); generator: unique anim names, idle/walk/attack role mapping + always-on "living" loop, emissive glowmask layer, fly/swim/stationary components, entity_sounds; overlay: gravemind mass + 3 evolution stages, burrowed -> mite + sculk mass, mass-funded reinforcements (mod's EntityFactory table), throttled infestation cursors, nodes, ancient node, 9 mob abilities, custom blocks (mass/node/ancient node) | 32 mobs + 3 blocks, validator 0 errors, tsc clean, render of 29 models correct; FTC + Dweller regenerated with 0 errors (found + fixed a regression: unknown attack damage was treated as 0) | Verified by validation + type-check + render (not in game)
E-014 | 2026-10-21 | 2ca239f | CI + user PC | Client v1: module framework (50+ modules incl. roadmap), ClickGUI (Right Shift), zoom/FOV/sensitivity/attack hooks, shaderpack loader (CreateFileW/CreateFile2 redirect) | CI green; on launch the mod wrote config.json, modules.json and shaderpack.txt (loads + enable ran). Not yet seen in a world | Verified by compilation + load on hardware
E-015 | 2026-10-21 | - | user PC | Tried to drive the game with computer use | Input is ignored (GameInput doesn't take synthetic SendInput); window title never showed Not Responding; title screen in 1.26 is a still image | Limitation: in-game tests need Fernando
E-016 | 2026-10-21 | 3387e03 | CI | Client batch 2: fullbright (lightmap), hit colour, no hurt cam, no view bobbing, time changer (render-thread only) | CI green, installed | Verified by compilation
E-017 | 2026-10-21 | ec95b5f | cloud | ShaderForge: lazurite 1.26 buffer-layout shim (byte-identical round trip on Sky/RenderChunk/ColorPostProcessing), dxc 1.8, build.py generates per-variant input structs and only accepts signature-identical blobs | Anime pack: Sky 4/4, RenderChunk 48/60 (Seasons variants vanilla), bindings identical to vanilla | Verified by disassembly (not in game)
E-018 | 2026-10-21 | 3387e03 | cloud | Sculk Horde pass 2: 45 infested block variants (63 vanilla blocks), acid blob + 4 soul bolts (vanilla projectile component), spine spikes, floor soul spears, Angel of Reaping + Sculk Enderman attack kits | validator 0 errors (39 entities, 48 blocks), tsc clean | Verified by validation + type-check (not in game)

## E-019 - 2026-10-21 in-game test pass (b6-b8)
- ClickGUI: Right Shift opens; left click toggles (Speed Display, Fullbright verified); categories switch; modules.json persists.
- Bug fixed (b7): after closing, the panel kept drawing over the HUD (another non-HUD layer kept "dialog seen" fresh). Esc / x now reset the menu directly.
- Bug found in b7 (latching the first non-HUD view drew the native dialog over our menu) -> b8 covers every non-HUD view again and logs screen names in diag.txt.
- Quick Right Shift taps missed (async key state already released) -> GetKeyState check added (b8).
- Shaderpacks: Actor, RenderChunk AND Sky load from the pack (Sky loads at world join; the first report was written too early). 56 material opens, all CreateFileW.
- Fullbright: createBaseLightTextureData is hooked and brightened (829 calls), but no visible effect on 1.26. buildImage ran only twice, so the terrain lightmap is
  produced elsewhere (GPU pass?). Next: find the 1.26 lightmap path, or push a brightness term through the Anime RenderChunk shader.
- New: mods/BedrockForgeCore/logs/diag.txt (hook counters, enabled modules, screens) and logs/shaderpacks.txt (every material path), rewritten every ~10 s.
- BetterRenderDragon 1.4.4 review (reference only, AGPL, 1.20/1.21 signatures): ideas to rebuild originally - hot shader reload (discard frame + free compiled
  shader blobs), material.bin from resource packs, deferred / Vibrant Visuals unlock. Planned as Bedrock Forge "ForgeRender".

## E-020 - 2026-10-21 (unattended cloud session, no PC access: folder prompt not yet approved)
- Fullbright v3 (compiles: CI #39 green; not tested in game): LightTexture::refresh/getColorForUV are MCNAPI (no symbol) so they can't be hooked.
  New path: hook BlockTessellatorCache::getLightColor (client-only chunk tessellator) to floor sky+block light at strength*15, and
  call RenderChunkCoordinator::relightAllRenderChunkGeometry() on every coordinator when the module/strength changes (HUD tick).
  Also LevelRendererCamera::getAmbientBrightness -> 1. diag.txt: "tessellator light N (relights M)". Mobs/particles not covered yet
  (Actor::getBrightness is shared with gameplay; only hook it render-thread-only if needed).
- ForgeRender v1 (ShaderPacks.cpp + ForgeRender.cpp, original code): module id stays "shaderpacks", shown as "ForgeRender".
  * Shaderpack slider (0 = vanilla, 1..N = folders in shaderpacks/), label shows the name; writes config/shaderpack.txt; redirect
    state swapped atomically so files opened after the switch come from the new pack.
  * Resource-pack shaders: hook MinecraftGame::onActiveResourcePacksChanged -> iteratePacks -> PackInstance::getResource(
    renderer/materials/<every vanilla material name>) -> cache/packshaders/. Redirect priority: resource pack > shaderpack > vanilla.
    shaderpacks.txt lists the stack order and which pack served each material.
  * Reload: MinecraftGame::cleanReloadMaterials() on the client thread (menu toggle "Reload materials now", or auto on change).
    UNKNOWN whether it re-opens material.bin files: compare "saw N material opens" in shaderpacks.txt before/after.
  * Vibrant Visuals toggle: TextureGroup::mAdvancedGraphicsOptions -> setGraphicsMode(Advanced/Fancy), only if
    isVibrantVisualsAllowed() && canSwitchGraphicsModeInGame() (server force-disable is respected). Assumes Advanced == VV.
- Actor AlphaTest (not built: vanilla materials are on the PC). Hypotheses for "invisible mobs": (a) bfDitherKill discards when
  fade <= t, so fade = 0 near the camera kills every pixel if DitheringEnabledToggle is on for all actors; (b) camera-relative
  world position minus u_invView translation; (c) TintedAlphaTestEnabled with OverlayColor.a = 0. Next build: variant without
  dither first, then re-add.
- Impact frames now trigger on critical hits only (Settings::critOnly, default true): hooks Player::emitCriticalHitParticles
  (attacker = local player, single-player) + LocalPlayer::emitCriticalHitParticles (servers), 100 ms de-dup. Compile-only.
- User-supplied "Renderer Foundation" zip = BetterRenderDragon v1.4.4 source with the licence file removed. Not copied: its
  signatures target 1.19.80-1.20.50 and its reload offsets are version-specific, so none of it runs on 1.26 as-is. Ideas noted for
  1.26: do the material reload between frames (endFrame/discardFrame, both virtuals in LL's FrameBuilder.h) and serve
  material.bin through ResourcePackManager::load instead of a cache.
- BP script: same crit-only rule (critOnly setting, default on; isFalling && !isOnGround/isInWater/isClimbing/riding/blindness). tsc --checkJs clean vs @minecraft/server 2.3.0.
