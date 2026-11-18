<!-- Part of docs/LYRICSFLOW_SPEC.md: secondary systems of LyricsFlow v2.0 (config, themes, text passes, top bar, sky, weather, media backend, AudioSync, keys), written from the decompiled v2.0 jar on 2026-10-29. The core (lyrics, search, WorldLyrics placement, clock) is in the main spec. -->

# LyricsFlow v2.0 → Bedrock port spec (secondary systems)

Source: decompiled `dev.lyricsflow.*` under `/home/claude/untrusted/src/lyricsflow/`. Assets: `/home/claude/untrusted/jar/x/assets/lyricsflow/`.
Not covered here (other engineer): `lyrics/*`, `render/WorldLyrics.java`, `media/Track.java`, `media/MediaManager.java`.

Conventions used in the source, which the port has to match:
- Colours are packed `int` ARGB (`0xAARRGGBB`). A config colour of `0` means **"auto"** (use the theme default). The JSON stores `"auto"`, `#RRGGBB` (alpha FF), or `#AARRGGBB`.
- MC angles: `yaw` in degrees, `pitch` in degrees with **negative = up** (pitch −90 is the zenith). `Proj.dir(yaw,pitch) = (−sin y·cos p, −sin p, cos y·cos p)`.
- GUI units are MC "scaled GUI pixels". `Gfx.scale()` = GUI scale factor. Everything is drawn in GUI units and snapped to real pixels via `round(v*scale)`.
- `I` / intensity values are 0..1 fade multipliers applied to alpha.

---

## 1. Config (`config/LFConfig.java`, `config/ConfigManager.java`)

### 1.1 Storage
- Path: `<.minecraft>/config/lyricsflow/config.json` (UTF-8, pretty-printed). Written atomically: `config.json.tmp`, then rename with REPLACE_EXISTING + ATOMIC_MOVE (falls back to a direct write).
- On disk the JSON is **grouped**: `_info` (string), `configVersion`, then objects `general`, `music`, `lyrics`, `style`, `sky`, `topBar`, `positions`, then `other` for any leftover keys, then `profiles`. Loading flattens every nested object except `profiles`, and ignores keys that start with `_`.
- Colour fields are any key ending in `Color` (except `twoColor`), plus `customMain`, `customA` and `customB`. They are written as `"auto"` when 0, `#RRGGBB` when alpha = 255, otherwise `#AARRGGBB`. On read, a string starting with `#` is parsed as hex: if `len<=7` it is OR'd with `0xFF000000`. Any other string becomes 0.
- Keys dropped on save/snapshot: `connection`, `spotifyClientId`, `spotifyRefreshToken` (legacy).
- **Profiles / share codes**: `profiles: Map<String,String>` maps a name to a snapshot JSON (the whole config minus `profiles` and the dropped keys). Export code is `"LF1:" + Base64(Deflate level 9(utf8 json))`. Import accepts either `LF1:…` (whitespace stripped) or raw `{…}` JSON. `applySnapshot` keeps the current `profiles` and forces `configVersion=14`. `reset()` restores defaults but keeps `profiles`.

### 1.2 Load-time migrations (configVersion now 14)
Applied in order on load:
- `source==null||ANY` → `SPOTIFY`.
- `ver<4` → `lyricsStyle=FLOATING`.
- `ver<5` → `soundStyle=0`.
- Key `skyPreset` missing → `"watercolor"`. Value `"candy"` → `"lilac"`. Note that `"lilac"` is not a preset id, so `byId` falls back to `off`.
- Keys `stars`, `motes`, `streaks` missing → `true`.
- `ver<6` → `songChime=false`.
- `ver<7` → `ConfigManager.moveKeys=true`, which remaps old keybinds (see §9).
- `ver<8` → `stars=streaks=true`.
- `ver<9` → `beatGlow=false`.
- `ver<10` → `customHotbar=false`.
- `ver<11` → `twoColorStrength = twoColor?0.6:0`.
- `ver<12` → `followView=false`.
- `ver<13` → `customWeather=0`.
- `ver<14` → `syncOffsetMs=0`.
- **Always on load**: `viewLock=false` and `randomPosition=true`. Both are effectively forced.

