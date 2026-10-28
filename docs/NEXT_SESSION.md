# Next session handoff (updated 2026-10-28 ~13:00 local, PC session, computer use)
- VV IMPACT FRAME COMPLETE + VERIFIED (Hysteria, VV, real 4-tick crit timing): whole world white, hit pig black.
  Black target = tools/HurtColorGen (NEW): copies the 211 vanilla render controllers that leave is_hurt_color unset with
  is_hurt_color = opaque red (alpha is clamped to 1; VV's ActorPrepass blends it ~50%: lerp(albedo, OverlayColor.rgb,
  OverlayColor.a), decoded from DXIL) into dev RP BedrockForgeImpact/render_controllers (generated, gitignored).
  ColorPostVV tests the RAW scene (before pack corrections): red > VV_HURT_RATIO (1.8) x max(green, blue) -> black.
  Known: strongly red blocks (red wool, redstone) also go black in the frame; custom-entity RPs (Dweller, Sculk Horde)
  keep their own controllers -> run HurtColorGen-style patch on them if their mobs don't go black. Hurt flash on Fancy is
  now opaque red too (Fancy detection still keys on red OverlayColor - unchanged, but re-verify on Fancy).
  BP: VV marker held 4 ticks past the fog (VV_EXTRA_TICKS; grading overrides fade in) + no double loops on repeat hits.
  Robust alternative if false positives annoy: replace ActorPrepass GeometryPrepass (3 MRT outputs, 36 variants/pass).
- HYSTERIA VV SKY (Fernando: "still milky"): measured Fancy sky 90-125 grey vs VV 120-160 cyan. Cause = VV auto exposure
  re-brightening + cyan scatter. Fixed in the VV post pass (new per-pack knobs in ColorPostVV: VV_WHITE_BALANCE,
  VV_SATURATION, VV_DEHAZE; Hysteria: bias 0.32, dehaze 0.45, sat 0.7, WB 1.06/1/0.90) + grey atmosphere (sat 0).
  Now VV sky ~100-130 neutral grey. NOT POSSIBLE with settings: Hysteria's own clouds + sun glow on VV - tested: VV does
  not draw the Sky material on screen (red test sky invisible), and post has no sky mask (scene alpha = 1 everywhere).
  Would need replacing VV's deferred lighting materials (DeferredMixedResolution*) - parked, ask Fernando first.
- /function bfiref = noon, clear, fixed camera (6.35 72 -199.87, yaw 161, pitch 8) for Fancy-vs-VV comparison shots.
- Leftover client-biome files for legacy names in the 5 VV RPs (mega_spruce_taiga, mesa_mutated...) log harmless
  BiomeRegistry errors; VVPackGen no longer writes them - delete them when delete permission is granted.

# Next session handoff (updated 2026-10-28 ~12:10 local, PC session, computer use)
- VV LOOK PER PACK: tools/VVPackGen (NEW, see its README) builds a VV resource pack per shaderpack from the game's own
  vanilla VV JSON (brarchive) + profiles.json: atmospherics, volumetric fog, lighting, client biomes, shadows.
  Installed in development_resource_packs: HysteriaVVRP (regenerated, same UUID, active in the beach "My World"),
  InsanityVVRP, AnimeVVRP, DreadVVRP, CursedFogVVRP (built, NOT yet activated in any world / not seen in game).
  Old HysteriaVVRP backed up to com.mojang/_bf_backups/HysteriaVVRP.pre-vvgen.
  Hysteria VERIFIED in game: Fancy reference = grey overcast, sun glow, heavy grey fog, dark greens. VV now matches the mood
  (fog max_density 0.35 to y260, desaturated grey sky, sun x0.4, sky light 0.5) + VV_EXPOSURE_BIAS 0.55 -> 0.42 (private src).
  Still a bit brighter than Fancy; tune profiles.json -> build -> rejoin. Gotchas: fog ids can't be minecraft:*,
  "weather" only under volumetric.density.
