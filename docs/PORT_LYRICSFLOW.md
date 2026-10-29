# LyricsFlow -> Bedrock Forge: feasibility research (2026-10-29)

Research only. Nothing built or installed. This is the starting point for the kickoff prompt.

> **Update 2026-10-29 (prep phase):** the groundwork is built on branches `lyricsflow/scaffold` (BF) and
> `lyricsflow/rf-scaffold` (RF). Exact v2.0 behaviour map, what's done, and the kickoff-day runbook:
> **`docs/LYRICSFLOW_SPEC.md`** (+ `docs/lyricsflow/SPEC_SECONDARY.md`). Still nothing tested in game.

## Sources examined
- `LyricsFlow.jar`, v1.19, Fabric, MC Java 1.21.11, author 374m, metadata says MIT. 103 classes, 22 TTF fonts (all OFL,
  plus Yellowtail and DejaVu-Arabic with their own licence files), 63 GUI textures, 14 UI sounds.
  The jar contains `NOVAK_PERMISSION.txt`: the *supplier* says 374m authorized Novak to port it and that the earlier ARR
  metadata was applied without the creator's consent.
  **FOUNDATION UPDATED 2026-10-29: use `LyricsFlow_Update.jar` v2.0 (374m, MIT, still MC 1.21.11 / Fabric) as the base,
  not the v1.19 jar. Fernando sent a couple of interim update jars the same day — ignore those, v2.0 is the one.** The
  v1.19 analysis below still holds for the core; v2.0 adds a lot on top (see "What's new in v2.0").
  **PERMISSION (Fernando, 2026-10-29): 374m gave the OK to modify LyricsFlow for PERSONAL USE ONLY. This build is for
  Fernando alone and will NOT be distributed.** LyricsFlow is the foundation; we build our own Bedrock version on top of
  it. Scope rule for the whole project: private/personal, no public release of this port. Keep the MIT notice and the
  font licence files in our copy, and credit 374m. If this ever changes to a public release, get that permission in
  writing first (Discord: zulemayeniden, discord.gg/LyricsFlow), the same way we did with tapeQ for Shine.
- `lyricsplus-3.0.0-1.20.1.jar`, v3.0.0, author FRK, MIT, source on github.com/lyricsplus. 24 classes.
- BedrockForge repo `main` @ 8572f25 (docs/NEXT_SESSION, PROJECT_STATUS, PORT_SPATIALGUI, src/).
- Analysis: javap bytecode dumps (the CFR decompiler download was blocked in the cloud session). Enough to see the whole design.

## How LyricsFlow actually works (from the bytecode)
| Area | Java implementation | Port difficulty |
|---|---|---|
| Media detection | `SmtcBackend`: Windows Global System Media Transport Controls (WinRT) called raw through JNA/COM. Reads title, artist, album, duration, position, sample time, playing, source app. Play/pause/next/prev/seek. Source filter SPOTIFY / YOUTUBE (browsers: chrome, edge, brave, arc, opera(gx), firefox, vivaldi, zen, librewolf, waterfox) / ANY | **Easy.** C++/WinRT does this natively in BF, with less code than the JNA version |
| Clock sync | `Track.positionNow()` extrapolates from the sample time. `MediaManager` re-syncs when Windows' position drifts (seek detection, "stable" heuristics). `syncOffsetMs` lets you adjust timing by hand | Easy (pure logic) |
| Lyrics | `LyricsFetcher`: LRCLIB `/api/get` (exact match by duration) + `/api/search`, NetEase `music.163.com/api/search/get` + `/api/song/lyric`. Parallel candidates, first synced result wins, duration scaling for sped-up/slowed versions. `TitleCleaner` strips "Official Video", feat., remix/nightcore and so on | Easy (WinHTTP + JSON) |
| Word timing | v1.19: `splitWords` spreads a line's time evenly across its words (interpolated). **v2.0 adds real word timing** (see below) | Easy to port the logic |
| Placement modes | `LyricsStyle` = SKY, FLOATING, SCREEN, WALL, plus `followView`, `viewLock`, `randomPosition`, `arcLayout`, `varySizes`, `maxLines`, `sentenceMode`. `WorldLyrics` raycasts the world (ClipContext) to: fit text onto wall surfaces (`wallPlace`, `fitToSurface`, surface normal quaternion); keep clearance from terrain and clouds; avoid HUD areas (`avoidBoxes`); avoid crowded spots (`crowd`); fade when blocked (`occl`, every few ticks) or when you look away | **Medium.** Needs client-side block queries (LeviLamina) and a port of the placement maths |
| Text drawing | Vanilla MC font renderer with TTF providers loaded from a runtime resource pack (`CustomFonts`, which also imports user TTFs from a folder). `FontTextureMixin` switches glyph textures to linear filtering. "NEON" style = layered glow passes (`neon3d`) | Hard to copy 1:1, but there's a better route (MSDF, below) |
| Shader fallback | `lateForShaders()`: when Iris shaders are on, every style except WALL is drawn in **2D at the screen-projected world position** (`Proj.project`, `drawScreen`) instead of as real 3D geometry. **The Java mod already uses the same approach we'd use on Bedrock** | Validates the plan |
| Animations | Enter: WORD / FADE / RISE. Exit: FLOAT_UP / DRIFT / FADE. Each word has its own offset, size and rotation (`wx0/wy0/wsize/wrot`), `animSpeed`, `beatGlow` | Easy (pure maths) |
| Looks | 23 themes (aurora, black_white, blood_moon, candy, enchant, fire, galaxy, gold, ice_blue, lavender, matrix, midnight, minecraft(_gold), neon_pink, nether, ocean, sakura, sunset, toxic, vaporwave, custom). Single colour, two-colour, glow, opacity, shadow | Easy on the GPU text shader |
| Extras | Sky FX (stars, motes, streaks) + 24 sky presets + `SkyTint` (tints sky/fog/clouds/stars), `Chroma` (screen tint), `TopBar` karaoke/now-playing bar, `HotbarStyle`, `PlayerScreen` music player, cover art, per-app volume (`AppVolume`, Windows audio sessions), media keys, HUD editor, config profiles, hide in screenshots, music notes for instrumental parts | Mixed. Sky tint fits our ShaderForge `@uniform` / VV colour grading path. The rest is ImGui/2D |
| Not in LyricsFlow | **Falling-letter physics.** Nothing like it in either mod. New work | Medium (see below) |

