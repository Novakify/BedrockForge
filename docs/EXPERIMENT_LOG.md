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

## E-021 - 2026-10-21 Renderer Foundation (separate AGPL project)
- Second zip "Renderer_Foundation" = upstream BetterRenderDragon v1.4.4 (identical to ddf8196/BetterRenderDragon main, AGPL-3.0)
  plus a permission notice that says it doesn't override existing licences. Fernando chose: private AGPL fork, kept separate.
- Fork lives on the orphan-history branch `renderer-foundation` of this repo (upstream history kept, NOTICE.md, LICENSE),
  worktree /home/claude/rf in the cloud. CI builds RendererFoundation.dll to branch `rf-builds`. Move it to its own repo
  (Novakify/RendererFoundation) when Fernando creates one - the integration can't create repos (403).
- 2.0.0-alpha: LeviLamina client mod; MaterialBinLoader via file-open redirect + ResourcePackManager::load; shaderpack
  folders; reload via cleanReloadMaterials; Vibrant Visuals toggle; ImGui overlay (D3D12 probe, Present/Present1,
  ExecuteCommandLists queue capture, Win32 WndProc input, F6).
- Bedrock Forge side: if RendererFoundation.dll is loaded, ForgeRender's material redirect yields to it (no code shared;
  the two DLLs only coexist).

## E-022 - 2026-10-22 (Fernando away 2 h; PC folder access + computer use granted)
- Installed b9 (abd455a) + crit-only impact BP script; built + installed "Dread" horror shaderpack (ShaderForge against the PC's
  vanilla 1.26 materials; active via shaderpack.txt = Dread). Launched the instance via computer use: title screen + world join OK,
  BF log "shaderpacks: 2 found, active = 'Dread'", Actor + RenderChunk served from Dread at boot; the world behind the pause menu
  looks desaturated/grey-fogged (screenshot). Game input still ignored (Resume click did nothing) -> gameplay tests need Fernando.
- From The Fog (Bedrock) v1 packs (branch ftf-bedrock 8950b88) installed as dev packs; new world "From The Fog Test"
  (copy of My World, only FTF BP/RP, level.dat LevelName patched). Pack stack log shows the BP loads. content_log_file turned on.
- Renderer Foundation: LeviLamina refused it ("isn't using the unified memory allocation operator") -> added MemoryOperators.cpp
  (4e519cb). Closing the game: alt+F4 works after clicking the title bar first.
- Content log (content_log_file now on) from a My World session: all three script packs load (Dweller, Sculk Horde, Impact) with
  no script errors. Found + fixed: ModPorter wrote GeckoLib keyframes ({"vector","easing"}) and GeckoLib timeline
  instructions into Bedrock animations (~21k content-log errors, Sculk Horde animations broken) -> generate.py bedrock_anim();
  the installed SculkHordeRP was patched in place (6367 keyframes, 39 files; originals in SculkHordeRP/_backup_anim_prefix).
  Dweller overlay: removed invalid `entity_cramming` damage cause + `check_if_outnumbered` (installed copy patched, backup kept).
- RF 4e519cb loads ("2 mod(s) loaded") but the D3D12 probe failed at load -> 33f2ec1 logs HRESULTs, adds a D3D11 probe fallback
  and retries on enable. RF config set to shaderPack=Dread (Dread copied to mods/RendererFoundation/shaderpacks) because
  Bedrock Forge yields material redirection to RF.

## E-023 (2026-10-22, Fernando at the PC) - animations verified, Vibrant Visuals explains "can't see shaders"
- Content log after the E-022 animation fixes: ~21k errors -> ~430. Remaining causes, all fixed in the installed packs and in
  ModPorter: GeckoLib Molang with a lost operator ("q.anim_time * 10 5") makes Bedrock drop the WHOLE animation (that was the
  "can't find animation idle/walk/attack/living" every 5 s); empty "animations": [] in controller states; geckolib_format_version
  key; catmullrom on Molang keyframes (sculk_witch throwpotion); vanilla sculk particles need variable.direction
  (MolangVariableMap - must be created lazily, "Native constructor ... cannot be used in early execution").
  Attack state now also enters on q.is_delayed_attacking. Result: My World + From The Fog Test load with ZERO content errors.
- Why the shaders looked vanilla: options.txt graphics_mode:2 = Vibrant Visuals (deferred). Dread's RenderChunk/Sky/Actor
  forward shaders don't run there. New: Dread Atmosphere (tools/DreadAtmos/build.py) = VV colour grading / atmospherics /
  lighting derived from the vanilla settings (desat 0.42, contrast 1.32, sun x0.45, grey cold skies) + a Dread fog
  (distance fog for Fancy, volumetric for VV) pushed by a tiny BP (/fog push). Verified in both worlds (screenshots).
- Adding packs to a world by editing world_*_packs.json: also add world_*_pack_history.json entries, and do it while the
  game is CLOSED for a world that was already opened in this game session (the game rewrites the list from its cache on load).
- New dev packs are only discovered at game start (PackManifestFactoryCache).
- Renderer Foundation 0b01034: no crash any more (SEH guard + swapchain queue); overlay init still fails ->
  843e0cf logs the failing step + retries 3x. Note: the LeviLamina log stops mid-line after that error (buffered).
- Dweller "stiff": keyframes were all linear, crouch_idle didn't loop (froze), 0.15 s blends -> catmull-rom on constant
  channels, looping idles, 0.3 s blends, flee speed-scaled (overlay "smooth_anims": true).

## E-025 (2026-10-22 17:45 UTC) - Cursed Fog port runs on Fancy; file switch works; reload doesn't re-read files
- CursedFog (port of Cursed Fog V1.0.10, LoLip_p, CC BY 4.0) loaded with no crash: Actor, RenderChunk, Sky served;
  black side stripes, CRT/VHS pattern, near-black night visible in My World. So ShaderForge's extra uniforms
  (ViewPositionAndTime, FogAndDistanceControl, LightWorldSpaceDirection, u_viewRect appended to the bgfx uniform table)
  are accepted by the game, and u_viewRect is filled (stripes at the right place).
- File switch: writing shaderpack.txt in a running game switched the pack in <12 s and ran cleanReloadMaterials, but the
  reload re-opened 0 material files -> materials are cached; a pack change still needs a restart. Next: find the real
  reload path (BetterRenderDragon frees the compiled shader cache).
- Crash guard was too strict: force-closing a hung game counted as a crash twice (CursedFog -> Dread -> off). Now it only
  falls back when CrashLogger wrote a new report in <game>/logs/crash after the marker.
- The game process often lingers after the window is closed (mods disabled cleanly, window stays). LeviLauncher's
  "Force Launch" starts a fresh instance.