- IMPACT FRAMES ON VV: WHITE WORLD WORKS (verified). FogColor never reaches VV's post pass, so the BP now sets the player's
  VV colour grading midtones GAIN to (4, 0.05, 4) for the frame (vanilla gains are always 1; the game blends overrides in, so
  ColorPostVV tests thresholds x,z > 1.6 & y < 0.8). Tiny midtones OFFSET markers did NOT survive (don't retry that).
  Setting vvShader (default true) skips the old posterize grading. All 5 packs' ColorPostProcessing rebuilt + installed
  (backups *.pre-vvimpact). Test: /function bfitest (pig 3 ahead, camera down), /function bfivv (impact, 3 s hold).
- BLACK TARGET ON VV: NOT DONE. VV shows NO red hurt overlay on the pig (checked frame by frame), so the post pass can't find
  the target. NEXT options: (a) ActorPrepass replacement: when FogColor is magenta and OverlayColor red -> write black albedo,
  post then keeps near-black pixels black (needs the GeometryPrepass MRT output decoded); (b) RP render controller override
  making is_hurt_color black (but Fancy's detection keys on red OverlayColor - would need both changed).
- Shaders only reload on a GAME RESTART (VV<->Fancy flip reloaded ColorPostProcessing once, then cached). Restart = Save &
  Quit, window X (closes fully now), then open the instance's Minecraft.windows.exe app (~40 s to menu).
- Debug: #define BF_DEBUG_GRADING in a pack's ColorPostProcessing/Fragment.hlsl draws the grading uniforms as top bars.
- Git: the PC device shell (Linux VM) has git; the cloud session pushed via add_repo. Windows Git installer still in
  Downloads\BedrockForge-Installers (only needed for pushing from Windows).
- Untouched leftovers in the instance BP functions (unregistered, safe to delete): bfivvlong, bfmon, bfmoff.

# Next session handoff (updated 2026-10-28 ~10:50 local)
- VV SHADERS: HOOK PROVEN. Packs can replace VV's final pass (ColorPostProcessing): DebugVV pack turned the world red
  on VV (UI untouched). shaders/src/_shared/ColorPostVV.fragment.hlsli = vanilla exposure path + raster/UI composite
  (decoded 1.26 DXIL) + per-pack packGradeVV(). Installed in ALL 5 packs (Anime ACES+sat, Dread ACES+desat+grain,
  CursedFog cfFinal, Hysteria hysFinal(rec709->2020) bias 0.55, Insanity insFinal). Verified Hysteria on VV in game:
  60 fps, Hysteria grade active, but the scene is too bright/hazy = VV's own atmosphere/fog. NEXT: per-pack VV RP with
  atmospherics + fog (+ lighting) tuned to each pack (HysteriaVVRP from another session has lighting+shadows only,
  and is active in the newest "My World"). Then impact frames on VV (white world possible in ColorPostProcessing via
  @uniform FogColor if bgfx keeps it set; black target needs the Actor*Prepass/ForwardPBR materials).
- Another session made shaderpacks/HysteriaVV (same idea, ColorPostProcessing only) - superseded by Hysteria's own VV pass;
  shaderpack.txt is now "Hysteria" (works on Fancy AND VV). DebugVV + DebugFog are debug packs only.

> (Merged 2026-10-28: the two UTC blocks below came from the cloud/GitHub side; items 1 and 5 of that checklist are done - see the blocks above.)
# Next session handoff (updated 2026-10-28 ~07:45 UTC, PC connected, Fernando away - nothing tested in game)
TOMORROW'S CHECKLIST (one at a time, in this order; each line says how to undo it):
1. Hysteria VV (runbook section 2, installed): launch -> settings say Vibrant Visuals; soft sun shadows that move;
   Hysteria colours; amber moon; UI not graded; switch back to Fancy/Hysteria without crash; F3 FPS.
   Logs: shaderpacks.txt "ColorPostProcessing <- HysteriaVV". Undo: shaderpack.txt = Hysteria, options graphics_mode:1,
   modules.json vibrant false (all have .bak-20261028).
2. Dread Atmosphere script (runbook 3b, installed): only in worlds My World AWJgm+UJIEM= / From The Fog Test / test.
   Leaves + dust still fall; /scriptevent bf:amb x prints nothing; ContentLog 0 errors. Undo: rename main.js.debug-bak back.
3. Motion blur (runbook 3a): nothing changed on the PC - stays OFF as Fernando asked (all postfx.json 0). Repo CursedFog 0 too.
4. Spatial GUI M0 spike (RF branch spatialgui-m0, commit cb579bb, CI run 40648827763 green, artifact
   RendererFoundation-client-windows-x64-cb579bb... kept until 2027-01-26). NOT installed: this session had no way to
   copy a DLL to the PC. Install = rename RendererFoundation.dll -> RendererFoundation.5246159.dll, copy the artifact's
   RendererFoundation.dll in, set "uiCaptureSpike": true in RendererFoundation.json, inventory open + F7.
   Steps + how to read report.txt: docs/PORT_SPATIALGUI.md "M0 status". ui_shaders.txt already on the PC.
   Undo: rename the 5246159 DLL back (uiCaptureSpike false also makes the new DLL behave like the old one).
5. ZIP-copy sync (from the "PC runbook section 0" chat): Downloads\BedrockForge-main holds VERIFIED sky fix v2
   (shaders/src/_shared/SkyFull.vertex.hlsli) + ShaderForge --impact + Anime/Dread Actor+Sky + Dread/CursedFog Clouds
   (FogColor) - never committed. Folder access was refused while Fernando was away -> approve access to
   ~\Downloads\BedrockForge-main next session, then diff those files against main and commit.
6. Cinematic Interactions M1: BLOCKED - needs Spatial GUI M1, which needs the M0 result (item 4). Not started.
Other: impact-frame white/black work from the section-0 chat is still open (DebugFog pack installed for it).

# Next session handoff (updated 2026-10-28 ~07:10 UTC, PC connected)
INSTALLED, NOT YET SEEN IN GAME: GOAL 1 Hysteria-VV real shadows (runbook section 2, findings written there).
- shaderpacks\HysteriaVV\ (renderer/materials/ColorPostProcessing.material.bin, postfx.json = Hysteria's bloom+CAS, src\ PRIVATE).
  Build report: ColorPostProcessing 2 fragment blobs replaced, 0 kept vanilla.
- RP "Hysteria VV" (uuid 09d44b3b..., from tools/HysteriaVV/build.py) in development_resource_packs\HysteriaVVRP, added at the TOP
  of My World (+A1sY8g-qtk=, the one played 10-28) world_resource_packs.json (backup .bak-20261028).
- config\shaderpack.txt = HysteriaVV (old: shaderpack.txt.hysteria.bak). modules.json shaderpacks.settings.vibrant = true
  (backup .bak-20261028). options.txt graphics_mode:2 = Vibrant Visuals (backup options.txt.bak-20261028).
- Rollback: shaderpack.txt = Hysteria, options graphics_mode:1, vibrant false (or restore the .bak-20261028 files).
- FIRST THING after Fernando's test: logs\shaderpacks.txt must list "ColorPostProcessing <- HysteriaVV"; ContentLog 0 errors for
  Hysteria VV; check config\shaderpack.crashed (crash guard). Tunables: orbital_offset_degrees sign (40) and moon brightness.
- Not in repo/handoff before: someone installed a Hysteria SKY FIX on 2026-10-28 02:38 UTC (src Sky/Vertex.hlsl, Stars, SunMoon,
  _shared/SkyFull.vertex.hlsli; Sky.material.bin.pre-skyfix kept; BUILD_REPORT.skyfix.txt). Its source is only on the PC.
- ShaderForge works on the PC shell too (lazurite + dxc installed in the device VM; copy of tools/ShaderForge md5-verified).
STILL OPEN: runbook section 3 (motion blur, Dread counters), section 4 sky (check if the 10-28 sky fix solved it).

# Next session handoff (updated 2026-10-28 ~01:55 local) - SUPERSEDES the impact/VV lines of the block below
- IMPACT FRAMES ON FANCY: WORKING, verified in game (Hysteria): whole world white, hurt pig black.
  Why v1 failed: the game FADES fog colour (~0.3 s in, ~0.5 s out; measured with the DebugFog pack), so pure magenta
  was never reached. Detection now = fog heading to magenta: min(r,b) - g > 0.3*max(r,b) (ShaderForge --impact).
  BP: fog held WHITE_FRAME_TICKS=4, overlapping crits extend the same frame (no double push); bfioff removes x3.
  All 5 packs rebuilt + installed. Particles (vanilla Particle material) stay as is (grey specks) - optional later.
  Shaderpack "DebugFog" left installed (debug only; never set it as active for play).
- SHADERS ON VIBRANT VISUALS: NOT DONE - needs its own session. Findings: VV draws with *Prepass (G-buffer),
  Deferred*, *ForwardPBR, then ColorPostProcessing (tonemap + VV colour grading; 24 uniforms, s_ColorTexture,
  s_RasterizedColor, s_AverageLuminance...). Options: (a) port each pack's final grade into a ColorPostProcessing
  replacement (decode vanilla tonemap first - big), (b) grade pass in Renderer Foundation's post-FX at Present (C++/CI,
  works in every mode), (c) per-pack VV resource pack (color_grading/atmospherics/fog JSON) for the look + VV shadows.
  Impact frames on VV still use the old BP posterize (grading API); white/black on VV needs (a) or (b).
- Game left on FANCY graphics (VV -> Fancy for testing). My World.

# Next session handoff (updated 2026-10-28 ~01:35 local, PC session, computer use)
Fernando's 2026-10-28 report -> status:
- SKY half missing / grey / no clouds: CAUSE = sky fix v1 folded LOCAL space (sky plane local y=0, height is in u_model)
  -> fold never ran + vertical direction lost. FIXED v2 (_shared/SkyFull.vertex.hlsli folds world space). VERIFIED in game
  (Hysteria, Fancy, y200: one continuous shader sky to the horizon, Hysteria clouds visible, sun drawn).
- Motion blur: "motion_blur": 0 in every postfx.json (backups postfx.json.pre-noblur). Only CursedFog had it on.
- Mob speed: Dweller BP movement 0.53/0.4/0.27 (was 0.6/0.45/0.35, original 0.5/0.35/0.21), chase x1.05 (was 1.35);
  knockback_resistance kept 0.6. Sculk Horde + Dweller RP walk/run anim_time clamp(speed*1.6, 0.6, 1.2) (was *3.0, 0.75..2.5
  -> legs ran up to 2.5x = "too fast"). Boss movement values were never changed. Backups: _backup_animspeed/, ice_bear_2026.json.fast.bak.
- Impact frames checkerboard on Fancy: native overlay texture failed to load -> config.json impact.useFrameTextures=false.
  The B/W part only ever worked on VV because the BP uses VV colour grading.
- NEW white-world / black-target impact frame for Fancy (UNVERIFIED - first in-game test showed NO white):
  BP pushes fog "bf:impact" (#FF00FF, RP fogs/bf_impact.json) for 3 ticks; ShaderForge --impact wraps every fragment that
  sees FogColor: white, actors with red OverlayColor (hurt) black. All 5 packs rebuilt with it (backups renderer_backup_pre_impact/).
  The shader did NOT react -> FogColor uniform is not the raw fog_color (blended/dimmed/transitioned?). NEXT: shaderpack
  "DebugFog" (installed: left half terrain = FogColor, right half = FogAndDistanceControl) -> shaderpack.txt=DebugFog, restart,
  /function bfion vs bfioff, read the colours, then pick the detection (colour ratio or fog distances). If neither is usable:
  native path (RENDERING_PLAN R4: BF sets a uniform from ImpactFrames.cpp) - needs C++ + CI.
  Test helpers in BedrockForgeImpactBP/functions: bfitest (creative/noon/clear/pig), bfihit (damage pig + push fog), bfion, bfioff.
- Shaders on Vibrant Visuals: NOT DONE. BF packs only replace Fancy materials; VV draws with *ForwardPBR / deferred
  materials, so on VV nothing of the pack applies (confirmed: only SunMoonForwardPBR/ActorForwardPBR etc. opened).
  = runbook section 2 (Hysteria-VV mode), a project of its own.
- Computer use notes: typing ":" "@" "~" goes via clipboard and pastes the USER's clipboard -> put commands in .mcfunction
  files and run /function <name>. Game X button closes the window but the process lingers; LeviLauncher then ignores
  Launch (stuck "waiting for game window") - Fernando must end Minecraft.Windows.exe in Task Manager.
Repo copy (Downloads ZIP, no git): tools/ShaderForge/build.py (--impact), shaders/src/_shared/SkyFull.vertex.hlsli v2,
  Anime/Dread Actor+Sky, Dread/CursedFog Clouds (FogColor), postfx.json motion_blur 0. Commit these.

# Next session handoff (updated 2026-10-27 evening, PC session via device shell)
DONE: PC_RUNBOOK section 0 verified live (table rewritten - NOTE game data lives in <instance>\Minecraft Bedrock, no repo checkout
on the PC, RF is ENABLED). Section 4 sky fix v1 INSTALLED in all 5 BF packs (Anime, Dread, CursedFog, Hysteria, Insanity):
Sky vertex fold (shader sky now covers below the horizon) + per-pack SunMoon/Stars. Details + rollback: PC_RUNBOOK section 4.
NOT PUSHED: this session had no git access. Changed repo files (in Downloads\BedrockForge-main ZIP copy):
  docs/PC_RUNBOOK.md, docs/NEXT_SESSION.md, shaders/src/_shared/SkyFull.vertex.hlsli,
  shaders/src/{Anime,Dread,CursedFog}/{Sky/Vertex.hlsl, SunMoon/Fragment.hlsl, Stars/Fragment.hlsl}  -> commit these.
  Private (never commit): shaderpacks/{Hysteria,Insanity}/src/{_shared/SkyFull.vertex.hlsli, Sky/Vertex.hlsl, SunMoon/, Stars/}.
NOT DONE: real sun direction / time of day + moving sky clouds -> needs R4 native uniform hook (C++, CI). Untested in game.
FERNANDO TO TEST: restart game, flat world, y 200: horizon / up / down at noon, sunset, night, per pack. Report seams,
  missing terrain below the horizon, or any vanilla band.

# Next session handoff (updated 2026-10-27, cloud-only session - PC was not connected)
DONE + PUSHED: docs/PORT_SPATIALGUI.md (goal 3), docs/PORT_CINEMATIC.md (goal 4), docs/PC_RUNBOOK.md (exact steps
for everything that needs the PC). Nothing installed, no DLL/pack change, PC state same as 2026-10-23 below.
NOT DONE (needs PC): goal 1 Hysteria-VV shadows, goal 2 motion blur check + Dread Atmosphere debug counters,
sky bug. Follow docs/PC_RUNBOOK.md sections 0 -> 4. Start with section 0 (verify PC paths - never confirmed live).
FERNANDO'S TEST 2026-10-27: mob animations good; FPS 15-18 (was 42-47); SKY BROKEN in both shader skies (upper half
shader but stationary, lower half vanilla). His rule: get this mod perfect before starting any other mod port.
Cloud findings: only Sky (+Clouds) material replaced -> sun/moon/stars/lower sky vanilla; no sun-dir/time uniform.
Motion blur: RF math OK; only CursedFog's postfx.json enables it (1.25) -> likely why it was never seen.
Dread Atmosphere: leaves/dust/counters script exists only on the PC (repo builder still fog-only) -> sync back.
GeckoBridge: deferred by Fernando; branch geckobridge/compat-audit-20261025, do not merge, test JARs off GitHub.

# Next session handoff (updated 2026-10-23, stopped early: user near weekly usage limit)
INSTALLED + VERIFIED IN GAME: BF 361836d (prev .d3a2759.dll), RF 5246159 (prev .a0f4909 / .ec06850). Active pack Hysteria.
- RF F6 overlay works (D3D12). Clicking it crashed the game once (std::mutex re-entered via SetCapture) -> fixed in 5246159.
- Post effects run (F6 "Post effects": bloom/CA/CAS/motion blur, from shaderpacks/<Pack>/postfx.json via BF_GetPostFx).
  CA + bloom seen in game. Motion blur (camera rotation only) NOT visually checked (could not turn the camera by script).
- Leaves now get the packs' shading + wind (Seasons variants). Hysteria clouds fixed (no streaks), ~42-47 fps.
- Ambient falling leaves + dust: Dread Atmosphere BP/RP (gitignored packs/). Works ("/scriptevent bf:amb x" prints
  counters); My World's trees are mostly non-leaf blocks so few leaves fall there. Debug counters still in main.js.
- Effects run on the swapchain image, so they also touch the HUD; they pause in menus.
TODO: real shadows (Hysteria-VV mode: VV JSON + ColorPostProcessing replacement); verify motion blur; Insanity/CursedFog
  postfx in game; tidy dust density; remove debug counters; FPS of Hysteria; earlier OPEN list below still stands.

# Next session handoff (updated 2026-10-22 20:05 UTC)
VERIFIED IN GAME TODAY: CursedFog, Insanity, Hysteria, Dread shaderpacks on Fancy (previews in chat); Dread Atmosphere on
VV; mob animations (0 content errors; Dweller walks/stalks/attacks); BF+RF coexistence; crash guard.
INSTALLED: BedrockForgeCore d3a2759 (prev .2a13305.dll). Active pack: Hysteria. RF disabled (manifest.json.disabled),
RF DLL 843e0cf; build e2757eb (Present logging) is on rf-builds - install + enable to find why the overlay never inits.
PRIVATE PORTS: Insanity / Hysteria sources only on the PC (shaderpacks/<Pack>/src) + /home/claude/private_ports (cloud,
temporary). Never commit them (AGNYA licence). Cursed Fog is CC BY 4.0 -> in repo (shaders/src/CursedFog).
Switching packs: write mods/BedrockForgeCore/config/shaderpack.txt then RESTART (live reload doesn't re-read materials).
Computer use: chat commands can be typed (/ then type + Return). Close: Save&Quit, title-bar X, then LeviLauncher
Launch -> "Force Launch" (old process lingers). Approvals expire after 30 min idle; ask the user in chat first.
OPEN: real time-of-day/sun direction for shaders; RF overlay; full-screen pass (bloom, chromatic aberration, motion blur,
CAS); Hysteria clouds; Nether/End variants; FPS of Hysteria (~40); live material reload; Fullbright v3 / zoom / hit
colour / impact crit-only / Angel of Reaping still untested by hand; remaining client modules (freelook, block outline,
nametags, hitboxes); leftover .tar files in mods/BedrockForgeCore (need delete permission).

# Next session handoff (updated 2026-10-22 17:00 UTC)
INSTALLED, NOT YET SEEN IN GAME (needs one launch from LeviLauncher):
- BedrockForgeCore 2a13305 (prev: .29f18fc.dll, .b9.dll). New: shaderpack.txt authoritative + watched (live switch),
  config/command.txt (reload | vibrant on|off | rescan), BF keeps serving when RF loaded, CRASH GUARD
  (config/shaderpack.running; if the last run crashed with pack X -> config/shaderpack.fallback = Dread; writes shaderpack.crashed).
- Active pack: CursedFog (shaders/src/CursedFog = port of Cursed Fog V1.0.10, LoLip_p, CC BY 4.0; see PORTING.md).
  Uses ShaderForge's new // @uniform (ViewPositionAndTime, FogAndDistanceControl, LightWorldSpaceDirection, u_viewRect
  appended to the fragment uniform tables). FIRST THING: launch -> My World -> screenshot; check logs/shaderpacks.txt and
  logs/crash. If u_viewRect isn't filled, stripes/CRT use 1920x1080 fallback (would look offset at other sizes).
  Verify: day red tint + desat 45%, black side stripes, CRT/vignette, grain, exp2 fog, no clouds; night very dark/purple.
- Dread Atmosphere BP now CLEARS the Dread fog (Cursed reads the real fog colour). RP stays (VV grade).
- Fancy graphics (graphics_mode:1), fullbright off. RF disabled; its config materialBinLoaderEnabled=false.
CURSED FOG TODO: SunMoon/Stars materials (stage them), water waves (gbuffers_water), Nether/End (world-1/world1),
entity variants with Change_Color/Emissive, chromatic aberration + interlace + motion blur -> RF full-screen pass.
MORE HORROR PACKS (Modrinth, licence checked): Hysteria (3.7M dl, AGNYA: private mods allowed, public needs permission;
Photon-based, 18k lines), Insanity (8M, AGNYA, 11k lines), Somber (MIT, 660 lines), Lost Signal VHS (MIT, ~2k lines),
Ancient Dream (GPL-2.0), Optimism (MIT), HyShaders Horror (MIT). Downloads in /tmp/claude-0/dl/others (cloud, temporary).

# Next session handoff (updated 2026-10-22 16:00 UTC)
STATE ON THE PC (1.26.51.01 instance):
- options.txt graphics_mode:1 (FANCY; was 2 = Vibrant Visuals, backup options.txt.bak-vv). Fullbright module OFF (backup modules.json.bak-fb).
- Custom shaders DO run on Fancy: proven with the DebugRed pack (all terrain red). Dread's blobs replace the same slots.
- Why Dread looked vanilla: (1) the ForgeRender "pack" slider is an INDEX saved in modules.json and wins over shaderpack.txt;
  adding DebugRed shifted Dread 2->3 so slot 2 = DebugRed; (2) with Renderer Foundation loaded BF stopped redirecting
  (yield) and RF's loader served nothing. 9461ee7 fixes both: shaderpack.txt is authoritative + WATCHED (write a name into
  it -> switch + reload), config/command.txt = reload | vibrant on|off | rescan, BF keeps serving with RF loaded.
- Game left RUNNING in My World with DebugRed (red terrain) when Fernando left; screen locked -> computer use blocked
  ("desktop shell is frontmost"). Don't call request_access unattended.
- To finish: install the 9461ee7+ DLL (rename the loaded DLL first - renaming works while the game runs), shaderpack.txt=Dread,
  next launch -> verify Dread on Fancy, then test live switching by writing shaderpack.txt / command.txt=reload and reading
  logs/shaderpacks.txt ("cleanReloadMaterials ran", material open counts).
- Dread Atmosphere RP+BP (VV grade + fog) on in My World + From The Fog Test. Content logs: ZERO errors in both worlds.
- Renderer Foundation: manifest.json.disabled; DLL = 843e0cf (init step logging). Overlay init failed on 0b01034 (reason
  truncated in log). Keep disabled until the overlay works.
GOAL (Fernando): shaders must look 1:1 like the Java shaderpack, not similar. Assumed Cursed Fog (LoLip_p, CC-BY-4.0, Modrinth);
asked him to confirm + allow modrinth.com / api.modrinth.com / cdn.modrinth.com (blocked in cloud AND on the PC sandbox).
Port plan: gbuffers_* -> RenderDragon materials (RenderChunk/Actor/Sky/Clouds/Weather...), composite/final -> needs a
post-process pass (Fancy has none: inject a full-screen pass at Present or use VV's ColorPostProcessing material).

# Next session handoff (updated 2026-10-22 14:40 UTC)
STATE ON THE PC (1.26.51.01 instance; folder access granted):
- BedrockForgeCore b9 (abd455a). Dread shaderpack v2 active (BF serves it: mods/BedrockForgeCore/shaderpacks/Dread, shaderpack.txt=Dread).
- Renderer Foundation: DLL 0b01034 installed but DISABLED (manifest.json renamed manifest.json.disabled) because 33f2ec1 crashed the
  game in ImGui_ImplDX12_CreateDeviceObjects (legacy init had no command queue). 0b01034 takes the queue from the swapchain, uses
  ImGui 1.91 InitInfo, and wraps the overlay in an SEH guard. To test: rename manifest.json.disabled -> manifest.json, launch, check
  logs/latest.log for "[RF] overlay ready" and logs/crash/. RF config shaderPack=Dread (copy in mods/RendererFoundation/shaderpacks).
- Animations: installed SculkHordeRP fixed in place (GeckoLib keyframe objects + channel-level {"vector"} values -> Bedrock; timeline
  instructions dropped; walk/run anims sped up with movement). generate.py does the same for future ports. NOT YET VERIFIED IN GAME:
  enter My World, then grep the newest Minecraft Bedrock/logs/ContentLog*.txt for "[Animation][error]" (expect 0).
- Dweller faster: movement 0.6/0.45/0.35 (stand/crouch/crawl), chase melee speed x1.35 (installed BP + overlay in repo).
- From The Fog (Bedrock) v1 installed; world "From The Fog Test". content_log_file=1 in options.txt (backup options.txt.bak-contentlog).
- Computer use: the game window is "Minecraft.windows" (minecraft.windows.exe, resolve only while running). Grants expire after 30 min
  idle and a re-request BLOCKS until someone approves -> overnight runs lost 8 h waiting. Don't re-request unattended.

# Next session handoff (written 2026-10-21 ~20:30 UTC)
State: all work committed + pushed to github.com/Novakify/BedrockForge (main). CI build #39 (commit ec1ab0a) green -> DLL on `builds`.
PC instance: C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01\mods\BedrockForgeCore
(installed = b9 / commit abd455a, md5 1f9783e6..., backup BedrockForgeCore.b8.dll). Logs: mods/BedrockForgeCore/logs/diag.txt + shaderpacks.txt (rewritten every ~10 s).
Folder access to the instance is now granted (persists for later runs).
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
