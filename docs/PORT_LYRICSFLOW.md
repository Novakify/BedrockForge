# LyricsFlow -> Bedrock Forge: feasibility research (2026-10-29)

Research only. Nothing built or installed. This is the starting point for the kickoff prompt.

## Sources examined
- `LyricsFlow.jar`, v1.19, Fabric, MC Java 1.21.11, author 374m, metadata says MIT. 103 classes, 22 TTF fonts (all OFL,
  plus Yellowtail and DejaVu-Arabic with their own licence files), 63 GUI textures, 14 UI sounds.
  The jar contains `NOVAK_PERMISSION.txt`: the *supplier* says 374m authorized Novak to port it and that the earlier ARR
  metadata was applied without the creator's consent. The file says itself that this is the supplier's word, not
  verified. **Before any public release, get a written OK from 374m (Discord: zulemayeniden, discord.gg/LyricsFlow), the
  same way we did with tapeQ for Shine.** Keep the MIT notice and the font licence files, and credit 374m.
- `lyricsplus-3.0.0-1.20.1.jar`, v3.0.0, author FRK, MIT, source on github.com/lyricsplus. 24 classes.
- BedrockForge repo `main` @ 8572f25 (docs/NEXT_SESSION, PROJECT_STATUS, PORT_SPATIALGUI, src/).
- Analysis: javap bytecode dumps (the CFR decompiler download was blocked in the cloud session). Enough to see the whole design.

## How LyricsFlow actually works (from the bytecode)
| Area | Java implementation | Port difficulty |
|---|---|---|
| Media detection | `SmtcBackend`: Windows Global System Media Transport Controls (WinRT) called raw through JNA/COM. Reads title, artist, album, duration, position, sample time, playing, source app. Play/pause/next/prev/seek. Source filter SPOTIFY / YOUTUBE (browsers: chrome, edge, brave, arc, opera(gx), firefox, vivaldi, zen, librewolf, waterfox) / ANY | **Easy.** C++/WinRT does this natively in BF, with less code than the JNA version |
| Clock sync | `Track.positionNow()` extrapolates from the sample time. `MediaManager` re-syncs when Windows' position drifts (seek detection, "stable" heuristics). `syncOffsetMs` lets you adjust timing by hand | Easy (pure logic) |
| Lyrics | `LyricsFetcher`: LRCLIB `/api/get` (exact match by duration) + `/api/search`, NetEase `music.163.com/api/search/get` + `/api/song/lyric`. Parallel candidates, first synced result wins, duration scaling for sped-up/slowed versions. `TitleCleaner` strips "Official Video", feat., remix/nightcore and so on | Easy (WinHTTP + JSON) |
| Word timing | `Lyrics$Line.splitWords` **spreads a line's time evenly across its words**. Enhanced LRC `<mm:ss.xx>` word tags are parsed when present. So "word-by-word" is mostly interpolated | Easy. True word sync is a data problem (see below) |
| Placement modes | `LyricsStyle` = SKY, FLOATING, SCREEN, WALL, plus `followView`, `viewLock`, `randomPosition`, `arcLayout`, `varySizes`, `maxLines`, `sentenceMode`. `WorldLyrics` raycasts the world (ClipContext) to: fit text onto wall surfaces (`wallPlace`, `fitToSurface`, surface normal quaternion); keep clearance from terrain and clouds; avoid HUD areas (`avoidBoxes`); avoid crowded spots (`crowd`); fade when blocked (`occl`, every few ticks) or when you look away | **Medium.** Needs client-side block queries (LeviLamina) and a port of the placement maths |
| Text drawing | Vanilla MC font renderer with TTF providers loaded from a runtime resource pack (`CustomFonts`, which also imports user TTFs from a folder). `FontTextureMixin` switches glyph textures to linear filtering. "NEON" style = layered glow passes (`neon3d`) | Hard to copy 1:1, but there's a better route (MSDF, below) |
| Shader fallback | `lateForShaders()`: when Iris shaders are on, every style except WALL is drawn in **2D at the screen-projected world position** (`Proj.project`, `drawScreen`) instead of as real 3D geometry. **The Java mod already uses the same approach we'd use on Bedrock** | Validates the plan |
| Animations | Enter: WORD / FADE / RISE. Exit: FLOAT_UP / DRIFT / FADE. Each word has its own offset, size and rotation (`wx0/wy0/wsize/wrot`), `animSpeed`, `beatGlow` | Easy (pure maths) |
| Looks | 23 themes (aurora, black_white, blood_moon, candy, enchant, fire, galaxy, gold, ice_blue, lavender, matrix, midnight, minecraft(_gold), neon_pink, nether, ocean, sakura, sunset, toxic, vaporwave, custom). Single colour, two-colour, glow, opacity, shadow | Easy on the GPU text shader |
| Extras | Sky FX (stars, motes, streaks) + 24 sky presets + `SkyTint` (tints sky/fog/clouds/stars), `Chroma` (screen tint), `TopBar` karaoke/now-playing bar, `HotbarStyle`, `PlayerScreen` music player, cover art, per-app volume (`AppVolume`, Windows audio sessions), media keys, HUD editor, config profiles, hide in screenshots, music notes for instrumental parts | Mixed. Sky tint fits our ShaderForge `@uniform` / VV colour grading path. The rest is ImGui/2D |
| Not in LyricsFlow | **Falling-letter physics.** Nothing like it in either mod. New work | Medium (see below) |