### 1.3 Field table
Ranges come from the in-game UI sliders/choices in `gui/Pages.java`; the code itself does not clamp. "WL" means the field is consumed by `WorldLyrics` (other engineer's file). Fields marked **(skip)** are cosmetics/UI-only for our build.

| Group | Field | Type | Default | Range / values | Purpose |
|---|---|---|---|---|---|
| — | configVersion | int | 14 | — | Migration version |
| general | enabled | bool | true | | Master switch for everything. Hotkey O toggles it |
| general | uiSounds | bool | true | | Menu click sounds **(skip, UI)** |
| general | uiVolume | float | 0.6 | 0..1 step .05 | UI sound volume **(skip)** |
| general | soundStyle | int | 0 | 0 classic, 1 serious, 2 minecraft | UI sound set **(skip)** |
| general | songChime | bool | false | | Plays the `UiSounds.SONG` chime on a SONG event |
| general | mediaKeys | bool | true | | Lets the play/pause/next/prev keybinds work |
| general | hideInScreenshots | bool | true | | **Dead in v2.0**: `ScreenshotHelper.defer()` always returns false |
| music | source | enum Source | SPOTIFY | SPOTIFY, YOUTUBE (ANY exists but is migrated to SPOTIFY) | SMTC session filter, see §7 |
| music | syncOffsetMs | int | 0 | −2000..2000 step 50 | Global lyric offset added to the position |
| (other) | songOffsets | Map<String,Integer> | {} | each value −5000..5000 | Per-song offset in ms. Key is `lower(artist+"\0"+title)`. Set by the `[`/`]` keys (±100 ms) |
| lyrics | showLyrics | bool | true | | Draw lyrics (WL) |
| lyrics | lyricsStyle | enum | FLOATING | SKY, FLOATING, SCREEN, WALL | Lyric placement mode (WL). SKY also makes streaks spawn "far" |
| lyrics | sentenceMode | bool | false | | **Unused** in v2.0 |
| lyrics | allCaps | bool | false | | Upper-cases display text (`Lyrics.show`) |
| lyrics | floatHeight | float | 0.0 | −1..4 step .25 | FLOATING vertical offset (blocks) (WL) |
| lyrics | skyWords | bool | false | | SKY mode word option (WL) |
| lyrics | randomPosition | bool | true | forced true | Random placement jitter (WL). SCREEN: ±0.26·r1 in x, ±0.12 in y |
| lyrics | followView | bool | false | | FLOATING: follow the camera (WL) |
| lyrics | viewLock | bool | false | forced false | (WL) |
| lyrics | showUnderground | bool | false | | Show lyrics when the sky isn't visible (WL) |
| lyrics | musicNotes | bool | true | | ♪ glyphs in instrumental gaps (WL) |
| lyrics | lyricsScale | float | 1.0 | 0.4..2.5 step .05 | Text size multiplier. SCREEN em = 15 px × scale |
| lyrics | lyricsDistance | float | 1.0 | 0.4..2.0 | FLOATING distance multiplier (WL) |
| lyrics | skyDistance | float | 150 | 30..300 step 10 (m) | SKY mode distance (WL) |
| lyrics | textShadow | int | 1 | 0 off, 1 soft, 2 strong | See §3.1 |
| lyrics | shadowColor | int(ARGB) | 0 auto | colour | Overrides the shadow colour (alpha forced FF) |
| lyrics | textOpacity | float | 1.0 | 0.2..1.0 | Global lyric alpha |
| lyrics | maxLines | int | 2 | 1..3 | Concurrent visible lines (WL) |
| lyrics | varySizes | bool | true | | Per-word size variation (WL) |
| lyrics | arcLayout | bool | true | | Words laid out on an arc (WL) |
| lyrics | enterAnim | enum | WORD | WORD, FADE, RISE | Word enter animation (WL) |
| lyrics | exitAnim | enum | FLOAT_UP | FLOAT_UP, DRIFT, FADE | Line exit animation (WL) |
| lyrics | animSpeed | float | 1.0 | 0.5..2.0 | Animation speed multiplier (WL) |
| style | font | String | "lyricsflow_script" | ids from §3.3, or `custom_*` | Lyric font. Choosing CLEAN style switches it to `poppins` unless it is custom/quicksand/minecraft |
| style | textStyle | enum | NEON | NEON, CLEAN | Text render style, §3.1 |
| style | theme | String | "neon_pink" | ids from §2.1 | Colour theme |
| style | customTheme | bool | false | | Use customMain/A/B instead of the theme |
| style | singleColor | int | 0 auto | colour | Replaces the theme's `main` colour only |
| style | customMain | int | #FFE3F6 | hue slider 0..358°, S=.12 V=1 | Custom theme main |
| style | customA | int | #FF5AC8 | hue, S=.7 V=1 | Custom theme A |
| style | customB | int | #6FD8FF | hue, S=.6 V=1 | Custom theme B |
| style | twoColor | bool | true | | Enables the two-colour offset passes (passed into `Gfx.neon`) |
| style | twoColorStrength | float | 0.6 | 0..1 step .05 | Strength of the offset passes. ≤0.01 means off |
| style | twoColorColor | int | 0 auto | colour | One colour used for both offset passes instead of a/b |
| style | glow | bool | true | | `glowBase=0.65` in WL. **Note:** `Gfx.neon` ignores its `glow` argument in v2.0 |
| style | beatGlow | bool | false | | Top-bar beat glow, and adds `beat*0.5` to WL glow |
| style | accentColor | int | 0 auto | colour | Accent. Auto = `lerp(theme.a, #8B5CF6, 0.45)` |
| sky | skyPreset | String | "off" | ids from §5.2 (+`custom`) | Sky preset |
| sky | skyColor | int | 0 | colour | Simple sky tint. Overrides the preset's tint colours (§5.4) |
| sky | plainSkyColor | int | 0 | colour (PLAIN_PALETTE) | Flat sky colour, only used when preset = off |
| sky | skyAlways | bool | true | | Sky FX on even when nothing is playing |
| sky | chroma | bool | false | | Chroma post-effect (§5.5) |
| sky | chromaColor | int | 0 | 0..7 = lilac, black, grey, purple, pink, blue, red, gold | Chroma look index |
| sky | stars | bool | true | | SkyFx stars + vanilla star brightness boost |
| sky | starDensity | float | 0.6 | 0.1..1.0 | Star count = 140·d |
| sky | starColor | int | 0 | colour | Auto = lerp(white, theme.main, .3) |
| sky | motes | bool | false | | Floating motes near the player |
| sky | moteDensity | float | 0.5 | 0.1..1 | Mote count = 60·d |
| sky | moteColor | int | 0 | colour | Auto = theme a/b (random per mote) |
| sky | streaks | bool | true | | Falling light streaks |
| sky | streakDensity | float | 0.5 | 0.1..1 | Spawn rate scale |
| sky | streakColor | int | 0 | colour | Auto = lerp(white, theme.main, .25) |
| sky | customTop | int | #000000 | SKY_PALETTE | Custom preset zenith colour (pure black is mapped to #010101 for top/bottom) |
| sky | customHorizon | int | #000000 | SKY_PALETTE | Custom preset horizon colour |
| sky | customAurora1 | int | #FFFFFF | AURORA_PALETTE | Custom aurora colour 1 |
| sky | customAurora2 | int | #FFFFFF | AURORA_PALETTE | Custom aurora colour 2 |
| sky | customAurora | bool | true | | Custom: BANDS layer |
| sky | customStars | bool | false | | Custom: STARS layer |
| sky | customMoon | bool | false | | Custom: MOONGLOW layer |
| sky | customShooting | bool | true | | Custom: SHOOT layer |
| sky | customWeather | int | 0 | — | **Unused** in v2.0 |
| sky | weatherRain | bool | false | mutually exclusive with snow (UI) | Forces rain FX (§6) |
| sky | weatherSnow | bool | false | | Forces snow FX |
| sky | weatherEmbers | bool | false | | Embers FX |
| topBar | showTopBar | bool | true | | Draw the top karaoke bar |
| topBar | customHotbar | bool | false | | Restyles the vanilla hotbar sprites **(skip / optional)** |
| topBar | hotbarColor | int | 0 | colour | Hotbar selection accent. Auto `#9F7BE6` (−6325530) |
| topBar | hotbarBarColor | int | 0 | colour | Hotbar bg. Auto `0xD8222124`. Custom = `0xD8`+rgb |
| topBar | topBarStyle | enum | KARAOKE | KARAOKE, SIMPLE | SIMPLE never shows lyrics in the bar |
| topBar | topBarFont | String | "outfit" | outfit, syne, quicksand, minecraft | Bar font |
| topBar | topBarScale | float | 1.0 | 0.6..1.8 | Bar scale `k` |
| topBar | barColor | int | 0 | colour | Bar bg. Auto `0xF0161321`. Custom = `withAlpha(darken(c,.78),240)` |
| topBar | clock | bool | false | | HH:mm chip left of the bar |
| topBar | ping | bool | false | | Ping chip right of the bar |
| topBar | fps | bool | false | | FPS chip right of the bar |
| topBar | showOnlyWhenPlaying | bool | true | | When false the bar always shows |
| topBar | hideWhenStopped | bool | false | | Hide the bar 3 s after PAUSED |
| topBar | dimWhenIdle | bool | true | | Dim the bar to 0.6 alpha 2 s after PAUSED |
| positions | topBarX | float | 0.5 | 0..1 (HUD editor) | Bar centre x as a fraction of screen width |
| positions | topBarY | float | 0.02 | 0..1 | Bar top y as a fraction of screen height |
| positions | screenLyricsX | float | 0.5 | 0..1 | SCREEN-mode anchor x (fraction) |
| positions | screenLyricsY | float | 0.32 | 0..1 | SCREEN-mode anchor y. If the anchor lands within (±0.18, ±0.12) of the screen centre it is forced to 0.32 |
| (other) | cosmetic | String | "none" | Cosmetics.IDS | **(skip)** |
| — | profiles | Map<String,String> | {} | | Saved profile snapshots |

---

## 2. Themes and colour math (`render/Themes.java`, `util/Col.java`)

### 2.1 Theme table
`Theme(id, main, a, b, skyTop, skyBottom)`. `main` is the fill colour, `a`/`b` are the two-colour offset/glow colours, and `skyTop`/`skyBottom` are used by the GUI/NowPlaying. An unknown id falls back to `neon_pink`.

| id | main | a | b | skyTop | skyBottom |
|---|---|---|---|---|---|
| neon_pink | #FFE3F6 | #FF5AC8 | #6FD8FF | #2A1A44 | #D98BB5 |
| ice_blue | #E8FBFF | #3FB8FF | #C8F0FF | #0F2440 | #7FB8E0 |
| sunset | #FFF0E0 | #FF5A8A | #FFB347 | #3A1838 | #FF9A6A |
| matrix | #E6FFE9 | #1D9E75 | #5AFF8A | #04140A | #1D6E45 |
| lavender | #F7F0FF | #9B5CFF | #FFFFFF | #2A2040 | #B9A2E8 |
| ocean | #E6FFFB | #2EE6D6 | #3B4CCA | #0C2A35 | #3C8EA8 |
| fire | #FFF4D6 | #FF3C3C | #FFB02E | #3A1010 | #E0602A |
| sakura | #FFF0F5 | #FF8FB8 | #FFD1E1 | #3D2030 | #F2A9C4 |
| galaxy | #F0E8FF | #7C3AED | #22D3EE | #0B0820 | #4B2A8A |
| gold | #FFFBE6 | #FFB800 | #FFF3A0 | #2E2408 | #D9A441 |
| vaporwave | #FFFFFF | #FF71CE | #01CDFE | #2A1A40 | #B967FF |
| toxic | #F4FFE0 | #A3FF12 | #B026FF | #1A2408 | #5E8A1E |
| blood_moon | #FFE6E6 | #B3001B | #FF6B6B | #24070B | #8A1E2A |
| aurora | #EFFFF8 | #00E5A0 | #8B5CF6 | #071A24 | #2A8A7A |
| candy | #FFFFFF | #FF9AD5 | #9AE8FF | #3A2A40 | #E7A8D8 |
| midnight | #E6ECFF | #3B4CCA | #A5B4FC | #0B1026 | #3A4A8A |
| minecraft | #FFFFFF | #3F3F3F | #3F3F3F | #78A7FF | #C6DBFF |
| minecraft_gold | #FFFF55 | #3F3F15 | #AA7700 | #241A3A | #8A6AC8 |
| enchant | #E0C8FF | #AA00AA | #5555FF | #120A24 | #5A3A9A |
| nether | #FFD0A0 | #AA0000 | #FF5500 | #2A0808 | #7A2410 |
| black | #0B0B0F | #4A4A55 | #9A9AAA | #050506 | #3A3A42 |
| black_white | #FFFFFF | #0B0B0F | #2A2A30 | #050506 | #2A2A30 |

All colours have alpha FF.

`Themes.SINGLE` (the swatch palette for `singleColor`): #FF4A4A, #FF9A3C, #FFE14A, #4ADE80, #2EE6D6, #4A8DFF, #3B4CCA, #9B5CFF, #FF6EC7, #FFC94A, #FFFFFF, #C8C8D4, #5A5A66, #0A0A0E.

### 2.2 Theme resolution (`Themes.current()`)
```
base = byId(cfg.theme)
if cfg.customTheme: Theme("custom", customMain, customA, customB, darken(customA,0.82), lerp(customA,customB,0.5))
elif cfg.singleColor != 0: base with main = singleColor
else: base
accent() = cfg.accentColor != 0 ? accentColor : lerp(current().a, #8B5CF6, 0.45)
```

### 2.3 Colour helpers (`Col`)
- `lerp(c1,c2,t)`: per-channel ARGB linear interpolation. `t` is clamped to 0..1. Each channel is computed as `(int)(c1 + (c2−c1)·t)` (truncation, not rounding) and clamped to 0..255.
- `alpha(c, mul)`: if `mul≥1` return c unchanged. If `mul≤0`, return rgb with alpha 0. Otherwise the new alpha is `(int)(A(c)·mul)`.
- `withAlpha(c, a)`: replace alpha with clamp(a).
- `brighten(c,x) = lerp(c, (c&0xFF000000)|0xFFFFFF, x)`. `darken(c,x) = lerp(c, c&0xFF000000, x)`.
- `hsv(h,s,v)` / `toHsv`: java.awt HSB. h is in 0..1.

**Gradients, per-letter and animated colour:** none of these exist in the covered files. Lyric words are drawn in flat `theme.main` plus the two offset passes (§3.1). The CLEAN style uses `lerp(main, white, 0.2)`. There is no per-letter or rainbow colour anywhere in v2.0. The only "gradients" are the vertical/horizontal rect gradients in `Gfx` and the sky gradients in §5.

---

## 3. Text and 2D drawing (`render/Gfx.java`, `render/Fonts.java`, `render/CustomFonts.java`)

### 3.1 `Gfx.neon` — the lyric word renderer (exact passes)
Signature: `neon(g, s, font, cx, y, size, main, a, b, alpha, twoColor, glow)`. Text is horizontally centred on `cx`. `y` is the top. If `alpha≤0.01` nothing is drawn.
```
w = textWidth(s,font,size);  x = cx − w/2
o = max(0.6, size·0.055)                       // offset unit
clean = cfg.textStyle == CLEAN
softShadow = clean ? #15132A : darken(a, 0.75)
```
Draw order (back to front):
1. **Shadow** (`textShadow` mode):
   - Mode 0: none.
   - Mode 1 (soft): one pass at `(x+0.6o, y+1.0o)`, colour `soft` × alpha.
   - Mode 2 (strong): two passes, `(x+1.3o, y+2.0o)` and `(x+0.7o, y+1.1o)`, colour `strong` × alpha, where `strong=#0A0612`.
   - If `shadowColor≠0`, both soft and strong become `shadowColor|0xFF000000`.
2. **CLEAN**: one pass at `(x,y)`, colour `lerp(main, #FFFFFF, 0.2)` × alpha. Done.
3. **NEON**: if `twoColor && twoColorStrength(tcs) > 0.01`:
   ```
   ca = twoColorColor≠0 ? twoColorColor|FF000000 : a
   cb = twoColorColor≠0 ? twoColorColor|FF000000 : b
   oo = o·(0.35 + 0.65·tcs)
   ta = alpha·min(1, 0.3 + 0.7·tcs)
   pass A at (x−oo, y+0.5oo) colour ca×ta
   pass B at (x+oo, y−0.5oo) colour cb×ta
   ```
   Then the main pass at `(x,y)`, colour `main×alpha`.

So: maximum 5 text draws per word (2 shadow + 2 offset + main). There is **no blurred glow halo**. The `glow` argument is unused in v2.0, and so is the config `glow` → `glowBase` value computed in WorldLyrics. Text draws with `Col.a<4` are skipped.

### 3.2 `Gfx.text` primitive
`k = size / Fonts.height(font)`. Translate to `(x, y + size·Fonts.yShift(font))`, scale by k, then draw the vanilla string without the MC drop shadow. Returns the advance `unitWidth·size`.
`textCentered` → `x = cx − w/2`. `ellipsize` uses a binary search on the char count and appends `…`, trimming trailing whitespace.

### 3.3 Fonts
- `Fonts.SIZE = 16`. `height(id) = 16` for TTF fonts and `9` for `minecraft`. `unitWidth = vanillaWidth(styledText)/height`, cached (cache cleared when >3000 entries).
- Lyric fonts (id → name): lyricsflow_script (LyricsFlow Script), kaushan, dancing, pacifico, yellowtail, sacramento, greatvibes, allura, parisienne, alexbrush, damion, marck, courgette, norican, grandhotel, yesteryear, quicksand, poppins, outfit_bold, minecraft. Followed by `CustomFonts.list()`.
- UI fonts: outfit, syne, quicksand, minecraft. Constants `UI_REGULAR=outfit`, `UI_BOLD=outfit_bold`.
- Font json (e.g. `font/outfit.json`): TTF provider at size 16, with references to `minecraft:include/default` + `unifont` as fallback. TTFs are in `assets/lyricsflow/font/ttf/` (there is also an `arabic.ttf`).
- **Vertical baseline shift** `yShift(id)`, as a fraction of size added to y:

| font | shift | font | shift | font | shift |
|---|---|---|---|---|---|
| alexbrush | .35 | grandhotel | .348 | parisienne | .337 |
| allura | .285 | greatvibes | .354 | quicksand | .37 |
| courgette | .379 | kaushan | .367 | sacramento | .325 |
| damion | .326 | lyricsflow_script | .367 | syne | .349 |
| dancing | .326 | marck | .315 | yellowtail | .354 |
| norican | .35 | outfit | .357 | yesteryear | .329 |
| outfit_bold | .36 | poppins | .378 | pacifico | .399 |

`minecraft` → 0. Unknown/custom → CustomFonts shift, else 0.35. (The shift compensates for MC's TTF provider placing glyphs so that the visual centre lands at `y+size/2`.)
- `isRtl(s)`: true if any char is in U+0590–U+08FF, U+FB1D–U+FDFF or U+FE70–U+FEFF. Used by the top-bar karaoke to mirror word order.

### 3.4 CustomFonts (user TTFs)
- Folder `config/lyricsflow/fonts/*.ttf` (with a README written there). Rebuilds a resource pack at `config/lyricsflow/fontpack/` that contains `assets/lyricsflow/font/custom_<slug>.json` + `font/custom/<id>.ttf`.
- `slug = lower(name) − ".ttf"`, with `[^a-z0-9]+→_` and outer `_` trimmed. id = `custom_<slug>`.
- Display name = AWT family, or full font name if it differs and is <40 chars.
- Shift metric: at 16 px, `capH = −bounds("H").y` and `xH = −bounds("x").y`. If both >1: `center = 7 − (capH+xH)/4` and `shift = (8 − center)/16`, clamped to 0.1..0.6. Default 0.35.
- Bedrock port: no resource-pack font pipeline. Load the TTFs directly with our own glyph atlas (stb_truetype / FreeType) and reuse this shift formula for vertical centring.

### 3.5 Other Gfx primitives (screen space)
Shape textures are in `textures/gui/shape/`: `round.png` (136², 9-slice 64), `ring.png` (136², slice 64), `soft.png` (128², slice 48), `dot.png`, `glow.png` (128² radial blob), `band.png` (64²; a soft-edged strip, u along the length, v across).
- `rect` = solid fill. `gradientV` = vanilla fillGradient. `gradientH` = up to 48 vertical slices (one every 4 px), each slice lerped at its centre.
- `round(x,y,w,h,r,col)`: 9-slice of `round.png`, corner `c = clamp(round(r·s), 1, min(W,H)/2)`. When `r<0.4` it is a plain rect.
- `roundOutline`: screen → 9-slice `ring.png` with `corner = min(max(max(r,3t), 16t), min(w,h)/2)`. World → 64-segment ellipse of BAND quads with half-thickness `max(1.2, 2.4t)/2`.
- `shadow(x,y,w,h,r,size,col)`: `e=1.6·size`, 9-slice of `soft.png` over `(x−e, y−e+0.4size, w+2e, h+2e)`, corner `max(r,size)+e`. `glow` is the same without the 0.4·size y offset.
- `circle` = `dot.png` quad of radius r. `blob` = `glow.png` quad of radius r (skipped if r≤0.5).
- `line` = rotated rect. `streak(x0,y0,x1,y1,t,col)` = 6 line segments with alpha (i+1)/6, so it fades in from tail (x0) to head (x1).
- `band(x0,y0,x1,y1,t,col)` = rotated `band.png` quad, length+2 px, thickness max(2px, t).

### 3.6 World-space mode (how sky FX are drawn)
`Gfx.beginWorld(pose, buffers)` switches every 2D primitive to emit world quads. A GUI point `(gx,gy)` is unprojected onto a sphere of radius `worldDepth` around the camera:
```
worldDepth (sky)  = clamp(renderDistanceChunks·16·0.5, 20, 96)
d = worldDepth·(1 − min(0.03, seq·3e−6))       // tiny per-quad depth step to avoid z-fighting
P = (F + a·R + b·U)·d   where a=(gx−W/2)/focal,  b=−(gy−H/2)/focal
```
Quads use fullbright light (0xF000F0) and no overlay. Winding is flipped when the signed area is negative, so they are never back-face culled. For motes and streaks, `worldDepth` is set per particle to its projected depth `P[2]`. In world mode `band()` uses a joined-segment mitre system: 6 pending segment slots; joins are emitted when the thickness ratio is in 0.7..1.43, with mitre ≤2 and colours lerped 0.5 at the join. Port note: on Bedrock it is simpler to build these as camera-relative triangle strips directly.

---

## 4. HUD, top bar and projection (`render/Hud.java`, `render/TopBar.java`, `render/Proj.java`)

### 4.1 Proj (camera projection) — exact
Updated once per frame with the GUI width/height (scaled px):
```
cam pos (cx,cy,cz), yaw, pitch
F = camera forward, U = camera up, R = −camera left
fov = fovFromRenderer (actual dynamic FOV incl. sprint/zoom) if 1..179, else options FOV
focal     = H/2 / tan(fov/2)
focalBase = H/2 / tan(optionsFov/2)
zoomComp  = (focalBase/focal)^0.85           // used by WL to counter FOV zoom

projectRel(dx,dy,dz):
  z = d·F;  if z < 0.05 → not visible
  x = d·R;  y = d·U;  k = focal/z
  screen = (W/2 + x·k,  H/2 − y·k);  out = [sx, sy, z, k]
project(world) = projectRel(world − cam)
unproject(gx,gy,depth) = (F + a·R + b·U)·depth,  a=(gx−W/2)/focal, b=−(gy−H/2)/focal
onScreen(p, margin): −margin < sx < W+margin  &&  −margin < sy < H+margin
dir(yaw,pitch) = (−sin y·cos p, −sin p, cos y·cos p)
```
`k` (px per block at that depth) is used everywhere as the size scale.

### 4.2 Hud frame logic (shared state)
Runs once per frame (guarded by `Anim.frameId`):
- Drain the MediaManager event queue → `TopBar.onEvent`, `WorldLyrics.onEvent`. SONG + songChime → chime.
- **Focus fade**: when the window regains focus → `mm.resync()` and wait 450 ms. While unfocused or waiting, `focusA=0`. Otherwise `focusA = approach(focusA, 1, 5)`.
- `intensity = approach(intensity, on?1:0, on?2.2:1.2)`, where `on = enabled && playing`.
- `skyIntensity`: the same, but `on = enabled && (playing || skyAlways)`.
- **beat**: set to 1.0 each time the current (line.index·1000 + wordIndex) changes, i.e. on every new sung word. Decays as `beat = max(0, beat − dt·3.2)`. This is the only "beat" signal; no audio analysis is involved.
- Render hooks (Fabric):
  - `START_MAIN` → `renderFarSky()` (sky layers + stars, identity pose).
  - After entities → `renderWorldSky()`: motes, streaks, then `Rain.renderGround`, `renderDrops`, `renderSnow`, `Petals`, `Embers`.
  - HUD before MISC_OVERLAYS → WL screen lyrics.
  - HUD before CHAT → TopBar.
  - When Chroma is active, world FX + world lyrics are drawn late (after the post-pass) using captured view matrices.

### 4.3 Anim (`util/Anim.java`) — all easing functions
```
dt = min(0.1, secondsSinceLastFrame)   (only updates if ≥1 ms elapsed; frameId++)
approach(cur, target, speed): k = 1 − exp(−speed·dt); v = cur + (target−cur)·k; snap to target if |target−v| < 5e−4
clamp01(t)
easeOutCubic(t)  = 1 − (1−t)^3
easeInCubic(t)   = t^3
easeInOut(t)     = t<.5 ? 4t^3 : 1 − (−2t+2)^3/2
easeOutBack(t)   = 1 + 2.70158·(t−1)^3 + 1.70158·(t−1)^2
lerp(a,b,t)      = a + (b−a)t
progress(t,a,b)  = b≤a ? (t≥b?1:0) : clamp01((t−a)/(b−a))
hash01(seed):  x = seed·0x9E3779B97F4A7C15 (−7046029254386353131); x ^= x>>>31;
               x *= 0xBF58476D1CE4E5B9 (−4658895280553007687); x ^= x>>>27;
               return (x>>>40)/2^24        // all 64-bit unsigned shifts, wrapping multiply
```
All inputs to easing functions are clamped to 0..1 first. `seconds()` = monotonic nanoTime/1e9.

### 4.4 Top bar (TopBar.java)
**Phases:** TITLE, LYRICS, PAUSED, RESUMED.
- Events: SONG→TITLE, PAUSE→PAUSED, RESUME→RESUMED.
- TITLE→LYRICS once phaseAge > 3.2 s and lyrics exist.
- RESUMED → LYRICS (or TITLE) after 1.3 s.
- PAUSED → RESUMED if the track is playing again.

**Visibility:**
- `want = track≠null`. If `hideWhenStopped` and PAUSED for >3 s → false. If `!showOnlyWhenPlaying` → always true.
- `vis = approach(vis, want, want?7:5)`. Nothing is drawn when vis<0.01.
- `dim = 0.6` if paused for >2 s with dimWhenIdle, else 1. Then `dim *= 0.25 + 0.75·focusA`.
- Final alpha `a = easeOutCubic(vis)·dim`.

**Geometry** (`k = topBarScale`):
```
h = 18k
ts = (minecraft ? 9 : 8.6)·k                       // text size
textY = (h − (mc ? ts·0.78 : ts))/2 + (mc ? 0.5k : −0.3k)
barsW = 14k; pad = 7k
maxText = min(240k, screenW·0.55)
targetW = max(110k, h + min(textW, maxText) + barsW + 2·pad)
width = approach(width, targetW, 8)                 // initial 150
x = clamp(topBarX·screenW − width/2, 2, screenW − width − 2)
y = clamp(topBarY·screenH − 10·(1−easeOutCubic(vis)), 1, screenH − h − 1)   // slides down 10px on show
```
**Draw order:**
1. Chips.
2. `shadow(r=h/2, size=5k, black×0.5a)`.
3. Pill `round(r=h/2)` filled with barBg.
4. Outline 0.5 in `#40FFFFFF`.
5. If beatGlow and beat>0.05: `glow(size 3k, accent × 0.35·a·beat)`.

**Left icon:** centre `(x + h/2 + 1k, y + h/2)`, `ir = 0.34h`.
- PAUSED: circle #E8E4F5 plus two pause bars #161321. Each bar is `0.28ir × 0.9ir`, at x offsets −0.42ir and +0.14ir.
- Lyrics with cover art ready: round cover of radius 1.15ir.
- Lyrics without cover: spinning "vinyl".
  - `spin += dt·2.4` while playing.
  - Outer circle `lerp(accent,white,.25)`, inner 0.72ir in #2A2340.
  - Dot r=0.16ir in #90FFFFFF at `0.45ir·(cos spin, sin spin)`.
  - Centre 0.22ir in accent.
- Otherwise: Spotify/YouTube logo (`textures/gui/logos/*.png`, 128²), popped in with `easeOutBack(min(1, age·3))`, size `ir·(0.6+0.4pop)·2.15`. YouTube is used if `sourceApp ≠ "Spotify"`.

**Text area:**
- `tx = x + h + 2k`, `tw = width − h − barsW − pad − 2k`, scissored to that area.
- Content key: `"L"+lineIndex` / `"S"+phase` / `"T"+title`. When the key changes: `swap=0`, and scroll resets for lyrics. Then `swap = approach(swap,1,9)`.
- Text slides up `(1−easeOutCubic(swap))·6k` px and fades with `easeOutCubic(swap)`.
- Status mode: `"Paused  ·  title"` in #E8E4F5.
- Title mode: title in white, then `"  ·  artist"` in #9A93B5, ellipsized.

**Karaoke (word highlight):**
- Line chosen = `lineAt(t)`, else the last started line if `t − last.end < 2500 ms`. `word = line.wordAt(t)` (−1 = none).
- Word x positions: cumulative `width(word) + width(" ")`. RTL: `xs[i] = total − xs[i] − ws[i]`.
- Scroll target:
  - `0`, or `total−tw` for RTL overflow.
  - If a word is active and `total>tw`: `clamp(xs[w] + ws[w]/2 − 0.55tw, 0, total−tw)`.
  - `scroll = approach(scroll, target, 6)`.
- Highlight pill:
  - `pad=2.2`; target `x = xs[w]−2.2`, `w = ws[w]+4.4`. Snaps on first use, then `approach(…, 16)` for both x and w.
  - Drawn as `round(ox+hlX, ty−1.3, hlW, ts+2.6, r=3)` in accent × 0.95a.
- Word colours: white for sung and current words, #B9B3D6 for upcoming words.

**Equalizer bars (right):** 4 bars, starting at `bx = x + width − barsW − pad + 3k`, spaced 3k apart, each 1.8k wide with radius 0.9k, vertically centred.
```
target_i = playing ? 0.25 + 0.35·|sin(time·(3.1 + 1.7i) + i)| + 0.45·beat·(i even ? 1 : 0.7) : 0.18
bar_i = approach(bar_i, min(1,target_i), 14);  height = max(1.5k, bar_i·h·0.5)
colour = lerp(theme.b, accent, i/3)
```
**Chips:** `ch = 0.82h`. Text is drawn with a black 0.55a drop shadow at +0.6/+0.7 px.
- Clock `HH:mm` sits left of the bar, at `x − (textW+10) − 4`, in #E8E4F5.
- Right side starts at `x+w+4`:
  - ping `"<ms>ms"`: colour #7CF0A8 if <80, #FFD36A if <180, else #FF7A7A.
  - fps `"<n> fps"` in #E8E4F5.

### 4.5 SCREEN mode (summary only; implemented in WorldLyrics)
- Anchor `(screenLyricsX·W, screenLyricsY·H)`, randomized ±0.26 x and ±0.12 y (see config table). `emPx = 15·lyricsScale`.
- Each word is drawn via `Gfx.neon`, rotated by the line tilt + per-word rotation, with a bob of `sin(t·1.6 + j·0.9 + seed)·0.04 em`.
- `globalA = pauseFade·focusA·textOpacity`.
- WorldLyrics computes `glow = glowBase(0.65 if glow) + 0.35 if sung + 0.5·beat` and passes it in, but it is unused (§3.1).

---

## 5. Sky system (`render/SkyFx.java`, `SkyPresets.java`, `SkyTint.java`, `Chroma.java`)

### 5.1 SkyFx (theme-driven, independent of preset)
`I = Hud.skyIntensity`. If `I<0.01`, motes and streaks are cleared.
**Sky visibility:** checked every 20 sky frames as `dimension.hasSkyLight()` (`comp_642`). `skyVisible = approach(skyVisible, target, 2)`. Sky layers and stars are multiplied by it.

**Stars** (sky pass, `cfg.stars`):
- 140 fixed stars from `java.util.Random(374)`, in order per star: yaw = `r·360`; pitch = `−(4 + r^0.7·84)`; size = `0.5 + r·r·1.3`; phase = `r·6.283`. To reproduce exactly, port Java's LCG Random (48-bit, mult 0x5DEECE66D, add 0xB, `nextFloat = next(24)/2^24`).
- Count = `140·clamp(starDensity)`. Each star is projected at `dir·100` and culled if not onScreen with a 4 px margin.
- `tw = 0.45 + 0.55·(0.5 + 0.5 sin(time·(0.8 + 0.4·phase) + phase))`. `size = s·(1 + 0.25·beat)`.
- Tint = starColor, or `lerp(white, theme.main, 0.3)`.
- Draw `circle(r = 0.6size, tint × I·tw·0.9)`. If s>1.25, also a halo `circle(r=1.6size, tint × I·tw·0.12)`.

**Motes** (world pass, `cfg.motes`):
- Pool size = `60·moteDensity`.
- Spawn:
  - At angle θ, distance `1.5 + 6.5r` around the camera.
  - y = `camY − 2.5 + r·(anywhere ? 7 : 3)`.
  - maxLife `5 + 6r` s, size `0.06 + 0.06r`, phase `r·6.28`, colour pick a/b 50 %.
- Per frame:
  - `life += dt`; `y += 0.35dt`.
  - `x += sin(0.9life + phase)·0.12dt`; `z += cos(0.7life + phase)·0.12dt`.
- Respawn when life > maxLife, horizontal dist² > 81, or |y − camY| > 5.
- Fade `lifeA = min(1, life/1.2)·min(1, (maxLife−life)/1.5)`. Radius `r = clamp(k·size, 1, 7)` px. `a = I·lifeA·(0.8 + 0.2beat)`.
- Three layers:
  - `blob(5r, col × 0.35a)`
  - `circle(1.6r, col × 0.45a)`
  - `circle(r, lerp(col, white, .6) × a)`

**Streaks** (world pass, `cfg.streaks`):
- `rate = (1.2 + 1.5beat)·streakDensity` per second, accumulated. Max 10 alive.
- Spawn:
  - yaw = camYaw ± 70° random. `far = (lyricsStyle==SKY)`, `K = far?6:1`.
  - Distance: far → 30..60 blocks, else 2..8 blocks, plus a random ±1.5K offset in x/z.
  - y = camY + (far ? 18..43 : 2..7).
  - Velocity: sideways (perpendicular to yaw) at 2.5..4.5 b/s, `vy = −(11..17)`.
  - Length 1.2..2.6.
  - If far: vx,vz ×6, vy ×3.6, len ×6.
- Life 1.6 s. Removed early if `y < camY − (far?40:6)`.
- Tail = head − v̂·len. Drawn if head or tail is onScreen with a 30 px margin.
- `fade = min(1, 5life)·min(1, 3(1.6−life))`. Thickness `clamp(k·0.035·(far?6:1), 0.5, 1.6)`.
- `Gfx.streak(tail→head, col × I·fade·0.8)`. Colour = streakColor or `lerp(white, theme.main, .25)`.

### 5.2 Sky presets (`SkyPresets.ALL`)
Each preset is `(id, top, bottom, layers…)`, and each layer is `l(kind, n, strength s, colours…)`. `col(l,i)` = `cols[i mod len]`. Layers are drawn in the order listed with `I = skyIntensity·skyVisible`. The sky pass maps (yaw,pitch) via `Proj.dir·100` → screen (culled with a 240 px margin). `A[2] = k·1.745` is "px per degree" (k at 100 blocks × 1.745 ≈ px/deg at 100 units).

| id | top / bottom (fallback) | layers in order: KIND(n, s, colours) |
|---|---|---|
| off | — | none (vanilla sky; `plainSkyColor` may apply) |
| custom | built from config (§5.3) | GRAD2(1,1, hor,top); [STARS 140 .9 #FFF,#DDE6FF]; WISPS2(16,1, a1@.42, a2@.42, mix@.35); [BANDS 4 1.1 a1,a2]; [MOONGLOW 30 1.3 #E8EEFF,#FFF]; [SHOOT 1 .8 #FFF] |
| watercolor | #BFD8FF / #FFD0E8 | GRAD2(1,1 #FFD0E8,#B8D4FF); FLOW(9,1.6 #FF6FB5,#5EC8FF,#FFC94F,#A67CFF,#4FE3B0,#FF8F6B); STROKES(14,1 #FF5FA8,#4FB8FF,#FFC23F,#9A6CFF,#3FD8A0,#FF7F5A); VEINS(6,.8 #FF6FB5,#FFE0F0); VEINS(6,.8 #5EC8FF,#E0F6FF) |
| black_sun | #030109 / #100C1A | GRAD2(1,1 #100C1A,#030109); STARS(120,.55 #B8B4C8,#9A96AA); SMOKE(5,1); CONSTELLATION(1,1 #FFF); SUNHOLE(1,1 #FF6A1A) |
| pink_aurora | #100D14 / #241A30 | GRAD2(40,1 #2A1E36,#100D14); RING(14 = aurora_front, paired with aurora_back, .3); STARS(50,.45 #D8D0E8); SHOOT(1,.8) **+ Petals FX** |
| retro_sunset | #260B3D / #8A3C9E | GRAD2(38,1 #8A3C9E,#260C3D); GRADIENT(synth_grad); STARS(35,.5 #D8C8F0); PHOTO_SUN(1,1 #F04FE0,#FF8AE8); CONSTELLATION(2 → STAR_PIC_M); SHOOT(1,.8); CLOUDS(7,1) |
| pulsar_beam | #050A18 / #0B1A34 | GRAD2(1,1 #0B1A34,#050A18); STARS(320,.85 #FFF,#C8DCFF); MILKY(1); WISPS(6,1); BEAM(1,1 #46D2FF,#9A7CF0); CONSTELLATION(3 → PULSAR_PICS) |
| ember_veins | #180A03 / #3D1203 | GRAD2(1,1 #4A1604,#140702); STARS(60,.5 #FFD8B0); CLOUDSW(12,1.5 #90401408,#80301006,#90501A0A); VEINS(18,1 #FF8A1E,#FFD27A) |
| green_fall | #010302 / #04120A | GRAD2(1,1 #06140C,#010201); FALL(40,1 #2EC870,#5AE896,#1A8A48) |
| northern_lights | #233345 / #174240 | GRAD2(34,1 #174240,#233345); RING(0,1.2); STARS(120,.8 #FFF,#CFFFF0) |
| violet_aurora | #171325 / #4B2F6F | GRAD2(34 #4B2F6F,#171325); RING(1,1.1); STARS(100,.8 #FFF,#F0D8FF) |
| galaxy | #191826 / #1F1F3A | GRAD2(35 #1F1F3A,#191826); RING(2,.4); STARS(170,.9 #FFF,#E8DCFF); MOONGLOW(10,2.6 #E8D8FF,#FFF4FF) |
| rain | #2A3038 / #5A6470 | GRAD2(1 #5A6470,#2A3038); RAIN(26,.45 #B8C6D6,#8FA2B8) **+ forces Rain FX kind 1** |
| neon_city | #100E1A / #3D1849 | GRAD2(34 #3D1849,#100E1A); RING(3,.4); STARS(50,.7 #FFF) |
| golden_hour | #6F5054 / #D3976B | GRAD2(30 #D3976B,#6F5054); RING(4,.35) |
| cotton_sky | #7692B7 / #ABA2B8 | GRAD2(35 #ABA2B8,#7692B7); RING(5,.5) |
| cherry_blossom | #714F73 / #D19DC3 | GRAD2(29 #D19DC3,#714F73); RING(6,.35); STARS(25,.5) |
| frost_night | #354D65 / #5A87AF | GRAD2(34 #5A87AF,#354D65); RING(7,1); STARS(110,.8); MOONGLOW(10,1.4 #DDEEFF,#FFF) |
| midnight | #13161D / #1C2236 | GRAD2(32 #1C2236,#13161D); RING(8,.3); STARS(180,.9 #FFF,#CFDCFF); MOONGLOW(6,1.5 #E8EEFF,#FFF) |
| deep_sea | #17363A / #0F3841 | GRAD2(37 #0F3841,#17363A); RING(9,1.5); SHAFTS(14,1 #BFFFF4,#7FE8E0); STARS(40,.4 #B0FFF0) |
| blood_moon | #210C0F / #4B1014 | GRAD2(34 #4B1014,#210C0F); RING(10,.7); STARS(60,.7 #FFD0D0); MOONGLOW(5,2 #FF6060,#FFC8C0) |
| thunderstorm | #292F37 / #1E242C | GRAD2(34 #1E242C,#292F37); RING(11,.7); LIGHTNING(2,1 #FFF,#C8D8FF) |

RING colours are all #FFFFFF. GRAD2 colours are `(horizon, zenith)`. For GRAD2 the `n` field is the "full" pitch in degrees (see below). The UI shows presets 6 per page.

**Ring textures** (`textures/sky/ring_<name>.png`):

| idx | name | w×h | span° |
|---|---|---|---|
| 0 | northern_lights | 2048×620 | 42 |
| 1 | violet_aurora | 2048×676 | 42 |
| 2 | galaxy | 2048×744 | 44 |
| 3 | neon_city | 2048×596 | 42 |
| 4 | golden_hour | 2048×536 | 38 |
| 5 | cotton_sky | 2048×680 | 44 |
| 6 | cherry_blossom | 2048×492 | 36 |
| 7 | frost_night | 2048×644 | 42 |
| 8 | midnight | 2048×604 | 40 |
| 9 | deep_sea | 2048×736 | 46 |
| 10 | blood_moon | 2048×636 | 42 |
| 11 | thunderstorm | 2048×624 | 42 |
| 12 | custom_aurora | 2048×480 | 34 |
| 13 | ember_veins | 512×632 | 70 |
| 14 | aurora_front | 2048×512 | 50 |
| 15 | aurora_back | 2048×512 | 50 |

Index 14 is paired with 15 for its second half.

Other sky textures: `blackhole.png` (1024×358), `aurora_front/back.png` (2048×512, PANORAMA), `synth_grad.png`, `synth_sun.png` (512²), `cloud0-2.png` (512×256), `cloudw0-2.png` (512×256), `cloudwisp0-2.png` (512×256), `wisp0-1.png` (512×256), `milkyway.png`, `caustic0-1.png` (512²), `smoke.png` (512×236), `white.png`.

### 5.3 Layer kinds actually used — how each is drawn
Notation:
- `h(i)` = `Anim.hash01(i·7919 + 17)` (deterministic per index).
- `time` = seconds.
- `A[2]` = px/deg scale.
- `sprite(tex, yaw, pitch, wDeg, col)` = a camera-facing quad on the sky sphere. Direction D=dir(yaw,pitch), R=dir(yaw+90,0), U=dir(yaw,pitch−90). `hx = tan(min(160,wDeg)/2)`, `hy = hx·th/tw`, `d = depth/√(1+hx²+hy²)`. Corners `(D ± R·hx ± U·hy)·d`.

- **GRAD2 (sky dome gradient)** (sky pass only):
  - Built from 24 yaw segments × rows at pitches `[60,30,14,4,0,−6,−14,−24,−36,−50,−66,−80,−89.5]`, at radius `0.97·depth`, using `white.png`.
  - Colour at a pitch: `t = clamp(−pitch/full)`, raised to the power 1 (if full<89) or 0.55 (if full=90). Then `lerp(hor, top, t)`, with alpha `I·s`.
  - `full = n>1 ? n : 90`. The gradient therefore reaches the zenith colour at pitch −n. Below the horizon it stays the horizon colour.
- **STARS:**
  - Count = n (sky). The preview uses max(6, n/6).
  - Star i: yaw `h(i+300)·360`; pitch `−6 − h(i+611)^0.8·82`.
  - `big = h(i+977)`. `tw = .55 + .45 sin(time·(.8 + 2.2h(i+41)) + i)`. `r = max(.55, A2·(.05 + .07big))`.
  - `circle(col_i × I·s·(.45 + .55tw))`. If big>.88, also `blob(5r, × I·s·.35tw)`.
- **SHOOT:**
  - n·3 meteors. Meteor i has period `3.2 + 2.5h(i+3)`. Phase `t0 = time/period + h(i+11)`; `cycle = floor`, `ph = frac`. Visible only while ph ≤ .35, with `u = ph/.35`.
  - Start yaw `h(cycle·13+i)·360`, pitch `−(40 + 25h(cycle·7+i))`, direction ±1 from `h(cycle + 5i)`.
  - Head = `(yaw + dir·30u, pitch + 14u)`. Tail = head − `(dir·6, 2.8)`.
  - `fade = sin(πu)`. Drawn as `streak(thick max(.8, .25A2), col × I·fade·s)` plus a white head dot.
- **RAIN (sky streaks):**
  - n·3 drops, speed `22 + 18h(i+5)` deg/s.
  - yaw = `(h(i)·360 + floor(time·speed/60 + h(i+9))·37) mod 360`.
  - `ph = frac(time·speed/60 + h(i+9))`. `pitch = −72 + 74ph`. Tail is 7·s degrees above.
  - `fade = sin(π·ph)`. `streak(thick max(.6, .35A2), col_i × I·.95·fade)`.
- **LIGHTNING:**
  - n·3 bolts, period `2.2 + 2h(i+21)`, phase offset h(i+4). Visible while ph ≤ .14, skipping frames where `(int)(ph·60) % 3 == 2` (flicker).
  - Start yaw `h(cycle·31+i)·360`, pitch −62.
  - 7 segments, each going down 8° with yaw jitter `(h(cycle·101 + k + 7i) − .5)·9`.
  - `a = I·(1 − ph/.14)·s`. Per segment: a wide line `max(2, .8A2)` in col_i × .4a, and a core line `max(.8, .25A2)` in white × a.
- **RING (aurora ring texture band around the horizon):** sky pass only.
  - Two copies cover 180° each, starting at yaw 90 and 270. Each copy spans 180 + 2·15° of overlap, with 84 columns × 16 rows (plus a row −1 extended to pitch +40, below the horizon).
  - Row pitch = `3 − fv·(span+3)`, where `fv = row/16`. Radius `0.96·depth`; the second copy uses ×0.985.
  - UV:
    - `u = (yaw − start)/cspan`, mirrored for the paired second copy, then mapped as `0.012 + 0.976u`.
    - `v = 1 − fv + amp·0.35·sin(3·yawRad − 0.35t)`, where `amp = s·0.01`. Mapped as `0.98v + 0.01` and clamped.
  - Vertex colour = `lerp(bot, top, fv)` × I × fade.
    - Top 20 % fades: `fade = (1−fv)/0.2` when fv>0.8.
    - Side edges fade over the 15° overlap: `edge = min(yaw−start, start+cspan−yaw)/30`.
- **MOONGLOW:**
  - Position (yaw 180, pitch −n). The preview uses pitch −n−20.
  - `r = 1.4·A2·s`.
  - Blobs: `9r @ .18I`, `4r @ .35I`, `2r @ .55I` in col0.
  - Disc: `circle(r)` in col1 @ .95I.
- **FLOW (watercolour blobs):**
  - n·4 blobs, direction ±1 from h(i+11), speed `2 + 4h(i+12)` deg/s.
  - yaw = `(h(i)·360 + dir·time·speed + 10 sin(.3time + i)) mod 360`.
  - pitch = `−(8 + 55h(i+6)) + 8 sin(time·(.15 + .2h(i+3)) + 1.7i)`.
  - Radius `(20 + 18h(i+8))·breathe·A2`, where `breathe = .8 + .25 sin(time·(.3 + .4h(i+4)) + i)`.
  - Colour `lerp(col_i, col_{i+1}, .5 + .5 sin(.2time + .9i))`, alpha `I·.38s·(.75 + .25 sin(.5time + 2i))`.
- **STROKES (brush strokes)** (n strokes, 96 segments in the sky / 24 in the preview):
  - Base yaw `h(i+1500)·360 + time·(.4 + .6h(i+1501))·(±1 alternating)`.
  - Pitch `−(6 + 62h(i+1502))`.
  - Length `90 + 110h(i+1503)` degrees, tilt `(h(i+1504) − .5)·.25`, thickness `6 + 9h(i+1505)`.
  - Along f: `yaw = y0 + f·len`, `pitch = p0 + f·len·tilt + 4 sin(5f + .25time + 1.3i)`.
  - Envelope `e = sin(πf)^0.6`, width wave `.8 + .2 sin(9f + i)`.
  - Two variable-width band paths:
    - width `thick·wv·A2` @ `I·s·.42e`
    - width `.45·thick·wv·A2` @ `.35e`
- **VEINS:**
  - n veins. yaw0 = `i·360/n + 25h(i+1000)`, pitch0 = `−(6 + 58h(i+1001))`, length `50 + 60h(i+1002)` degrees, 28 segments.
  - pitch(f) = `p0 + 7 sin(6f + .35time + 1.7i) + 3 sin(15f − .6time + i)`.
  - `e = sin(πf)`, `fl = .75 + .25 sin(1.5time + i + 4f)`.
  - Outer band `2.6A2` in col0 @ `.22·e·fl·I·s`. Inner band `max(.6, .45A2)` in col1 @ `.75·e·fl·I·s`.
- **FALL (green_fall):**
  - n trails. yaw `h(i+1100)·360`, speed `5 + 7h(i+1101)`, length `(10 + 22h(i+1102))·0.6`.
  - `ph = frac(time·speed/95 + h(i+1103))`. `head = −50 + 56ph`. `fade = sin(π·ph)`.
  - 2-segment band of width `2.6A2` from head−len to head, alpha `I·s·.4·fade·(.35 + .65f)`.
- **SHAFTS (light shafts from above):**
  - n shafts, 24 segments. baseYaw `h(i+2100)·360`.
  - sway `9 sin(time·(.18 + .15h(i+2101)) + 1.7i)`, lean `(h(i+2102) − .5)·24`, width `3.5 + 5h(i+2103)`.
  - pulse `.55 + .45 sin(time·(.35 + .3h(i+2104)) + 2.3i)`. Colour `lerp(col0, col1, h(i+2105))`.
  - Along f: `pitch = −78 + 66f`, `yaw = base + (sway+lean)·f`, width `width·(.6 + .9f)·A2`.
  - Alpha `I·s·.16·pulse·sin(π·min(1, 1.15f))`.
- **SUNHOLE (black_sun):**
  - At (160, −24) (preview 25, −30). A pulse blob of radius `2.45·A2·s·5` in col0 @ `.12I·(.5 + .5 sin .9t)`.
  - Then `sprite(blackhole.png 1024×358, wDeg = 2.45·s·1024/58.5)` in white @ I.
- **CONSTELLATION:**
  - n=1: STAR_PIC at base (160, −24) — the sky position. Offsets (dyaw, dpitch): (−4.79,8.19), (−3.12,12.63), (.76,11.17), (2.57,16.52), (7.5,14.92). Applied as `(by + dx, bp − dy)`.
  - n=2: STAR_PIC_M at (180, −6.93). Offsets (−11.72,19.8), (−8.12,23.55), (−4.52,19.36), (−.89,23.31), (2.38,18.92).
  - n=3: PULSAR_PICS, absolute (yaw,pitch) polylines {(107.27,−50.03), (113.37,−50.88), (115.08,−47.75), (110.03,−46.58), (107.27,−50.03)} and {(157.39,−37.74), (160.38,−36), (162.77,−32.66), (167.24,−33.85), (165.86,−37.3)}.
  - Drawn as lines 0.45 px @ `I·s·.75` plus star dots `max(.7, .09A2)` that twinkle at `.75 + .25 sin(…)`.
- **SMOKE:**
  - n sprites (smoke.png 512×236), direction alternating.
  - yaw `h(i+800)·360 ± time·(.5 + .7h(i+801))`, pitch `−(10 + 45h(i+802)) + 2 sin(.05t + i)`.
  - Width `55 + 30h(i+804)` degrees, alpha `I·s·(.85 + .15 sin(.15t + i))`.
- **CLOUDS:**
  - n sprites cycling cloud0..2.
  - yaw `h(i+900)·360 + dir·time·(.35 + .5h(i+902))`, pitch `−(9 + 24h(i+903)) + 1.5 sin(.06t + i)`.
  - Width `26 + 22h(i+904)`, alpha `I·s·(.9 + .1 sin(.2t + i))`.
- **CLOUDSW** (tinted clouds, cloudw0..2):
  - yaw `h(i+1400)·360 + dir·time·(.25 + .35h(i+1403))`, pitch `−(5 + 32h(i+1401)) + 1.2 sin(.06t + i)`.
  - Width `(34 + 26h(i+1404))·s`. Colour col_i (with its alpha) × I.
- **WISPS:**
  - n sprites wisp0/1.
  - yaw `230 + 200h(i+950) ± time·(.3 + .4h(i+951))`, pitch `−(12 + 45h(i+952))`, width `40 + 30h(i+953)`, white × I·s.
- **WISPS2** (cloudwisp0..2, custom preset):
  - yaw `h(i+1600)·360 + dir·time·(.15 + .3h(i+1603))`, pitch `−(4 + h(i+1601)^1.4·52) + sin(.05t + i)`.
  - Width `35 + 55h(i+1604)`, colour col_i × `I·s·(.75 + .25 sin(.1t + 1.7i))`.
- **BANDS (procedural aurora, custom preset):**
  - For band b < n: base pitch `−(24 + 12b)`, thickness `5.5 − 0.8b` degrees, alpha `I·min(.55, (.3 − .04b)·s)`.
  - Sampled every 8° of yaw over ±96° around the camera yaw.
  - `pitch = base + 7 sin(2·yawRad + .12t + 1.7b) + 2.5 sin(5·yawRad − .07t + b)`.
  - Drawn as a joined band path in col_b.
- **GRADIENT (retro_sunset synthwave band):** sky only. 24 yaw segments × pitch rows [1.45, −6, −14, −24, −38] at `.95·depth`. Texture `synth_grad.png`, u 0.2..0.8, `v = (pitch − (−38))/(1.45 − (−38))`, white × I·s.
- **PHOTO_SUN:**
  - At (180, −6.93). `r = 7.35·A2·s`, pulse `.9 + .1 sin(.7t)`.
  - Blobs: `5.5r` col0 @ `.3I·pulse`, `3r` col0 @ `.45`, `1.8r` col1 @ `.45`.
  - Then a `synth_sun.png` sprite of width `7.35·s·2.0905` degrees.
  - Plus a horizon line: 48 segments at pitch 0.3, band `1.6A2` in #E8B8F2 @ .55I.
- **MILKY:**
  - A 16-segment textured strip along a great-circle arc. `axis` = MILKY_AXIS (−.39034, .62187, −.6789) normalized. n=2 → GALAXY_AXIS (.75, .35, .56); n=3 → MIDNIGHT_AXIS (−.3, .55, .78).
  - `arc(a)`: `u = norm(axis × up)`, `v = u × axis`, `θ = π(.05 + .9a)`, point = `norm(u cosθ + v sinθ)`.
  - Half-width offset ±0.22 along `q × axis`. Radius `.9·depth`.
  - UV u = a0..a1, colour col0 × `I·s·(.9 + .1 sin .2t)`.
- **BEAM (pulsar):**
  - A great circle through `C = (−.39034, .62187, −.6789)` and `T = (.92052, .27674, −.27577)`, with `P(θ) = C cosθ + T sinθ` for θ = −108°..108° step 3°.
  - Converted to yaw/pitch as `yaw = deg(atan2(−x, z))`, `pitch = −deg(asin y)`.
  - Falloff `fall = (.45 + .55 e^(−|θ|/12))·min(1, (108 − |θ|)/25)`, wave `.85 + .15 sin(.3|θ| − 3t)`.
  - Three bands per segment:
    - `7.5A2` col0 @ .22
    - `3.4A2` col0 @ .42
    - `.9A2` #BFF4FF @ .35
    - each × `I·s·fall·wave`
  - Core at (150.1, −38.45):
    - Blobs `9A2` col0 @ .35pulse, `4A2` #BFF4FF @ .6pulse, `1.6A2` white @ .95 (pulse `.85 + .15 sin 2.4t`).
    - Two ring outlines of radius `6.4A2` in col1.
- Unused in any preset (defined but not referenced): BEAMS, BLOBS, SUN, MOON, ECLIPSE, PULSAR, EMBERS, SNOW, RINGS, CURTAIN, GALAXY, BLACKHOLE, PANORAMA, SUNRAYS, CAUSTICS. These can be skipped.

### 5.4 SkyTint (vanilla sky/fog/cloud colour override)
Hooked into the client environment-attribute colours (sky, fog, sunrise, cloud, star brightness).
```
plain()  = (plainSkyColor≠0 && preset=="off") ? plainSkyColor|FF000000 : 0
amount() = !enabled ? 0
         : plain≠0 ? skyIntensity
         : skyColor≠0 ? skyIntensity·0.8
         : coversSky(preset) (has GRAD2) ? skyIntensity
         : preset.top==0 ? 0 : skyIntensity·0.6
targetTop    = plain ?: (skyColor ? darken(skyColor,0.45) : GRAD2.col[1] ?: preset.top)
targetBottom = plain ?: (skyColor ? skyColor : GRAD2.col[0] ?: preset.bottom)
sky(c)     = lerp(c, targetTop, a)        (keep the original alpha)
fog(c)     = lerp(c, targetBottom, a)
sunrise(c) = alpha·max(0, 1 − 1.6a), rgb = lerp(c, brighten(targetBottom,.15), a)
cloud(c)   = lerp(c, lerp(white, targetBottom, .55), 0.7a)
stars(v)   = (enabled && stars) ? max(v, skyIntensity·0.75·max(0.3, starDensity)) : v
```
Bedrock: map these onto our sky/fog colour hooks (Renderer Foundation / Bedrock Forge environment overrides).

### 5.5 Chroma (post-process "colour bubble") — optional
Full-screen pass `shaders/post/chroma.fsh` (GLSL 330). For each pixel, it reconstructs the world position from depth; the sky (depth == 1) is left untouched. It snaps the position to a 1/16-block cell and measures the horizontal distance to the player. Pixels beyond `radius + lumps·1.6 + crumbs·0.45` are greyed:
- `lumps` = value noise(world/5) − .5
- `crumbs` = hash(floor(world/2)) − .5
- grey = `Lift + pow(luma, gamma)·Tint`, with luma weights (.299, .587, .114)

Radius animation:
- On enable: radius 70 → 6 with `r += (6−r)(1−e^(−2.2dt))`.
- On disable: radius → 70 with rate 2.5. Strength drops to 0 once r>68.

UBO: InvViewProj, CamFract, PlayerRel (w = radius), Params (.5, gamma, strength, bx mod 256), Tint (rgb, by mod 256), Lift (rgb, bz mod 256).

LOOK table `[tintR, tintG, tintB, liftR, liftG, liftB, gamma]`:

| idx | name | tint R | tint G | tint B | lift R | lift G | lift B | gamma |
|---|---|---|---|---|---|---|---|---|
| 0 | lilac | .92 | .885 | .92 | .04 | .035 | .1 | .8 |
| 1 | black | .28 | .28 | .31 | 0 | 0 | 0 | 1 |
| 2 | grey | 1 | 1 | 1 | 0 | 0 | 0 | 1 |
| 3 | purple | .85 | .62 | 1.15 | 0 | 0 | 0 | 1 |
| 4 | pink | 1.15 | .66 | .95 | 0 | 0 | 0 | 1 |
| 5 | blue | .62 | .82 | 1.18 | 0 | 0 | 0 | 1 |
| 6 | red | 1.15 | .55 | .55 | 0 | 0 | 0 | 1 |
| 7 | gold | 1.15 | .95 | .55 | 0 | 0 | 0 | 1 |

While Chroma is active, sky FX and world lyrics are re-drawn after the post pass so they stay coloured. On failure it disables itself for the session.

---

## 6. Weather-reactive FX (`render/Rain.java`, `Embers.java`, `Petals.java`, mixins)

**Important: real weather is never detected.** These FX are user-forced effects. The mixins then push the forced state *into* vanilla, so vanilla rain/snow and sky darkening also appear.
- `Rain.kind()`:
  - 0 if disabled.
  - 1 (rain) if `skyPreset=="rain"` or `weatherRain`.
  - Else 2 (snow) if `weatherSnow`.
  - Else 0.
- `Rain.tick()` runs every client tick (20 Hz). `target = selected ? skyIntensity : 0`. `strength += clamp(target − strength, −0.01, +0.012)`, snapped to 0 below 0.001. That is ~4.2 s to fade in and 5 s to fade out.
- `falling()` returns the last non-zero kind while strength>0, so FX can fade out.
- **LevelRainMixin** (`Level.getRainLevel`, `getThunderLevel`, client level only):
  - `rainLevel = max(vanilla, strength)`.
  - `thunderLevel = max(vanilla, 0.6·strength)` when the kind is rain.
- **WeatherKindMixin** (`WeatherEffectRenderer.getPrecipitationAt`): forces RAIN (kind 1) or SNOW (kind 2) for every position, so vanilla draws rain or snow even in deserts.
- Bedrock port: hook the client-side rain/lightning level getters (`Weather::getRainLevel` / `getLightningLevel` on the client Dimension or Level) and the biome precipitation query in the same way. Alternatively skip the vanilla override and rely only on our particles.

### 6.1 Rain (kind 1), only when `strength ≥ 0.02`
**Ground wetness** (`renderGround`): scans a disk of radius 30 around the camera block. For each column:
- `top = heightmap(MOTION_BLOCKING)`. Skipped if `|top − camY| > 10`.
- The block below must have no fluid, must not be leaves, and must have a sturdy top face.
- `fade = s·min(1, (30 − dist)/12)`. Light = level light at the top block.

Two passes:
- Pass 0 — a full 1×1 wet darkening quad at y+0.03, colour #05080E @ 0.3·fade (dot.png centre UV).
- Pass 1 — puddles, only where `hash(x,z) ≤ 0.26`:
  - Radius `r = .7 + .8·hash(x+7, z−3)`. Offset `(hash(x−5,z+9) − .5)·.6` and `(hash(x+11,z+2) − .5)·.6`.
  - glow.png quad at y+.045 in #0A121C @ .38fade, and a 0.8r quad at y+.06 in #8FA2B8 @ .16fade.
- Ripples on puddles within radius 14:
  - 3 rings per puddle. `ph = time·(.9 + .6·hash(x+k, 3z)) + hash(5x+k, z)`, `t = frac(ph)`, `cycle = floor(ph)`.
  - Random offset within r (hashed with cycle). Radius `.06 + .32t`.
  - ring.png at y+.075 in #DCE8F4 @ `(1−t)·.55s`.

`hash(x,z)`:
```
h = x·73428767 ^ z·912931 ^ 25214903917   (64-bit)
h ^= h>>>13
h *= 0x9E3779B97F4A7C15
h ^= h>>>29
return (h & 0xFFFFFF)/2^24
```

**Drops** (`renderDrops`):
- 900 slots, active `n = 900·min(1, s)`, stored camera-relative.
- Spawn:
  - Angle θ, radius `0.8 + √r·16`.
  - y = `anywhere ? −6..12 : 8..13`.
  - Fall speed `13..18` b/s, length `0.5..0.95`.
- Each frame:
  - Subtract camera movement. If the camera moved >20 in any axis, respawn everything.
  - Wrap x/z into ±17 (period 34). If y>14 then y −= 20.
  - `y −= v·dt`; `x += wind·dt` with `wind = 1.6`.
- When the world y drops below the ground heightmap, or y < −8:
  - Respawn the drop at the top.
  - If it hit the ground (ground > camY − 8, and fewer than 400 splashes exist), add a splash.
- Geometry: a vertical quad facing the camera horizontally.
  - Width `w = 0.012 + 0.004·(i%3)`. Top offset by `lean = wind/v·len` in −x.
  - Colour #C8D6EA @ `0.42·s·min(1, horizDist/1.5)`. Lit by camera-block light.
- Splashes live 0.35 s. Radius `.04 + .16t`. ring.png flat at ground+.02 in #E0ECF8 @ `(1−t)·.6s`.

### 6.2 Snow (kind 2)
- 700 flakes, `n = 700·min(1, s)`, using the same camera-relative wrap (±17, with y>13 → y −= 19).
- Spawn:
  - Radius `√r·17`.
  - y = `anywhere ? −6..11 : 9..12`.
  - Fall speed `0.9..1.7`, size `0.035..0.07`, phase 0..6.28.
- Motion:
  - `y −= v·dt`
  - `x += sin(.8t + p)·.35dt + .25dt`
  - `z += cos(.6t + 1.3p)·.35dt`
- Respawn below the ground heightmap or at y < −7.
- Drawn as a camera-facing billboard (dot.png, double-sided), skipped within 1.2 blocks of the camera. White @ `.95·s·min(1, (near − 1.2)/1.5)`.

### 6.3 Embers (`weatherEmbers`)
- `vis = approach(vis, on ? skyIntensity : 0, 1.5)`. Cleared below 0.02.
- 140 particles in a 20×20 area around the camera (world-space absolute).
- Spawn:
  - y = camY + (`anywhere ? −3..9 : −4..−2`).
  - rise `.4..1.0` b/s, size `0.09..0.18`, phase, sway `.5..1.3`, heat 0..1.
- Motion:
  - `y += rise·dt`
  - `x += sin(t·sway + phase)·.4dt`
  - `z += cos(.7·t·sway + phase)·.4dt`
- Respawn if `y > camY + 9` or |dx| or |dz| > 14.
- Colour: R=255, G=`110 + 120·heat`, B=40. Alpha `vis·flick·min(1, (14 − dist)/3)`, with `flick = .75 + .25 sin(7t + 3phase)`.
- Billboard size `3·size` with glow.png, render type `method_76014` (the same one Gfx world/sky quads use; Rain/Petals use `method_76000`; the blend mode was not verified from the decompile, so treat it as translucent emissive), fullbright.

### 6.4 Petals (only when sky preset = `pink_aurora`)
- `vis` same as embers. 70 particles in a 24×24 area.
- Spawn y = camY + (`anywhere ? −4..10 : 7..11`).
- fall `.5..1`, size `0.09..0.16`, spin rates X `.8..2.4`, Y `.5..1.7`, Z `.4..1.4`, sway `.4..1`.
- Motion:
  - `y −= fall·dt`
  - `x += sin(t·sway + ph)·.5dt + .15dt`
  - `z += cos(.8·t·sway + ph)·.4dt`
- Respawn if below camY − 5 or |dx| or |dz| > 14.
- Rotation: quaternion `rotationXYZ(t·spinX + ph, t·spinY + 2ph, t·spinZ)`. Quad half-axes `a = q·(size, 0, 0)` and `b = q·(0, 0, 1.2·size)`. Texture `textures/fx/petal.png`, white.
- Alpha `255·vis·min(1, (14 − dist)/3)·min(1, (near − 2.5)/1.5)`. Hidden within 2.5 blocks. Fullbright.

---

## 7. Media backend (Windows SMTC via raw COM/WinRT) — `media/SmtcBackend.java`, `Com.java`, `MediaBackend.java`, `AppVolume.java`

### 7.1 MediaBackend interface
`Track poll(Source)`, `byte[] cover()`, `playPause()`, `next()`, `previous()`, `seek(ms)` (default no-op), `String statusKey()`, `boolean available()`, `close()`.
Polling (in MediaManager, for reference): a background thread `LyricsFlow-Media` polls every **250 ms**, or **120 ms** once after a command (`pollSoon`).

### 7.2 Com helper (JNA → C++ is direct)
- `init()`: loads `combase` and `shcore`, then `RoInitialize(RO_INIT_MULTITHREADED=1)`.
- `call(obj, idx, args)`: vtable call (stdcall, convention 63). `outPtr`/`outInt`/`outLong` append an out-param and return null / INT_MIN / LONG_MIN when `hr<0`.
- HSTRING: `WindowsCreateString`, `WindowsGetStringRawBuffer`, `WindowsDeleteString`.
- `activationFactory(name, iid)` → `RoGetActivationFactory`.
- `await(op, timeout)`:
  - QI to `IAsyncInfo {00000036-0000-0000-C000-000000000046}`.
  - Poll `get_Status` (vtbl 7) every 2 ms until it is non-zero or the timeout passes.
  - If Completed (1), call `GetResults` (vtbl 8 on the op). Releases both.
- `awaitAction`: same, 1500 ms timeout, no results.
- `readRandomAccessStream`: `CreateStreamOverRandomAccessStream(ras, IID_IStream {0000000C-0000-0000-C000-000000000046})`, then `IStream::Read` (vtbl 3) in 64 KiB chunks, capped at 8 MiB.
- In C++ just use C++/WinRT: `GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get()`, etc.

### 7.3 SMTC calls (vtable index → WinRT member)
- Factory: runtime class `Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager`, statics IID `2050c4ee-11a0-57de-aed7-c97c70338245`.
  - `[6] RequestAsync` → await 5000 ms → manager.
- Manager: `[7] GetSessions` → IVectorView. Then `[7] get_Size` and `[6] GetAt(i)`.
- Session:
  - `[6] get_SourceAppUserModelId` (AUMID string)
  - `[7] TryGetMediaPropertiesAsync` (await 1500 ms)
  - `[8] GetTimelineProperties`
  - `[9] GetPlaybackInfo`
  - `[16] TrySkipNextAsync`
  - `[17] TrySkipPreviousAsync`
  - `[20] TryTogglePlayPauseAsync`
  - `[24] TryChangePlaybackPositionAsync(ms·10000)`
- PlaybackInfo: `[7] get_PlaybackStatus`, where **4 = Playing** (5 = Paused).
- MediaProperties:
  - `[6] Title`
  - `[8] AlbumArtist` (fallback if Artist is empty)
  - `[9] Artist`
  - `[10] AlbumTitle`
  - `[15] Thumbnail` → `[6] OpenReadAsync` (await 3000 ms) → bytes (max 8 MiB)
- Timeline (TimeSpan/DateTime are in 100 ns ticks):
  - `[6] StartTime`, `[7] EndTime`, `[10] Position`, `[11] LastUpdatedTime`.
  - `duration = (end − max(0,start))/10000 ms` (if end>0).
  - `position = pos/10000`, kept raw as `rawMs`.
  - If playing and `LastUpdatedTime>0`: `nowFT = (currentTimeMillis + 11644473600000)·10000`. If `0 < (nowFT − updated)/10000 < 6 h`, add that delta to the position (extrapolation).

**Session selection:**
- Iterate all sessions and keep those matching the source filter.
- Prefer the first *Playing* (status 4) session. Otherwise keep the first match.

**Source filter** (case-insensitive substring on the AUMID):
- SPOTIFY: contains `"spotify"`.
- YOUTUBE: isBrowser(aumid) **or** contains `"youtube"`.
  - isBrowser substrings: `chrome`, `msedge`, `edge`, `firefox`, `308046b0af4a39cb` (Firefox's AUMID hash), `opera`, `brave`, `vivaldi`, `yandex`, `arc`, `zen`.
  - Note that `"arc"` and `"zen"` are very loose matches.
- ANY: everything.

**Track output:**
- `sourceApp = "Spotify"` if Spotify, `"Browser"` if a browser, else the raw AUMID.
- For non-Spotify sessions, `TitleCleaner.splitYoutube(title, artist)` replaces artist/title.
- Status keys:
  - `lyricsflow.status.unsupported` — manager failed
  - `open_spotify` — SPOTIFY source, no session
  - `nothing` — other source with no session, or an empty title
  - `playing` / `paused`

### 7.4 TitleCleaner (exact regexes, Java syntax)
```
BRACKETS = \s*[\(\[\{【][^\)\]\}】]*[\)\]\}】]
NOISE    = (?i)\b(official\s*(music\s*)?(video|audio|lyric(s)?\s*video|visuali[sz]er)|lyrics?\s*video|lyrics?|audio|video\s*clip|clip\s*officiel|hd|hq|4k|remaster(ed)?(\s*\d{4})?|sped\s*up|slowed(\s*\+?\s*reverb)?|nightcore|8d\s*audio|extended|radio\s*edit|bass\s*boosted)\b
FEAT     = (?i)\s*[\(\[]?\s*\b(feat\.?|ft\.?|featuring|with)\s+[^\)\]\-]*[\)\]]?
```
**`splitYoutube(title, channel)`:**
1. `t = title.replaceFirst("^\(\d+\)\s*", "")`. This strips a "(3) " notification count.
2. `t = t.replaceAll("(?i)\s*[-–—|]\s*youtube( music)?\s*$", "")`.
3. `artist = channel` with these applied: `(?i)\s*-\s*topic$` → "", `(?i)vevo$` → "", `(?i)\s*official$` → "", then trim.
4. For each separator, in order `" - "`, `" – "`, `" — "`, `" | "`: if found at index>0 and the right side is non-empty:
   - for `" | "`, return `[artist, left]`
   - otherwise return `[left, right]`.
5. Otherwise return `[artist, t]`.

**`clean(s)`:**
1. Replace BRACKETS with " ", then NOISE with " ".
2. Apply `\s+-\s*$` → "" and `\s{2,}` → " ", then trim.
3. If the result is empty, return the original trimmed.

**`mainArtist(a)`:** take the first element of the split by `(?i)\s*(,|&|;|\bx\b|\bfeat\.?|\bft\.?|\bwith\b|/)\s*`. If that is blank, use `a.trim()`.

**`fold(s)`:** lowercase (ROOT), `ı→i`, `İ→i`, NFD, then strip `\p{M}+`.

**`variants(artist, title)`** gives the lookup candidates in order, de-duplicated by `lower(a+"|"+t)`, skipping empty titles, with each title whitespace-collapsed:
1. `(artist, title)`
2. `(artist, FEAT→" " (title))`
3. `(artist, clean(title))`
4. `(mainArtist, clean(FEAT→" " (title)))`
5. `(mainArtist, clean(title) with \s+-\s+.*$ removed)`
6. `(fold-NFD(mainArtist), fold-NFD(clean(FEAT(title))))` — diacritics stripped, case kept, `ı→i`, `İ→I`

C++ note: std::regex has no `(?i)` and no `\p{M}`. Use PCRE2 or ICU regex (ICU also gives NFD normalization). Java `\b` and `\s` are ASCII by default, so match that.

### 7.5 AppVolume (per-app volume slider for the music source)
- `CoInitializeEx(NULL, COINIT_MULTITHREADED=0)` once. Work runs on a single daemon thread.
- `CoCreateInstance(CLSID_MMDeviceEnumerator {BCDE0395-E52F-467C-8E3D-C4579291692E}, CLSCTX_ALL=23, IID_IMMDeviceEnumerator {A95664D2-9614-4F35-A746-DE8DB63617E6})`
- `[4] GetDefaultAudioEndpoint(eRender=0, eMultimedia=1)`
- `IMMDevice [3] Activate(IID_IAudioSessionManager2 {77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F}, 23, NULL)`
- `[5] GetSessionEnumerator` → `[3] GetCount`, `[4] GetSession(i)`.
- Per session:
  - QI `IAudioSessionControl2 {BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D}` → `[14] GetProcessId`.
  - Exe name via `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION=0x1000)` + `QueryFullProcessImageNameW`, then the basename lower-cased.
- Session filter:
  - Source YOUTUBE: exe ∈ {chrome.exe, msedge.exe, firefox.exe, opera.exe, opera_gx.exe, brave.exe, vivaldi.exe, browser.exe, arc.exe, zen.exe, librewolf.exe, waterfox.exe}.
  - Otherwise: exe == `spotify.exe`.
- Matching sessions: QI `ISimpleAudioVolume {87CE5498-68D6-44E5-9215-6DA47EF883D8}`.
  - `[3] SetMasterVolume(v, NULL)`
  - `[5] SetMute(b, NULL)`
  - `[4] GetMasterVolume` — the reported volume is the max over matching sessions
  - `[6] GetMute` — muted if any session is muted
- `get()` returns the cached value (−1 = unsupported) and schedules a refresh if more than 1 s has passed since the last read. `set()` clamps to 0..1, applies optimistically, and coalesces pending sets.

---

## 8. AudioSync — WASAPI loopback voice timing (`media/AudioSync.java`)

> **Status in v2.0: dead code.** Nothing calls `AudioSync.tick` (verified by grep). The algorithm is documented here in case we want it, but it is not needed to match v2.0 behaviour.

**Purpose:** for line-synced (not word-synced) LRC lyrics, the per-word times inside a line are *estimated* (split by syllables in `Lyrics`). AudioSync measures when vocals actually stop within each line and scales the estimated word times by a factor `k`. It does **not** estimate a global offset; the offsets are manual (§1.3, §9).

**Activation:** `tick(lyrics, t, playing)` only does work when it is Windows, the lyrics are `synced && !wordSynced`, and playback is running. It starts the capture thread `"LyricsFlow voice"` (daemon) once. On new lyrics: `k=1`, `measured=0`.

**Capture setup:**
1. `CoInitializeEx(NULL, 0)`.
2. `CoCreateInstance(MMDeviceEnumerator, CLSCTX_ALL)`.
3. `[4] GetDefaultAudioEndpoint(eRender=0, eConsole=0)`.
4. `[3] Activate(IID_IAudioClient {1CB9AD4C-DBFA-4C32-B178-C2F568A703B2}, 23)`.
5. `[8] GetMixFormat`. It requires `wBitsPerSample == 32` (assumed IEEE float; the extensible sub-format is not checked), channels ≥ 1 and rate ≥ 8000.
6. `[3] Initialize(AUDCLNT_SHAREMODE_SHARED=0, AUDCLNT_STREAMFLAGS_LOOPBACK=0x20000, hnsBufferDuration=10,000,000 (1 s), 0, fmt, NULL)`.
7. `[14] GetService(IID_IAudioCaptureClient {C8ADBD64-E71E-48A0-A4DE-185C395CD317})`.
8. `[10] Start`.
9. On exit: `[11] Stop`, `CoTaskMemFree(fmt)`, and release everything. Any failure sets `failed=true` permanently.

**Capture loop:**
- `[5] GetNextPacketSize`. If 0: push an "inactive" frame if no data has arrived for >100 ms, then sleep 8 ms.
- Otherwise `[3] GetBuffer(&data, &frames, &flags, NULL, NULL)`. If `flags & AUDCLNT_BUFFERFLAGS_SILENT (2)`, treat the samples as zeros. Then `[4] ReleaseBuffer(frames)`.

**Per-sample DSP** (mono = mean of channels):
```
hpA = exp(−2π·300/rate)      // one-pole high-pass, fc=300 Hz
lpA = exp(−2π·3400/rate)     // one-pole low-pass,  fc=3400 Hz
hp  = hpA·(hpPrevOut + x − hpPrevIn);  hpPrevIn=x; hpPrevOut=hp
lp  = lp·lpA + hp·(1−lpA)
band += lp²;  all += x²
```
**Every 20 ms frame** (`per = rate·20/1000` samples):
```
e   = band/n          // voice-band energy
tot = all/n           // full-band energy
level = level·0.995 + e·0.005        // slow running average of voice energy (init 1e−6)
active = e > 1.6·level  &&  e > 0.25·tot  &&  tot > 1e−6
push(active) into ring buffer: 1500 entries (30 s), with timestamp = currentTimeMillis
```
**Line-change measurement** (in `tick`, on the game thread):
- `offset = now_ms − t`, i.e. the wall clock at song time 0. `line = lastStartedBefore(t)`.
- When the line changes, if the previous line was `estimated` and `|offset − lastOffset| < 400 ms` (no seek):
  - `sung = voiceEnd(prev.start + offset, min(prev.end, next.start) + offset)`
  - `guess = prev.guessedEnd() − prev.start`, where guessedEnd is the origEnd of the last estimated word.
  - If `sung > 250 && guess > 250`:
    - `r = clamp(sung/guess, 0.55, 1.5)`
    - `k = 0.7k + 0.3r` (EMA)
    - `measured++`
- Then if the new line is estimated and `measured ≥ 2`: `line.retime(k)`. For each word, `start = min(end−40, lineStart + (origStart − lineStart)·k)`, and the same for the word end.

**`voiceEnd(from, to)`:**
- Walk the ring buffer backwards from the head, stopping at an empty stamp or a stamp < from.
- Among frames with stamp ≤ to: `seen++`, and if active, `on++`. Record the latest active stamp (`last`).
- Return `last − from` (ms of voice from line start to the last voiced frame) if `seen ≥ 10 && on ≥ 3 && on ≤ 0.97·seen`, else −1.

**Per-song offset storage** (not AudioSync): `config.songOffsets[lower(artist+"\0"+title)]`, clamped to ±5000 ms; a value of 0 removes the entry. Effective position = `max(0, track.positionNow() + syncOffsetMs + songOffset)` (MediaManager).

---

## 9. Keybinds (`Keys.java`)
Category `lyricsflow:main`. Defaults are GLFW key codes:

| Translation key | Default | GLFW | Action |
|---|---|---|---|
| key.lyricsflow.menu | **K** | 75 | Open LyricsFlowScreen (only if no screen is open) |
| key.lyricsflow.player | **J** | 74 | Open PlayerScreen |
| key.lyricsflow.onoff | **O** | 79 | Toggle `enabled`, save, clear WL when turning off, chat msg "LyricsFlow: On/Off" (green/red), toggle sound |
| key.lyricsflow.play_pause | **Down arrow** | 264 | SMTC TryTogglePlayPause (if mediaKeys) |
| key.lyricsflow.next | **Right arrow** | 262 | SMTC TrySkipNext (if mediaKeys) |
| key.lyricsflow.previous | **Left arrow** | 263 | SMTC TrySkipPrevious (if mediaKeys) |
| key.lyricsflow.earlier | **]** | 93 | Current song offset **+100 ms**. The toast shows "+0.1 s" etc. Click sound pitch 1.15 |
| key.lyricsflow.later | **[** | 91 | Current song offset **−100 ms**. Click sound pitch 0.9 |

Legacy migration (when `ver<7`): if a key is still bound to its old default it is reset to the new default. The old defaults were menu `.`, player `;` and on/off `]`.
Bedrock: Bedrock has no keybind API for mods. Read raw keys via our input hook, ignore them when a screen or chat is open, and store our own bindings in config.

---

## 10. Cosmetics — SKIP for personal build
`render/Cosmetics.java`, `CosmeticLayer.java`, `HelmetLayer.java` and `media/CosmeticSync.java` implement voxel hats/helmets: `assets/lyricsflow/cosmetics.json` holds per-voxel colour lists, with textures under `textures/cosmetic` and `textures/gui/cosmetics`. Selections are synced between players over a **public ntfy.sh topic** `https://ntfy.sh/lyricsflow-cosmetics-v1-7f3a9c`:
- POST `"lf1 <uuid> <cosmeticId>"` as text/plain, re-sent every 5 h.
- Streamed with `/json?since=12h`, using exponential backoff up to 60 s.
- Polled with `/json?poll=1&since=12h` (re-read every 15 s, or 120 s while live).
- User-Agent `LyricsFlow/1.0 (Minecraft mod; https://modrinth.com/mod/lyricsflow)`.

It also queries `api.mojang.com` and `sessionserver.mojang.com` for skins. The topic is unauthenticated and world-readable, and it leaks player UUIDs. **Do not port**: drop `cosmetic`, all cosmetic UI and any network calls. `customHotbar`/`HotbarStyle` (hotbar re-skin: pill radius 5.5, selection 18×18 with r=3, accent fill .22 and outline .95) is optional polish and also skippable.

---

## 11. Port checklist / gotchas
- These need **exact Java semantics**: `Anim.hash01`, `Rain.hash`, and `java.util.Random(374)` for the star layout. Use unsigned 64-bit shifts and wrapping multiplies.
- Colour lerp truncates toward zero per channel (`(int)` cast), and `alpha(c, m)` truncates as well.
- Pitch sign: negative is up throughout SkyPresets/SkyFx. Bedrock's camera pitch has the same sign convention (negative = look up), but verify against our camera hook.
- `Gfx.neon` draws at most 5 text passes and has no blur. A true glow would be a new feature, not part of the port.
- `AudioSync`, `sentenceMode`, `customWeather` and `hideInScreenshots` are inert in v2.0. `viewLock`/`randomPosition` are forced on load.
- Weather FX are config-driven, not detected from the world. The mixins feed the forced weather into vanilla rain/thunder levels and the precipitation type.
- The sky depth sphere is `clamp(renderDistance·8, 20, 96)` blocks. Draw sky layers before terrain (far sky) without depth writes; draw motes/streaks/particles in the translucent pass.
