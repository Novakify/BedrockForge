# PC runbook - work that needs Fernando's PC (written 2026-10-27, cloud-only session)

Follow this top to bottom. Nothing here is guessed from memory: each step says what to check first.
Fernando checks the game himself; no screenshots unless he asks.

## 0. PC file map (VERIFY FIRST - these come from older handoffs, never confirmed live)
Run once at the start of every PC session (device shell), and fix this table if anything moved:
```
dir "C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01\mods"
dir "C:\BedrockModding" /s /b /ad | findstr /i "BedrockForge Renderer ShaderForge shaderpacks Hysteria Insanity Dread"
```
VERIFIED LIVE 2026-10-27 (session with device shell; `<instance>` = `C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01`):
| What | Location (verified) | In GitHub? |
|---|---|---|
| Game instance (1.26.51.01) | `<instance>` OK | no |
| BF client DLL | `<instance>\mods\BedrockForgeCore\BedrockForgeCore.dll` OK (2026-10-22 21:05) + backups `.prev .b2-.b9 .29f18fc .2a13305 .d3a2759` | source: `main`, DLL: `builds` |
| BF config | `...\config\` OK: shaderpack.txt = Hysteria, shaderpack.crashed = Dread (stale, 2026-10-22), shaderpack.fallback = Dread, no .running | no |
| BF logs | `...\BedrockForgeCore\logs\` diag.txt + shaderpacks.txt OK; `<instance>\logs\latest.log` + `crash\` (3 minidumps, 2026-10-22) | no |
| Shaderpacks served by BF | `...\shaderpacks\` Anime, CursedFog, DebugRed, DebugSun, Dread, Hysteria, Insanity (postfx.json in CursedFog/Dread/Hysteria/Insanity) | Dread/Anime/CursedFog yes |
| Private shader sources | `...\shaderpacks\Hysteria\src`, `...\Insanity\src` OK (AGNYA: NEVER commit) | **no** |
| Renderer Foundation | `<instance>\mods\RendererFoundation\` OK - DLL + manifest.json ENABLED, config\RendererFoundation.json, shaderpacks\Dread | source: `renderer-foundation`, DLL: `rf-builds` |
| Vanilla materials (ShaderForge input) | `<instance>\data\renderer\materials\` OK (158 files incl. Sky, SunMoon, Stars, Clouds, Cubemap, LegacyCubemap, EndSky) | no (Mojang files) |
| Vanilla VV settings | packed in `<instance>\data\resource_packs\vanilla\__brarchive` (no loose lighting/atmospherics folders) | no |
| **Game data (worlds, dev packs, content logs)** | **`<instance>\Minecraft Bedrock\Users\...`** - LeviLauncher keeps its OWN copy; `%APPDATA%\Minecraft Bedrock` is the Store game's, not this instance's | no |
| Dread Atmosphere BP (live, edited) | `<instance>\Minecraft Bedrock\Users\Shared\games\com.mojang\development_behavior_packs\DreadAtmosphereBP\scripts\main.js` (has the `bf:amb` handler) | builder only |
| Repo checkout on PC | **NONE.** `C:\BedrockModding\Source\BedrockForge` is an empty skeleton (Examples/SDK/Tools). git not installed. 2026-10-27 used a GitHub ZIP at `C:\Users\Fazzman175\Downloads\BedrockForge-main\BedrockForge-main` (no .git) | = GitHub |
| GeckoBridge research | branch `geckobridge/compat-audit-20261025` (deferred, do not merge; test JARs stay off GitHub) | yes |

Device-shell toolchain (Linux sandbox on the PC, rebuilt each session): `pip3 install lazurite`; dxc **v1.8.2505.1** linux tarball
from GitHub releases (v1.9 needs glibc 2.38, sandbox has older); point `tools/ShaderForge/build.py` DXC + LD_LIBRARY_PATH at it.
Leftover `.tar` files in `mods\BedrockForgeCore` still there (no delete permission asked).

## 1. Standard change workflow (every DLL / pack change)
1. Code in the cloud clone -> commit -> push to `main` (BF) or `renderer-foundation` (RF).
2. Wait for CI (`.github/workflows/build.yml`) -> DLL lands on `builds` / `rf-builds`. Note the short hash.
3. On the PC: **rename** the loaded DLL to `<name>.<oldhash>.dll` (works while the game runs), copy the new DLL in.
   Packs: copy the folder next to the old one first, then switch `config\shaderpack.txt`.
4. Restart the game: Save & Quit -> close window -> LeviLauncher Launch -> "Force Launch" if the old process lingers.
   Pack switches also need a restart (live reload doesn't re-read materials).
5. Read logs, not screenshots: `logs\shaderpacks.txt`, `logs\diag.txt`, `<instance>\logs\latest.log`, `logs\crash\`,
   and the newest `ContentLog*.txt` for add-on errors. Crash guard: if `config\shaderpack.crashed` exists, the pack
   crashed and BF fell back to Dread.
6. Tell Fernando exactly what to look at (list per goal below). He reports back.
7. Rollback = rename the `.oldhash.dll` back. Never delete files (no delete permission; move to `_to_delete\`).
8. Update `docs/NEXT_SESSION.md` (installed hashes + what's verified) and push before stopping.
Approvals for computer use expire after 30 min idle and a re-request blocks: ask in chat first, never unattended.

## 2. GOAL 1 - Hysteria real shadows (Hysteria-VV mode)
**Why VV:** Fancy has no shadow maps; Vibrant Visuals does. So "real shadows" = run on VV, and make VV look like
Hysteria: (a) VV settings JSON tuned to Hysteria, (b) Hysteria's final grade in VV's ColorPostProcessing material,
(c) RF post-FX (bloom) as today. Honest limit: in VV the terrain shading is VV's own (deferred), not Hysteria's
gbuffers code; the look comes from settings + grade + post. Fancy Hysteria stays untouched as its own pack.

Steps:
1. Verify file map (section 0). Stage to the cloud (temporary, never commit): `shaderpacks\Hysteria\src\` (composite
   / final / lib grading files), vanilla `ColorPostProcessing.material.bin`, and the vanilla VV settings files.
2. Inspect `ColorPostProcessing` with lazurite (`pip install lazurite`; `tools/ShaderForge/lz.py` has the 1.26 fix):
   list passes, variants, fragment uniforms + samplers. Write what it has into this file (it decides what the grade
   can read - expect scene color + exposure/tonemap params, no depth).
3. New builder `tools/HysteriaVV/build.py`, same shape as `tools/DreadAtmos/build.py` (reads vanilla brarchives on
   the PC, writes RP with `capabilities: ["pbr"]`): sun angle/intensity, shadow strength, sky/fog colours, exposure
   and grading tuned to Hysteria's values (read the numbers from Hysteria's settings, don't eyeball). If the vanilla
   pack has a shadows settings file (`minecraft:shadow_settings`, `shadow_style`/`texel_size` in recent versions -
   VERIFY in the vanilla files), set soft shadows there. Output name: "Hysteria VV".
4. ShaderForge pack `shaders/src/HysteriaVV/ColorPostProcessing/Fragment.hlsl` = port of Hysteria's final tonemap +
   grade + vignette. Private (AGNYA) -> keep the source in `shaderpacks\HysteriaVV\src` on the PC and
   `/home/claude/private_ports` in the cloud only; add the folder to .gitignore BEFORE creating it.
   Build: `python tools/ShaderForge/build.py <vanilla materials> <HysteriaVV src> -o <out>/HysteriaVV`.
   ShaderForge keeps any variant vanilla if the signature doesn't match, so a bad build can't break the pipeline.
   Check its report for "kept vanilla" lines - if every variant stayed vanilla, the grade isn't active.
5. postfx.json for HysteriaVV: copy Hysteria's (bloom), no motion blur unless Fernando wants it.
6. Install: `shaderpacks\HysteriaVV\` next to Hysteria; RP "Hysteria VV" into My World; `config\shaderpack.txt` =
   HysteriaVV; `config\command.txt` = `vibrant on`. Restart.
7. Logs: shaderpacks.txt must show `ColorPostProcessing <- HysteriaVV` served; ContentLog 0 errors for the RP.
**Done 2026-10-28 (installed, not yet seen in game).** Findings:
- Vanilla `data\renderer\materials\` holds ColorPostProcessing; VV settings live in `data\resource_packs\vanilla_1.21.90\__brarchive\`
  (color_grading, lighting, atmospherics, pbr, shadows, water). `shadows` = `minecraft:shadow_settings`
  {shadow_style blocky_shadows|soft_shadows, texel_size} -> RP file `shadows/global.json` (MS docs confirm).
- ColorPostProcessing (1.26.51): 1 pass `ColorPostProcess`, 1 variant (no flags). Fragment in: SV_Position + TEXCOORD0.xy.
  Textures t0 AverageLuminance, t1 ColorTexture (HDR scene, linear 709), t2 CustomExposureCompensation (curve),
  t3 PreExposureLuminance, t5 RasterizedColor (UI layer, premultiplied). cbuffer = 24 vec4 (VV grading + tonemap). No depth.
  Decoded vanilla flow: if TonemapParams0.z>0 undo pre-exposure (c /= 0.18/preExp + 1e-4); avg = ExposureCompensation.z>0.5 ?
  clamp(AverageLuminance, LumMinMax.x, .y) : 0.18; comp = ExposureCompensation.y, or mode(.x)==1: 1.03-2/(log10(avg+1)+2),
  mode>1: curve texture at normalized log2(avg); white balance; per-zone grading; c *= 0.18/avg*comp; tonemapper
  TonemapParams0.x (1 = extended Reinhard, 2-5 others) if .y>=0.5; per-zone gamma; sRGB; saturate; UI over it; alpha 1.
- HysteriaVV fragment keeps the exposure path exactly and replaces grading + tonemap with Hysteria's `hysFinal`
  (Rec.2020 in). So VV color_grading JSON no longer reaches the screen; exposure settings still do.
- Build: SM6.0 + SM6.5 replaced, 0 kept vanilla. RP: `tools/HysteriaVV/build.py` (soft shadows, sun tilt 40,
  Hysteria noon/moon tints, moon:sun = 0.132:5.95). Atmospherics untouched.
- Startup gotcha: `command.txt = vibrant on` is only applied on a change after BF's first tick, so it can do nothing
  at launch. Instead set options.txt `graphics_mode:2` (CRLF file!) + modules.json `shaderpacks.settings.vibrant = true`.

**What Fernando checks:** settings say Vibrant Visuals; trees/players cast sun shadows that move with the time of day;
colours look like Hysteria (not plain VV); no crash when switching back (`vibrant off` + shaderpack Hysteria);
FPS with F3 vs Fancy Hysteria (~42-47 before / 15-18 in the last test).

## 3. GOAL 2 - motion blur + Dread Atmosphere debug counters
### 3a. Motion blur (code already reviewed in the cloud, 2026-10-27)
- RF math checked: `PostFx.cpp relRotation` + `postfx.hlsl PSComposite` are correct (the mirrored X axis cancels in
  prev^T*cur). Yaw/pitch come from BF `Modules.cpp` -> `postfx::hudFrame(rot.x, rot.z)` once per HUD frame.
- **Most likely reason it "wasn't seen": only CursedFog's postfx.json sets `motion_blur` (1.25).** Dread has none,
  Hysteria's is on the PC (check it). With motion_blur 0 the effect never runs.
Steps:
1. Read `shaderpacks\Hysteria\postfx.json` (and Insanity's). Note motion_blur values here.
2. Test A (no rebuild): F6 overlay -> Post effects -> "custom" + Motion blur slider 1.25, turn the camera fast.
   Test B: shaderpack CursedFog (has 1.25), restart, turn fast.
3. If no blur in either: check RF status line in F6 ("on: ... motion blur 1.25"). If it says idle -> BF not linked or
   `active` 0 (HUD not seen in last 250 ms). If "on" but no streaks -> yaw only updates per game tick; add a diag
   counter of yaw changes per second in BF (`diag::`) and compare to FPS. Fix: read the interpolated camera rotation
   (LevelRendererCamera) instead of `player->getRotation()`.
4. Known defect to fix in the same pass: post-FX run on the swapchain image, so the HUD/hotbar also smears while
   turning. Fix plan: copy the backbuffer at the first UI draw instead of at Present (same UI-PSO marker as
   Spatial GUI M0) - if that's too big for this goal, record it in NEXT_SESSION.md as open.
5. Add `"motion_blur"` to Hysteria's postfx.json only if Fernando wants it there.
**What Fernando checks:** turning left/right and up/down quickly gives streaks in the turn direction; standing still
or walking straight = sharp image; no streaks in menus; note if the hotbar smears.

**Status 2026-10-28:** motion blur is OFF on purpose - Fernando asked for 0 (felt laggy) in the "PC runbook section 0" chat;
every PC postfx.json has `"motion_blur": 0` (backups `postfx.json.pre-noblur`), repo CursedFog too. RF's F6 override
sliders are memory-only (ImGui ini off) and default to 0, so nothing can turn it back on. Don't re-enable unless he asks.
Still open if blur ever comes back: yaw source per tick vs per frame (step 3), HUD smear (step 4).

### 3b. Remove debug counters from Dread Atmosphere
- The ambient leaves + dust script with the counters exists ONLY on the PC (the repo builder
  `tools/DreadAtmos/build.py` still has the old fog-only BP_SCRIPT). So:
1. Find the live BP: search the instance's world + development packs for `bf:amb` (the `/scriptevent bf:amb x`
   handler).
2. Stage that `scripts\main.js` to the cloud. Remove: the counter variables/increments, the `bf:amb` scriptevent
   debug print, any periodic log/actionbar output. Keep spawning logic. Tidy dust density (open TODO) - make it one
   constant at the top.
3. Put the cleaned script into `tools/DreadAtmos/build.py` as BP_SCRIPT (it's original MIT code -> OK to commit) and
   bump the pack version so the game reloads it. Commit + push.
4. Copy the cleaned main.js over the live one (rename the old to `main.js.debug-bak` first).
**Done 2026-10-28:** live BP = `Users\Shared\games\com.mojang\development_behavior_packs\DreadAtmosphereBP` (old script kept as
`main.js.debug-bak`). Counters + `bf:amb` handler removed, tuning constants at the top (DUST_PER_RUN = density). Builder now
writes the same script + the particles/textures (byte-identical to the live pack). Versions stay 1.0.0 on purpose: three
worlds pin [1,0,0] and development packs are re-read every world load. Note: the BP is in My World `AWJgm+UJIEM=`,
From The Fog Test and "test" - NOT in the My World played on 10-28 (`+A1sY8g-qtk=`).
**What Fernando checks:** leaves + dust still fall; `/scriptevent bf:amb x` no longer prints; ContentLog 0 errors.

## 4. Sky bug from Fernando's 2026-10-27 test (top priority per his note: "perfect before any other mod")
Symptom: in a flat world, flying up: upper half of the sky = shader sky but **stationary**, lower half = **vanilla**.
Both shader skies. Cloud findings:
- Packs replace only the `Sky` material (+Clouds for Dread). Sun, moon, stars and the other sky/horizon materials are
  never staged, so they stay vanilla (CursedFog TODO already says "SunMoon/Stars materials (stage them)").
- `CursedFog/Sky/Fragment.hlsl` blends on `dir = normalize(texcoord3)`; `upDot` is clamped at 0, and there's no sun
  direction / day time uniform - so it can only follow vanilla SkyColor/FogColor, never the sun.
Steps:
1. On the PC list `<vanilla materials>` names containing Sky / Sun / Moon / Star / Cloud / EndSky / Cubemap; stage
   them + note which are used on Fancy in the overworld (BF's shaderpacks.txt "material opens" shows what loads).
2. Dump `Sky.material.bin` vertex shader: confirm what `texcoord3` really is in 1.26 (world dir or something else).
   If it isn't a direction, the dome's lower half computes garbage -> rebuild dir from position.
3. Add per-pack folders for the missing materials (ShaderForge picks up any `<Material>/Fragment.hlsl`).
4. Give the sky a real sun direction: `// @uniform` for the sun vector if the material has one; otherwise a BF
   uniform override (setUniform hook, RENDERING_PLAN R4) fed from `Dimension::getTimeOfDay`.
