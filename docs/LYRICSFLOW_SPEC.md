# LyricsFlow → Bedrock Forge: implementation spec (prep phase, 2026-10-29)

Builds on `docs/PORT_LYRICSFLOW.md` (feasibility, video analysis, phase plan). That doc says *whether and why*; this one
says *exactly what LyricsFlow v2.0 does* and *what we already built*. Source of truth: `LyricsFlow_Update.jar` **v2.0**
(374m, MIT, personal-use permission, credit in `third_party/lyricsflow/NOTICE.md`), decompiled with Vineflower.

- Secondary systems (config table, themes, text passes, top bar, sky presets, weather FX, SMTC calls, AudioSync, keys):
  `docs/lyricsflow/SPEC_SECONDARY.md`.
- **Project rule still applies:** this is prep. Integration and in-game testing start when Cinematic Interactions is done.

## 0. Status at a glance

| Piece | Where | State |
|---|---|---|
| Lyric parsers: LRC, enhanced LRC `<mm:ss.xx>`, NetEase YRC, Musixmatch richsync, plain-text spread, syllable word timing | `src/lyrics/core/Lyrics.*` | **Tested**, identical output to the original Java (goldens) |
| Search: LRCLIB get/search/by-title, NetEase LRC + YRC, Musixmatch token/matcher/richsync/subtitles, sped-up "stretched", duration tolerance, script check | `src/lyrics/core/Fetcher.*` | **Tested** with saved responses (no network) |
| TitleCleaner (clean, feat, main artist, YouTube split, variants, fold) | `src/lyrics/core/Text.*` | **Tested** against the Java original |
| Song clock (MediaManager drift/seek/resume logic, offsets) | `src/lyrics/core/Media.*` | **Tested** |
| WorldLyrics: scheduling, SKY/FLOATING/WALL/SCREEN placement, wall fitting, keep-out-of-blocks, animations | `src/lyrics/core/Scene.*` | **Tested** on a fake voxel world |
| Falling letters (new) | `src/lyrics/core/Physics.*` | **Tested**: land flat on ground/slab/step, tumble mode settles, fade, cap |
| Glyph quads with LyricsFlow's neon passes + SDF outline/glow/gradient (new) | `src/lyrics/core/Glyphs.*` | **Tested** |
| Settings (LFConfig fields + BF extras), themes, presets | `src/lyrics/core/Config.*`, `resources/lyricsflow/presets/` | **Tested** |
| BF→RF contract `BF_CopyLyricFrame` | `src/lyrics/BfLyrics.h`, `Engine.*` | **Tested** (frame store) |
| WinHTTP, SMTC (C++/WinRT), BlockSource world, `lyricsflow` module + keys | `src/lyrics/win/*` | **Compile-verified (CI)**, never run |
| Font atlases (8 SDF + pixel placeholder) + generator + previewer | `resources/lyricsflow/fonts`, `tools/LyricsFonts` | **Generated + previewed** on CPU with the shader maths |
| RF glyph shader + instanced pass + S1 camera hook + S2 under-HUD path | branch `lyricsflow/rf-scaffold` | **Compile-verified (CI, dxc)**, never run |

Everything that needs the game is staged, not verified. Section 7 is the runbook for kickoff day.

## 1. Architecture (as built)

```
Windows SMTC ──250 ms──► SongClock ──song change──► fetch thread (LRCLIB / NetEase / Musixmatch over WinHTTP)
                              │                                      │
                              ▼                                      ▼
HUD frame (BF render thread): CameraState + GameWorld(BlockSource) + Lyrics ──► Engine::frame
        Scene::tick/draw (WorldLyrics port) ──► DrawWord[] ──► buildWordGlyphs ──► BfGlyphV1[]   (+ LetterPhysics)
                                                                                     │ FrameStore (mutex)
RF Present (overlay thread): BF_CopyLyricFrame ──► upload ring ──► glyph.hlsl, 1 instanced draw ──► back buffer
                              camera: LyricsCamera hook (S1)  |  under-HUD: UiCapture redirect + composite (S2)
```
- **Core is pure C++20** (`src/lyrics/core`): no game, no Windows, no network. That's why most of the port is already
  tested. The game touch points are 3 files in `src/lyrics/win` + 2 in RF.
