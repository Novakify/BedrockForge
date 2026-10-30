# LyricsFlow kickoff on the PC - exact steps (written 2026-10-30, prep session)

> **Update 2026-10-30 ~19:00 local: foundation is now LyricsFlow 2.1** (Fernando got it from 374m, MIT, authorised
> for Novak; jar not committed). Priority (Fernando): lyrics + UI first, then glass hotbar + FPS profiles, sky last.
> - **World panel** = port of 2.1 `app.cosmetic.WorldPanel` (`src/lyrics/core/Panel.*`): **L** opens/closes it
>   (Java K is BF's), it stands 3.2 blocks in front of you, aim with the **crosshair**, left click. Clicks on it are
>   swallowed (no punching / placing). Tabs Sky / Lyrics / Look / **FPS** / More (2.1's Cosmetics slot = FPS page:
>   Chill/PvP profile, Performance/Balanced/Pretty, Automatic FPS = 2.1 AutoPerf, target, FPS boost).
> - RF branch to install: **`lyricsflow/rf-on-cinematic-v3`** (= cinematic 20529b2 + lyrics). CI publishes both.
> - **Glass hotbar** = resource pack `packs/LyricsFlowGlassHotbar` (in `lyricsflow-builds/packs/`): copy into the
>   instance's resource_packs + add to global_resource_packs.json (top). Textures only, vanilla layout.
> - **FPS boost (game settings)**: `tools/lyricsflow/fps_boost.py apply|restore|show <minecraftpe/options.txt>`,
>   GAME CLOSED (Bedrock rewrites options.txt on exit). Check `show` first: key names unverified on 1.26.51.
> - Sky system: not ported yet (skyPreset / strength / speed / pattern are stored, nothing draws a sky).

> **Update 2026-10-30 ~11:30 local (cloud session while Fernando was out, no PC access):**
> - RF branch is now **`lyricsflow/rf-on-cinematic-v2`** = `cinematic` 22bee91 + lyrics pass (the force-push of
>   the old branch was blocked; the workflow publishes `lyricsflow-rf-builds` from both names).
> - Install is two scripts: cloud `tools/lyricsflow/prepare_incoming.sh <repo> /mnt/user-data/outputs/lf <stamp>`,
>   `device_commit_files` the 4 files to `mods/_lyricsflow_incoming/<stamp>/`, then in the device shell
>   `tools/lyricsflow/kickoff_install.sh install <mods> <stamp> <installed RF cinematic sha>` (backs up by copy,
>   checks SHA256, never deletes; `rollback` restores). Dry-run tested in the cloud on a fake mods folder.
> - Presets tuned against the 4 videos in the new offline preview (`tools/LyricsPreview`), + new **Wall Coral**.
> - Pre-kickoff review fixes: RF camera hook idle unless lyrics draw / no game calls under its lock; stale frames
>   dropped after 30 Presents (no frozen lyrics on menus); atlas index guard + BF loads max 8 fonts (config fonts
>   first); S2 resize leak; O / [ / ] do nothing while the module is off; fetch thread can't std::terminate.
> - **Source now defaults to ANY** (Fernando uses **Apple Music**, Windows app, which publishes SMTC). New
>   `APPLE_MUSIC` source + clean-up of Apple Music's SMTC fields (artist can be empty -> album artist; or
>   "Artist — Album" in one field -> split), and no YouTube-style title split for it. F6 status / top bar show the
>   source app. Unverified on the real app: check what title/artist the top bar shows on the first song.
> - **Host access needed:** this session's folder-access requests were auto-blocked; Fernando adds
>   `C:\BedrockModding` + the `1.26.51.01` instance folder in the desktop app (+ -> Add folder).

Goes with `docs/LYRICSFLOW_SPEC.md` §7. Do NOT start while the Cinematic chat still drives the game (check its last
commit on `cinematic` / its NEXT_SESSION block; if it moved in the last ~20 min, wait).