5. Test per pack: flat world, fly to y 200, look at the horizon, straight up, straight down; noon, sunset, night.
### 2026-10-27 findings + what was installed (sky fix v1)
- Vanilla 1.26 Sky.Vertex decoded: `worldPos = u_model*pos`, `clip = u_viewProj*worldPos`, `colour = lerp(SkyColor, FogColor, COLOR.x)`,
  TEXCOORD3 = worldPos. Vanilla Sky.Fragment = colour only. Sky uniforms: u_viewProj, u_model, FogColor, SkyColor - nothing else.
  So `normalize(texcoord3)` IS a usable direction (step 2 answered) - not the bug.
- LOWER HALF: the sky mesh is a plane ABOVE the camera (COLOR.x 0 zenith .. 1 rim), so pack sky shaders only ever drew the upper
  half; below the horizon is the vanilla clear colour. FIX: `shaders/src/_shared/SkyFull.vertex.hlsli` (exact vanilla rebuild +
  fold: keep each vertex's distance, elevation goes +90 deg -> -60 deg (`SKY_FOLD_BELOW`) with COLOR.x; planes with y<=0 untouched;
  TEXCOORD3 = rotation/scale of u_model applied, translation dropped). Every pack has `Sky/Vertex.hlsl` including it.
- "STATIONARY": Hysteria's sky clouds animate from ViewPositionAndTime.w, which is 0 on sky draws (E-025/E-026), so they freeze.
  LightWorldSpaceDirection is 0 on Fancy. No time/sun uniform exists for Sky -> steps 4 needs RENDERING_PLAN R4 (native
  setUniform hook fed from Dimension::getTimeOfDay). NOT DONE (needs C++ + CI build + push).
