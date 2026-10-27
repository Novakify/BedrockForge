# PC runbook - work that needs Fernando's PC (written 2026-10-27, cloud-only session)

Follow this top to bottom. Nothing here is guessed from memory: each step says what to check first.
Fernando checks the game himself; no screenshots unless he asks.

## 0. PC file map (VERIFY FIRST - these come from older handoffs, never confirmed live)
Run once at the start of every PC session (device shell), and fix this table if anything moved:
```
dir "C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01\mods"
dir "C:\BedrockModding" /s /b /ad | findstr /i "BedrockForge Renderer ShaderForge shaderpacks Hysteria Insanity Dread"
```
| What | Expected location | In GitHub? |
|---|---|---|
| Game instance (1.26.51.01) | `C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01` | no |
| BF client DLL | `<instance>\mods\BedrockForgeCore\BedrockForgeCore.dll` (+ old `.xxxxxxx.dll` backups) | source: `main`, DLL: `builds` |
| BF config | `<instance>\mods\BedrockForgeCore\config\` shaderpack.txt, command.txt, shaderpack.running/.crashed/.fallback | no |
| BF logs | `<instance>\mods\BedrockForgeCore\logs\` diag.txt, shaderpacks.txt (rewritten ~10 s) + `<instance>\logs\` latest.log, crash\ | no |
| Shaderpacks served by BF | `<instance>\mods\BedrockForgeCore\shaderpacks\<Pack>\renderer\materials\*.material.bin` + postfx.json | Dread/Anime/CursedFog yes |
| Private shader sources | `...\shaderpacks\Hysteria\src`, `...\shaderpacks\Insanity\src` (AGNYA licence: NEVER commit) | **no** |
| Renderer Foundation | `<instance>\mods\RendererFoundation\` (RendererFoundation.dll, manifest.json, config, shaderpacks\) | source: `renderer-foundation`, DLL: `rf-builds` |
| Vanilla materials (ShaderForge input) | `<instance>\data\renderer\materials\` (VERIFY - may be under the game package folder instead) | no (Mojang files) |
| Vanilla VV settings (brarchives) | game `data\resource_packs\vanilla\...` lighting/atmospherics/color_grading (+ shadows if present) (VERIFY) | no |
| Dread Atmosphere BP/RP (live, edited) | world behavior/resource packs of "My World" + `development_*_packs` (VERIFY); repo builder: `tools/DreadAtmos/build.py` | builder only |
| Repo checkout on PC | `C:\BedrockModding\Source\BedrockForge` (VERIFY) | = GitHub |
| GeckoBridge research | branch `geckobridge/compat-audit-20261025` (deferred, do not merge; test JARs stay off GitHub) | yes |

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
**What Fernando checks:** no vanilla half; sky colour changes with time; sun/moon/stars match the pack.