## 0. Builds (all from CI; artifact downloads are blocked, git works)
| What | Branch with the build | Source branch |
|---|---|---|
| BF DLL + `resources/lyricsflow` (fonts, presets, licences) | `lyricsflow-builds` | `lyricsflow/scaffold` (off `main`) |
| RF DLL = Cinematic + lyrics pass | `lyricsflow-rf-builds` | `lyricsflow/rf-on-cinematic` (off `cinematic`) |

```
git fetch origin +lyricsflow-builds:refs/remotes/origin/lyricsflow-builds +lyricsflow-rf-builds:refs/remotes/origin/lyricsflow-rf-builds
git show origin/lyricsflow-builds:BedrockForgeCore/BedrockForgeCore.dll > BF-lyrics.dll
git show origin/lyricsflow-rf-builds:RendererFoundation/RendererFoundation.dll > RF-lyrics.dll
git archive origin/lyricsflow-builds BedrockForgeCore/resources | tar -x -C staging/
```
If Cinematic moved on since `e076e8f`: rebase `lyricsflow/rf-on-cinematic` onto the new `cinematic` first (only
`Overlay.cpp`, `GUI.cpp`, `Entry.cpp`, `UiCapture.cpp` are touched, + new `Lyrics*` files, `glyph.hlsl`,
`gen_glyph.py`, `BfLyrics.h`, the workflow) and push, wait for CI.

## 1. Install (mods folder = `C:\Users\Fazzman175\AppData\Roaming\levilauncher.exe\versions\1.26.51.01\mods`)
1. Game closed (Save & Quit with mouse down/up, window X).
2. Back up: `BedrockForgeCore\BedrockForgeCore.dll` -> `BedrockForgeCore.pre-lyrics.dll`,
   `RendererFoundation\RendererFoundation.dll` -> `RendererFoundation.pre-lyrics-<cine sha>.dll`.
   (Deleting in the mods folder needs permission - rename/move instead.)
3. Stage with NEW file names (re-using a staged name wrote 0 bytes before), `device_commit_files`, then copy into place.
4. Copy `resources/lyricsflow/{fonts,presets,licenses}` to `mods\BedrockForgeCore\resources\lyricsflow\`.
5. Launch the instance (`Minecraft.windows` only when it is closed), Play, My World.

## 2. Turn it on
- ClickGUI (Right Shift) -> Visual -> **LyricsFlow** on. Config: `mods\BedrockForgeCore\config\lyricsflow.json`
  (written on first start; presets = copy a preset's `settings` keys into it, or edit by hand; reload = restart).
- F6 -> **LyricsFlow (preview)**: status line (glyph count), camera source, atlases decoded, hook counters.
- Keys: **O** on/off, **[** / **]** song offset -100 / +100 ms. (K/J are BF's impact/HUD keys.)
- Start Spotify playing something popular (LRCLIB/Musixmatch coverage).

## 3. Test order (stop at the first thing that breaks, fix, rebuild)
1. **SMTC**: F6 status leaves "idle"; top karaoke bar shows title · artist, then the lyric line with the pill moving.
   Seek / pause in Spotify: bar shows "Paused", the pill follows the seek.
2. **S5** SCREEN style (`"lyricsStyle": "SCREEN"`): words fade in on the screen. No camera involved.
3. **S1** FLOATING: walk/strafe/jump/F5. Text must stay put in the world. Slides or mirrored -> F6 "Flip forward
   axis"; compare "game hook" vs "BF estimate".
4. **S2** only with `"uiCaptureSpike": true` (already on for Cinematic) + F6 "Under the HUD": hotbar/crosshair above
   the lyrics, chat/inventory/Cinematic scenes still fine. Anything odd -> turn it off (default draws over the HUD).
5. WALL near a wall, SKY at night (Aurora Cursive), Neon Arena, **Ground Drop** (letters land on grass/steps; slabs
   count as full blocks for now).
6. Perf with 3 lines + falling letters, F6 glyph count.

## 4. Rollback
Rename the `pre-lyrics` DLLs back to `BedrockForgeCore.dll` / `RendererFoundation.dll` (game closed). The lyrics
module is off by default, so even the new BF DLL is inert until switched on.