## What's new in v2.0 (the new foundation)
Version jumped 1.19 -> 2.0, +18 classes, jar 4.5MB -> 9.3MB. Removed: `Presence` (Discord rich presence), `PlayerTabOverlayMixin`. Added:

| New in v2.0 | What it does | Port note / priority |
|---|---|---|
| **Real word-by-word sync** | `LyricsFetcher` now pulls **Musixmatch richsync** (`apic-desktop.musixmatch.com` token + `track.richsync.get`, `parseRichsync`) and **NetEase YRC** (`/api/song/lyric/v1`, `parseYrc`, `YRC_WORD`/`YRC_LINE`). These carry per-word timestamps | **This fixes the biggest limitation in the v1.19 analysis.** Word sync is no longer just interpolation. Port note: Musixmatch's desktop API is unofficial/token-gated — keep it as one source with LRCLIB/NetEase fallback, and treat its ToS with caution (fine for a personal build) |
| **AudioSync (WASAPI loopback)** | `AudioSync` captures the system audio output (COM: AudioClient + CaptureClient, loopback, "LyricsFlow voice") and measures it to refine the sync offset per song; stores a `k` scale + per-song offsets (`songOffsets` map) | Sync-accuracy polish. C++/WASAPI does this natively. **Phase 4 option**, not core. Pairs with the `syncOffsetMs` + per-song-offset config |
| **Big sky system** | New sky textures: auroras (front/back + custom), milkyway, blackhole, synth sun/grad, caustics, layered clouds/wisps, and a `ring_*` texture per sky preset (aurora, blood_moon, cherry_blossom, galaxy, golden_hour, neon_city, northern_lights, thunderstorm, violet_aurora...). Config: `customAurora/Moon/Stars/Shooting`, `skyWords`, `plainSkyColor`, `customHorizon/Top` | Reinforces that **sky presets are core to the look** (matches reference video B). Our shader-uniform / VV route handles the tint; the textured sky layers are extra draws in RF |
| **Weather-reactive FX** | `LevelRainMixin` + `WeatherKindMixin` + `Rain`/`Embers`/`Petals` classes; config `weatherRain/weatherSnow/weatherEmbers`, `petal.png` | Eye-candy that reacts to in-world weather. Nice-to-have, phase 4 |
| **Cosmetics (shared)** | `Cosmetics`/`CosmeticLayer`/`HelmetLayer` render player-worn cosmetics (axolotl, astronaut/knight helmet, buddy skin, petals, embers). `CosmeticSync` **broadcasts/reads them over a public `ntfy.sh` topic** (`ntfy.sh/lyricsflow-cosmetics-v1-...`) + Mojang profile lookups, so other LyricsFlow users see each other's cosmetics | **Lowest priority / likely skip for a personal build.** It's a cross-player social feature that needs other people running the mod, and it posts to a *public* ntfy topic (privacy note). None of the reference videos use it. Not worth porting unless Fernando wants it |
| Other config | `allCaps`, `floatHeight`, `twoColorStrength`/`twoColorColor`, hotbar bar color, `skyColor`/`plainSkyColor` | Trivial, fold into our config |