- **Positions are camera-relative** (`BfLyricFrameV1::origin` = BF's eye position, glyphs relative to it). RF adds
  `origin − real camera position` in double precision, so third person / bobbing parallax comes out right.
- **One draw call** for all glyphs (instanced, 112-byte `BfGlyphV1` in a structured buffer, 16k glyph cap per frame).

## 2. LyricsFlow v2.0 behaviour map (core)

### 2.1 Lyrics model (`Lyrics.java`)
- Line end = given end, else next line start, else `min(duration, start+6 s)` (or `start+5 s` without duration);
  capped at `start + 9 s`; extended to the last real word's end.
- **Word timing when the source has none** (`splitWords`): syllables per word (vowel groups incl. Turkish/Latin-1
  vowels, else letters/2.5), song "singing rate" = 75th percentile of line syllables/s × 1.05 clamped 2.5–10 (4.5 if
  <3 usable lines). A line sung at ≥90 % of that rate uses 74 % of its gap, slower lines `max(total/rate, 60 % gap)`,
  clamped to `[min(350, gap), 82 % gap]`, distributed by `syllables + 0.6`; words start ≥130 ms apart, 60 ms early.
- `retime(k)` stretches estimated words (AudioSync's hook - unused in v2.0, see below).

### 2.2 Sources and the search (`LyricsFetcher.java`) - 15 s budget
1. Build title/artist **variants** (raw; without feat.; cleaned; main artist + cleaned; dash-suffix cut; diacritics folded).
2. **Sped up / slowed / nightcore / daycore** titles: LRCLIB exact first, else find the *original* song on LRCLIB
   (duration ratio 0.55–1.6, not itself a speed version) and scale every time by `duration / original` ("stretched").
3. Otherwise, in parallel: Musixmatch richsync, NetEase YRC (both word-synced), and the "first" list (LRCLIB `/get`
   exact by duration, `/search` by track+artist, `/search?q=` with the last variant, `/search?track_name=` by title,
   NetEase LRC). First **synced** result in *list order* wins; then the "more" list (other variants).
4. Musixmatch result (≥3 lines) replaces it; else NetEase YRC replaces it if the current one isn't word-synced.
5. Nothing yet and >1.5 s left: retry the first list with a 10 s duration tolerance.
6. Reject lyrics in a different script than a mostly-Latin title (>50 % non-Latin letters).
- Duration tolerance 3 s (2 s for remixes), scores: synced 0 / plain +1000, `|Δduration|·4`, title miss +200, artist
  miss +300, reject >1200 (LRCLIB search); by-title: plain +500, artist miss +50, reject >560.
- NetEase LRC drops Chinese credit lines (`汉字…:`) and `lyrics|composer|producer|arranger|written by:` lines.
- Musixmatch: `token.get` (cached 6 h, refused if `UpgradeOnly…`), 401 → new token once; `matcher.track.get` must match
  title (prefix either way) and artist (contains either way) and duration; richsync first, else LRC subtitles.
- **Port note:** Musixmatch's desktop API is unofficial and token-gated; kept as one source of several for this
  personal build (LyricsFlow does the same). Fallbacks cover it when it refuses.
- MediaManager retries a failed search up to 5× (waits 1.5 s × attempt) unless the song changes.
- On-disk lyrics cache: `config/lyricsflow_cache/<fnv64 of artist\0title>.json` (`toCacheJson`/`fromCacheJson`).

### 2.3 Clock (`Track` / `MediaManager.tick`)
- Poll 250 ms (120 ms once after a command). Position extrapolates from the sample time while playing.
- Re-sync to Windows only when trusted: a **seek** (raw position jumped >400 ms with a new LastUpdated time) seen on
  ≥1 stable poll, or a **fresh** update seen on ≥2 stable polls (offset consistent within 60 ms + 20 % of the poll
  gap), and the error is >250 ms (120 ms when forced). Otherwise keep our own clock (Windows positions are coarse).
- Resume: continue from where we paused unless Windows is >3 s away. 2.5 s without a session = Stop.
- Lyrics position = clock + `syncOffsetMs` + per-song offset (`[`/`]` = ∓100 ms, ±5 s cap, key lower(artist\0title)).

### 2.4 WorldLyrics (`WorldLyrics.java`) - scheduling
- Current line = last line started; instrumental gaps (>2.6 s to the next line, or no lyrics found) show `♪ ♫` lines
  every 1.7 s in the `minecraft` font (`musicNotes`).
- **FLOATING** and **SKY+skyWords** are "chunked": one placement per word, shown from 80 ms before the word; others place
  the whole line at once. A new line's first chunk starts the exit of older lines.
- Live limits: chunked `max(2, maxLines·3)`, else `maxLines` (SKY pairs ≥2). Linger after the last word: FLOATING 0.9 s,
  SKY 1.5 s, others 0.6 s. Seeking back >1.5 s clears everything.
- Visibility: in-view cone 52° yaw / 42° pitch (WALL: within 40 blocks); looking away fades out and drops it.
  Occlusion = raycast camera→anchor every 6 ticks (SKY: underground check every 10 ticks), faded at speed 6.

### 2.5 Placement (`place`, `wallPlace`, `fitToSurface`, `keepOutOfBlocks`)
Each new placement tries N candidates (`randomPosition`: FLOATING 40, others 12; else 1, SKY 8) and keeps the best score.
- **SKY** (pairs): distance `clamp(skyDistance, 20, 0.85·far)`, even lines left/up, odd lines right/down next to
  their mate (`h = 2 em°`, `v = 1.9 em°`), pitch ≤ `camPitch − 8°` (above the view), penalties: 450 per blocked ray
  (3 rays ±8°), 500 inside the cloud layer (±6 blocks).
- **SKY + skyWords**: words scattered ±36° yaw, pitch `camPitch/2 − 16° ± 15°` clamped −55..−4.
- **FLOATING**: yaw `cam ± (8..34°)`, pitch `0.6·cam − 9° ± 13°` clamped −40..28, distance `(5..7.5)·lyricsDistance`;
  penalties: crosshair (<4°/3°) −250, nearby players/target box (+12°/+6° margin) −4000 and −1500 if any is near the
  crosshair, blocked ends/edges −800/−700 each, clouds −500, and if terrain is closer than `dist+4` it moves in front
  of it (−800, −700 more if <2.2 blocks). Same-line words must not overlap (−12000 / −600 near).
- Spread: −40 per degree under 22° to the nearest other text, +4 per degree up to 60°.
- **WALL**: 10 rays (±35° yaw, pitch `cam/2 ± 12°` clamped −30..25, 16 blocks) onto *vertical* faces ≥2.5 blocks away;
  score prefers ~6 blocks, straight ahead, not crowded, solid wall ±1.1/±2.2 blocks sideways. No wall → lay it flat on
  the ground 4.5–7 blocks ahead. Then `fitToSurface`: try scale 1, .8, .65, .5 × lift 0/±1 × slide 0..±3 until every
  sample point (0.45 grid) has a solid block 0.3 behind and empty space 0.3 in front. Text sits 0.04 off the face.
- **FLOATING keep-out-of-blocks**: if any word overlaps a block's collision box, pull it toward the camera in 0.4 steps
  (min 1.8), else lift up to 3 blocks.
- Layout: word sizes (`varySizes`: 1.4 / 1.12 / 0.96 / 0.82 by hash, short first word 1.35), widths from the font,
  rows wrapped at 9.5 em (SKY 18, chunked 99; SKY rewraps until ≤3 rows), row gap 1.5 (SKY 1.4), `arcLayout` bends
  rows (0.03·x², SKY 0.008·x²) and tilts (SKY ±1.5°, skyWords ±22°, others −10..+4°, WALL ±3°), per-word rotation ±4°.
  RTL lines mirror x.
- Text size `em`: SKY `150·tan(3.1°)·scale`, FLOATING `0.0765·max(2.5, dist)·scale·fit`, others `0.085·max(2.5, dist)`.

### 2.6 Animation (`wordAnim`)
- Enter (300 ms ÷ animSpeed, from 40 ms early): WORD = easeOutCubic alpha + rise 0.45 em + easeOutBack scale 0.82→1;
  FADE; RISE (0.9 em).
- Exit (650 ms ÷ speed, staggered 55 ms per word): alpha × (1 − easeInOut); FLOAT_UP rises 1.6 em and grows 8 %;
  DRIFT slides 1.8 em sideways; FADE.
- Held last word (≥650 ms) pulses on an 860 ms two-bump "heartbeat" (+8.5 %). Bob: `sin(1.6 t + 0.9 j + seed)` ×
  0.04 em (SKY 0.02, WALL none).
- Sung word: glow +0.35 (we feed it to the SDF glow; in v2.0 the glow value is computed but unused).

### 2.7 Looks
- Text = up to 5 flat passes (soft/strong shadow, two offset colour passes `±oo` with theme a/b, main) stacked
  0.06 font units apart (WALL 0.006 blocks) - see SPEC_SECONDARY §3.1. **No real glow, gradient, outline or per-letter
  colour in v2.0** → ours are additions (`outlineWidth/Color`, `glowWidth/Strength/Color`, `gradient/Color`,
  `perLetterColor`, `hueShiftSpeed`).
- 22 themes ported exactly (`Config.cpp`), custom theme + single colour override, Java colour-lerp truncation.

### 2.8 Not used / skipped
- **AudioSync (WASAPI loopback) is dead code in v2.0** (nothing calls it). Documented in SPEC_SECONDARY §8. Skip
  unless line-sync songs feel off; it would only rescale estimated word times.
- **Cosmetics + CosmeticSync**: skipped (posts to a public ntfy.sh topic, needs other users).
- `sentenceMode`, `customWeather`, `hideInScreenshots`: inert in v2.0. `viewLock`/`randomPosition` are forced
  false/true on load in v2.0; we keep them as settings.
- Iris "late for shaders" 2D fallback: not needed, RF always draws real 3D after tonemapping.
- "Ghost" see-through pass over players: not needed (we draw without depth test, like `SEE_THROUGH`).

## 3. What we added (not in LyricsFlow)
- **Falling letters** (`Physics.*`, settings `fallingLetters`, `letterLand` FLAT|TUMBLE, `fallDelayMs`, `gravity`,
  `bounce`, `friction`, `spin`, `letterLifetimeMs`, `letterFadeMs`, `maxLetters`): when a word's exit starts (+delay,
  +55 ms per word), its main-pass glyphs become rigid quads; fixed 120 Hz substeps, gravity, air drag, spin; FLAT turns
  them face-up while falling and lays them on the **top face of the block below** (video A); TUMBLE bounces on the
  lowest corner and settles flat; walls bounce them back; rest → lifetime → fade; oldest dropped past the cap.
- **SDF text**: sharp at any distance, outline + glow + gradient in the shader.
- **Combat-safe** (`combatSafe`, `keepOutDeg`, `combatAlpha`, `combatHoldMs`): FLOATING candidates inside the crosshair
  cone are rejected (−6000), any text near the crosshair fades to 15 %, and a combat signal can cap opacity (the hit
  signal itself is wired at kickoff: BF already sees attacks/hurt events).
- Source `ANY` is allowed (v2.0 forces SPOTIFY on load).

## 4. Text folding (deviation from Java, on purpose)
Java uses ICU-grade `Normalizer` + Unicode categories. We fold ASCII, Latin-1, Latin Extended-A, Turkish ı/İ, Greek
tonos and Cyrillic й/ё/ї/ў, and treat other non-punctuation code points as letters. The goldens cover Latin, Turkish,
Polish, Greek and Cyrillic samples and match. Vietnamese (Latin Extended Additional) diacritics are not folded yet.
`std::regex` handles TitleCleaner's ASCII patterns; the one Unicode bracket (`【…】`) is handled by hand.

## 5. BF ↔ RF contract (`BfLyrics.h`, identical in both repos)
`BF_CopyLyricFrame(BfLyricFrameV1* hdr, BfGlyphV1* dst, uint32_t cap)` - copy-out under a mutex (BF writes on its HUD
frame, RF reads at Present). Header: active flag, frame counter, double `origin`, glyph count/total, BF camera estimate
(yaw/pitch/FOV), up to 8 atlas PNG paths + generation. Glyph: centre + half-extent right/up vectors (camera-relative),
atlas uv rect, RGBA8 fill/fill2/outline/glow, alpha, outline/glow widths, atlas index, flags (SCREEN, BITMAP, MSDF,
LETTER, GRADIENT), SDF pxRange. 112 bytes, `static_assert`ed.

## 6. Presets (`resources/lyricsflow/presets/*.json`, `{"name", "settings": {…}}` overlays)
| Preset | Video | Key settings |
|---|---|---|
| Ground Drop | A | FLOATING, `minecraft` pixel font, CLEAN + shadow, no glow, **fallingLetters FLAT**, 1 line |
| Aurora Cursive | B | SKY pairs, `greatvibes`, white, soft SDF glow, varySizes, FADE in, `northern_lights` sky |
| Neon Arena | C | FLOATING, `kaushan`, neon_pink two-colour 0.85, strong glow, scale 1.35, 3 lines, streaks |
| Wall Gold | D | WALL, `minecraft`, minecraft_gold, yellow→orange gradient, dark outline, strong shadow |
| Combat Safe | modifier | combatSafe, 14° keep-out, 35 % in combat, 1 line, opacity .85 |
Sky presets (`skyPreset`) are parsed into config but not rendered yet. The **top karaoke bar is done** (`TopBar.*`,
screen-space glyphs + `BF_GLYPH_SOLID` rounded shapes): title/artist, pause state, karaoke line with the highlight pill
gliding to the sung word, vinyl icon, EQ bars. No cover art / logos / clock-ping-fps chips yet.

CPU previews (rendered by `tools/LyricsFonts/render_frame.py` from frames the core tests dump with
`LYRICS_DUMP_DIR=<dir>`; same maths as `glyph.hlsl`, fake world, BF camera estimate):
`docs/img/lyricsflow_presets_preview.png` (Neon Arena, Aurora Cursive / Wall Gold, Ground Drop - letters lying flat on
the ground, seen edge-on) and `docs/img/lyricsflow_topbar_preview.png`.

## 7. Kickoff-day runbook (needs the PC; in this order)
1. **Merge order:** `lyricsflow/scaffold` (BF) and `lyricsflow/rf-scaffold` (RF, cut from `spatialgui-m0` @ c0c6d24)
   must be rebased on whatever main/cinematic look like then. RF touches `Overlay.cpp`, `GUI.cpp`, `Entry.cpp` (one line
   each) and `UiCapture.cpp` (redirect target helper, 6 lines) - expect small conflicts with Cinematic.
2. Copy `resources/lyricsflow/{fonts,presets}` next to the BF DLL (`mods/BedrockForgeCore/resources/lyricsflow/`).
   Optional: build the real Minecraft font: `python tools/LyricsFonts/make_fonts.py --out … --default8 "<game>/…/font/default8.png"`.
3. **SMTC log check:** turn on the `lyricsflow` module, play Spotify → BF log / F6 shows track + position; seek and
   pause in Spotify, check the clock follows.
4. **S5 hello world:** F6 "LyricsFlow (preview)" → status shows glyph count; SCREEN style first (no camera needed).
5. **S1 camera:** FLOATING; walk, strafe, jump, F5 third person. Text must stay glued to the world. If it slides or
   mirrors: toggle "Flip forward axis"; compare "game hook" vs "BF estimate"; check the hook counters move.
6. **S2 under HUD:** set `"uiCaptureSpike": true`, enable "Under the HUD". Hotbar/crosshair must stay on top and
   chat/inventory must still render. If the HUD flickers or vanishes, turn it off (default path draws over the HUD).
7. **S3/S4 world:** WALL near a wall (fits flat? crosses corners?), FLOATING into a hill (pulled out of blocks?), go
   underground in SKY (hidden?). Then **Ground Drop**: letters must land on grass/steps/slabs (slabs/stairs are full
   cubes for now - see open items).
8. Perf: 3 lines + falling letters on the RTX 4060, F6 glyph count, frame time.
9. Servers last (rules check for Hive/CubeCraft first - PORT_LYRICSFLOW.md "Servers").

## 8. Open items
- Real collision shapes for slabs/stairs/snow layers in `World_win.cpp` (now: solid = full cube, rest passable);
  real sky light (`getBrightnessPair`) for the underground check.
- ~~Lyrics disk cache~~ done (`config/lyricsflow_cache/<fnv>.json`); album-art palette (SMTC thumbnail); top karaoke bar; sky presets/tint/streaks/stars; weather FX;
  ImGui settings page in RF for the BF config (today: `config/lyricsflow.json` + presets).
- ~~Combat signal~~ done: BF's local attack hook + local hurt event call `noteCombat()`; opacity eases back over `combatHoldMs`.
- Commit `src/GlyphShaders.h` into the RF branch from the CI artifact (so a PC build doesn't need dxc).
- The other 14 LyricsFlow fonts: `make_fonts.py --fonts <id>` after copying the TTF + licence from the jar.
- True MSDF atlases (msdf-atlas-gen) if huge text shows rounded corners; the loader and shader already support them.
- Keys: LyricsFlow's K (menu) / J (player) clash with BF's impact/HUD keys; only O (on/off) and `[` `]` (offset) are bound.