- SunMoon (texture*SunMoonColor) and Stars (COLOR.rgb*COLOR.a*StarsColor) decoded; SunMoon TEXCOORD3 = the sun/moon quad's world
  position (= sun direction, but only inside that draw). Every pack now has SunMoon/Stars fragments = vanilla math + the pack's
  colour grade (Anime saturate, Dread desat, CursedFog desat+red, Hysteria/Insanity tonemap+grade). Instancing=On kept vanilla.
- Built with ShaderForge on the device shell (only Sky/SunMoon/Stars - other materials untouched), all variants replaced
  (no "kept vanilla" except the skipped instanced ones). Installed into all 5 BF packs; old Sky kept as
  `renderer\materials\Sky.material.bin.pre-skyfix` (SunMoon/Stars had no pack version = vanilla). Report: `<Pack>\BUILD_REPORT.skyfix.txt`.
- Rollback per pack: copy `Sky.material.bin.pre-skyfix` over `Sky.material.bin` and move SunMoon/Stars .bin into `_to_delete\`.
- RISKS to check in game: if terrain below the horizon disappears/gets covered (sky writes depth at the folded distance) or the
  lower sky has visible seams (coarse sky mesh), lower SKY_FOLD_BELOW (e.g. 20) or make the fold depth-safe.

**What Fernando checks:** no vanilla half; sky colour changes with time; sun/moon/stars match the pack.