Net effect on the plan: **word sync moves from "data-limited" to "likely to work"** (v2.0 ships the sources), and the sky work is bigger than first thought. Cosmetics are the one chunk we can safely drop for a personal build.

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
| True word-by-word sync | **Likely to work (upgraded by v2.0).** v2.0 already fetches Musixmatch richsync + NetEase YRC, which carry per-word timing, falling back to interpolated LRCLIB when a song has none. Port those sources; Musixmatch's desktop API is unofficial/token-gated, so keep it as one source of several and mind its ToS (fine for a personal build). Coverage still varies by song |
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

## Queue placement (CONFIRMED by Fernando 2026-10-29)
Order: **Spatial GUI (DONE) -> Cinematic Interactions (in progress) -> LyricsFlow (this) -> GeckoBridge (last)**.
- Spatial GUI: **ported basically 1-to-1** (major breakthrough). The RF perspective-quad + UI-pass pieces LyricsFlow's
  world text reuses are now proven in-game, so S1/S2 start from a working base instead of from scratch.
- LyricsFlow kicks off once **Cinematic Interactions is fully tested and working**. Don't start before then (project rule:
  finish current work first).
- Spike S5 (MSDF/pixel text hello-world) is the natural first step at kickoff since it touches no existing feature.

## Reference videos (studied 2026-10-29, all Java Edition TikTok clips, 576x1024)
| Video | Source | What it shows | What it means for us |
|---|---|---|---|
| A: "falling letters" (66 s, @nia_teppelin, Russian, "Music Subtitles" mod, Telegram TG_Nia_Teppelin0) | **A different mod, not LyricsFlow.** No jar, so we recreate it from the video | Vanilla Minecraft pixel font, white with drop shadow. Words pop up in the world near the player, then the letters drop and **land flat on the ground**, each at its own random angle. They follow the terrain (grass steps, a statue pedestal, cobble paths) and stay there a while | The physics is simpler than full 3D tumbling. The key look is "letters land flat on top faces and lie there as decals". Phase 3 v1 = fall + small spin + land on the top face of the block below (raycast down) + settle + fade. Tumble/bounce is extra polish. Also needs a **Minecraft pixel font preset** (nearest-filtered bitmap atlas, not MSDF). Read the game's own font from the installed game at runtime instead of shipping Mojang's file, or use an OFL Minecraft-style font |
| B: "cursive sky" (31 s, @neonlyricsmod, Rihanna "We Found Love") | **Looks like LyricsFlow itself.** Same top karaoke bar with a purple highlight pill | SKY mode at night with an aurora/star sky preset. White thin cursive (Great Vibes / Dancing-style), mixed word sizes (`varySizes`), 2 lines, gentle fade-in word by word, soft glow | = LyricsFlow SKY + CLEAN/soft glow + "northern" sky FX. The sky FX/aurora is part of the look, so sky presets matter for this preset |
| C: "big neon" (31 s, @neonlyricsmod, "Hate Me" Ellie Goulding & Juice WRLD) | Looks like LyricsFlow (same top bar) | NEON style: bold italic brush font, white core + pink/blue glow, **huge and small words scattered at different depths** across a PvP arena and the sky, overlapping, drawn over players and nametags. White streak lines (sky streaks). Second half: pink sky preset with cursive neon in the sky | = LyricsFlow FLOATING/SKY + NEON + streaks. Text sits on top of players, so occlusion isn't needed to match the look, but it **does cover the opponent**, so a combat-safe preset (keep the crosshair area clear, fade on hit) is a must for Hive |
| D: "words on walls" (28 s, @kangetsuvisuals, "Spotify lyrics in your world") | Looks like LyricsFlow (same top bar) | WALL mode in a duels arena: blocky bold font with an orange/gold gradient and outline, **text fitted flat onto arena walls in perspective** ("goddamn pathetic"), a few words at a time, during combat | = LyricsFlow WALL + minecraft_gold theme. Proves `wallPlace`/`fitToSurface` (needs S4 block queries) is a core feature, not an extra |

Takeaways:
- 3 of the 4 videos are LyricsFlow itself, so porting its placement, styles and top bar recreates them. Video A is the only one that needs new work, and its physics is simpler than first assumed.
- Presets to ship: **"Ground Drop"** (A), **"Aurora Cursive"** (B), **"Neon Arena"** (C), **"Wall Gold"** (D), plus a **"Combat Safe"** modifier.
- The top karaoke bar (song title, current line, highlighted word) appears in 3 videos, so it's worth porting early (2D, easy).
- Fonts needed for the videos: Minecraft pixel (A, D), a thin cursive (B), a bold brush italic like Kaushan (C).

## Inputs still wanted for kickoff
- Permission: DONE. 374m OK'd personal-use modification; private build only, not distributed (see Sources above).
- For phase 5: Hive/CubeCraft rule check + a test session on a real server. (Personal-use only means no distributing the
  mod; it does NOT by itself make it allowed on a server. The Hive's "assume not allowed unless listed" rule still applies
  to a lyrics overlay, so ask their support before using it there.)