### What LyricsPlus adds (secondary reference)
- GSMTC through a PowerShell child process (slower, so LyricsFlow's native route is better).
- A **localhost HTTP bridge** (`127.0.0.1`, `/update`, `/status`, CORS) so a browser extension can push track info. Good fallback for web players that don't report to Windows media controls.
- Explicit `applemusic.exe` recognition, and cover art lookup through iTunes Search + Deezer APIs.
- LRCLIB only (no NetEase). HUD now-playing widget and toast.
- Takeaway: borrow the bridge idea and the Apple Music handling. Everything else, LyricsFlow does better.

## What Bedrock Forge already has that this needs
- **RF draws its own geometry at Present on D3D12**: ImGui overlay (F6), post-FX (bloom, CA, CAS), and since Spatial GUI M1 a **perspective textured quad drawn with our own HLSL** (`SpatialPanel`, `panel.hlsl`, compiled with dxc). That's the core of world-space text.
- **Spatial GUI M0.5 found the first UI draw on the back buffer.** That's the exact spot to insert the lyrics pass: after the world, before the HUD. The HUD and hotbar stay on top and the lyrics sit "in" the world.
- BF exports camera yaw/pitch/FOV (`BF_GetPostFx`, FOV hook on `LevelRendererPlayer::getFov`), and LeviLamina gives the player position and client block access.
- Config pattern (BF JSON + RF ImGui) and keybinds (`bf/Input.h`).
- Weakness: BF's own `Hud.h` text is 2D UI with the "default" Bedrock font only. Fine for debug, not for the look.

## Recommended architecture
```
BF (LeviLamina C++)                          RF (D3D12 @ Present)
 media/   SMTC (C++/WinRT) + localhost bridge   text/  MSDF atlas per font (22 OFL TTFs, prebuilt)
 lyrics/  LRCLIB + NetEase + LRC parser + cache        + runtime import of user TTFs (msdfgen, MIT)
 scene/   placement (sky/float/wall/follow/screen)     glyph-quad shader: fill, gradient, per-letter colour,
          occlusion raycasts, letter physics sim        outline, glow, opacity, animated colour
          -> per-frame glyph instance list ------->    one instanced draw, inserted before the first UI draw
 config/  JSON + presets                        ui/    ImGui settings pages + presets (F6)
```
- **MSDF text** (multi-channel signed distance field) is better than the Java mod's bitmap TTF. It stays sharp at any size or distance, and the shader gets glow, outline and gradients almost for free.
- Shared data contract BF -> RF: an exported C function returning a frame's glyph instances (pos, right/up axes, uv rect, colours, alpha, flags). It's the same pattern as `BF_GetPostFx`.
- Draws after tonemapping, so it **looks the same on Fancy and Vibrant Visuals**, with all our shaderpacks. RF bloom can add extra glow.

## Critical unknowns: spikes to run first
| # | Question | Why it matters | Options |
|---|---|---|---|
| S1 | Exact camera view+projection (eye position, view bobbing, 3rd person, FOV effects) | Without it, world text "swims" against the terrain, which is very visible for falling letters | (a) read the view/proj from RenderDragon/bgfx constant buffers in RF's D3D12 hooks (`ViewPositionAndTime` is already reachable by ShaderForge); (b) hook the camera in BF (Lamium camera code is the reference); (c) MVP: rebuild it from pos+yaw/pitch/FOV and turn bobbing off |
| S2 | Lyrics pass under the HUD | Text over the hotbar/crosshair looks wrong and hurts PvP | Reuse the M0.5 "first UI PSO draw on back-buffer RTV" detection and draw just before it |
| S3 | Occlusion by terrain | Without depth, text shows through hills | (a) CPU raycast fade, the same as LyricsFlow (`occl`); (b) bind the scene depth buffer (find the DSV the way M0.5 found RTVs) for per-pixel occlusion. Start with (a) |
| S4 | Client block queries on servers | Wall placement + physics need terrain | LeviLamina client `BlockSource` / chunk access. Chunks are on the client on any server, so this should work, but verify on a real server |
| S5 | MSDF hello-world | Proves the text pipeline | One word at a fixed world position, one font, glow on |

## Feasibility by feature
| Feature | Verdict |
|---|---|
| Spotify / YouTube-in-browser detection, pause/resume/skip/seek follow | **Likely to work.** Same Windows API, proven by both mods |
| Apple Music | **Likely, needs testing.** The Microsoft Store Apple Music app reports to Windows media controls; iTunes is unreliable |
| Downloaded music | **Works for any player that reports to Windows media controls** (Windows Media Player, MusicBee, foobar2000 with a component, AIMP). Lyrics from LRCLIB, or a user .lrc folder matched by artist/title (Windows doesn't give the file path) |
| Line-synced lyrics | **Likely.** LRCLIB has good coverage of popular songs |
| True word-by-word sync | **Data-limited.** Most LRCLIB entries are line-level, so word timing gets interpolated like LyricsFlow does. Real word timing exists in NetEase "yrc", Apple TTML and Musixmatch/Spotify "richsync", but those are unofficial or account-locked APIs. Phase 4 option, licence/ToS caution |
| World-space 3D text (sky, floating, wall), perspective, depth feel | **Likely, needs a lot of testing.** It depends on S1-S3. RF already draws a perspective quad |
| Player-following mode | **Likely.** It's simpler than world mode |
| All 22 fonts + custom font import | **Likely.** MSDF atlases, prebuilt offline. Runtime import through msdfgen |
| Colours, gradients, per-word/per-letter colour, glow, outline, opacity, animated colour | **Likely.** Shader work. Should look better than the Java mod |
| Album-art palette | **Likely.** Windows gives the thumbnail. Extract the dominant colours |
| Enter/exit/word/letter animations, randomised styles | **Likely.** Pure maths |
| Falling letters (gravity, tumble, bounce, slide, friction, settle, fade, uneven terrain) | **Likely, needs tuning.** Each letter is a small rigid body (pos, vel, quat, angVel) colliding against block boxes from S4. A few hundred letters is trivial on the CPU. Phase 1 = full blocks; later slabs/stairs/snow from block shapes |
| Sky tint / sky FX presets | **Possible, separate track.** Shader uniform route (ShaderForge) on Fancy, colour grading on VV |
| Settings UI + presets | **Likely.** RF ImGui, the same as the existing F6 overlay |
| Performance on RTX 4060 | **Not a concern for the text itself**: one instanced draw. Watch the CPU raycasts (cap per tick, as LyricsFlow does) |

## Servers (The Hive, CubeCraft)
- **Technically: yes, and it's the most server-friendly mod in the project.** Everything is local: Windows media + web lyrics + local drawing. It sends no packets, needs nothing from the server, and touches no gameplay or entity data. Only you see it, which is what you want.
- **Blocker 1, rules.** The Hive's allowed-mods page lists what's allowed (cosmetics, FOV, fullbright, armor HUD, CPS...) and says *"If a modification is not listed here, you should assume it isn't allowed."* A lyrics overlay isn't listed, and the loader is an injected client. So: **technically harmless, formally unapproved.** You can ask Hive support to approve it. CubeCraft's current rules page wasn't found; check it before use.
- **Blocker 2, versions.** Big servers only accept current Bedrock versions. BF is pinned to 1.26.51.01 / LeviLamina 26.51.x. After every Bedrock update the mod won't work on servers until LeviLamina and our hooks are updated. Keep our engine hooks small (camera, block query, draw pass) so updates stay cheap.
- **Matches project rules:** no anti-cheat bypass is needed or ever done. Single-player first, then servers.
- **Combat safety:** keep LyricsFlow's `avoidBoxes`/`crowd` logic, plus a centre-screen keep-out zone and optional fading during combat.

## Is it worth it?
Yes. It's high-impact, mostly self-contained, and relies on only three fragile engine touch points (camera, blocks, draw hook). The rest is plain C++/HLSL that won't break on game updates. It also reuses the Spatial GUI M1 pieces (3D quad + UI-pass detection). Overall estimate: about 75% of the vision is likely to work well. The remaining 25% (camera-accurate world placement, occlusion, real word sync) needs testing or better data.

## Suggested phases
0. **Spikes S1, S2, S5 + SMTC log** (1-2 sessions, PC needed). Exit: one MSDF word fixed in the world, stable while you walk, under the HUD; Spotify track and position in BF logs.
1. **MVP:** Spotify/YouTube -> LRCLIB -> screen + floating modes, 4 fonts, fade/rise/word animations, sync offset.
2. **World mode:** sky / wall / floating placement port, CPU occlusion, follow-player mode, all fonts, themes, glow/outline/gradient.
3. **Falling letters physics** + presets inspired by the reference videos.
4. **ImGui settings**, custom font import, Apple Music + local .lrc, album palette, localhost bridge, optional sky tint/FX, word-sync data sources.
5. **Server hardening:** combat-safe defaults, performance caps, a version-update checklist, and a test on Hive/CubeCraft (after checking their rules).

## Queue placement (decision for Fernando)
Current order: Spatial GUI M1 (input) -> Cinematic Interactions -> GeckoBridge, under the rule "finish current work first".
LyricsFlow shares RF pieces with Spatial GUI, so the natural slot is right after Spatial GUI M1. Spike S5 (text) could also run earlier, since it doesn't touch existing features.

## Inputs still wanted for kickoff
- The 4 reference videos (not attached to the research session) to set presets and judge the falling-letter look.
- 374m's written permission, at least for a private build.
- For phase 5: Hive/CubeCraft rule check + a test session on a real server.
