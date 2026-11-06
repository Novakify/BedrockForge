// LyricsFlow port - settings. Field names, defaults and enums follow LyricsFlow v2.0's LFConfig (374m, MIT) so its
// presets/profiles map 1:1; cosmetics fields are dropped (personal build), and Bedrock Forge additions are marked [BF].
// Stored as JSON (mods/BedrockForgeCore/config/lyricsflow.json); presets are partial JSON overlays
// (resources/lyricsflow/presets/*.json) applied on top with applyJson().
// Colours are ARGB ints; 0 = "auto" (theme colour), written as "auto" / "#AARRGGBB".
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "lyrics/core/Json.h"

namespace bf::lyrics {

enum class Source : int { Spotify, Youtube, Any, AppleMusic }; // [BF] AppleMusic (Windows app via SMTC)
enum class LyricsStyle : int { Sky, Floating, Screen, Wall };
enum class TextStyle : int { Neon, Clean };
enum class EnterAnim : int { Word, Fade, Rise, Neon, Drop, Spin, Zoom, Glitch }; // Neon = 2.1 "Typed out"; Drop..Glitch [BF]
enum class ExitAnim : int { FloatUp, Drift, Fade, Fall, Shatter, Dissolve, Explode, Burn, Glitch, Vortex }; // Fall..Vortex [BF]
enum class TopBarStyle : int { Karaoke, Simple };
enum class LetterLand : int { Flat, Tumble }; // [BF] falling letters: lie flat as decals (video A) or full rigid tumble

// X(type, name, default). Types: B bool, I int, F float, S string, C colour (ARGB, 0 = auto), E enum.
#define BF_LYRICS_CONFIG_FIELDS(B, I, F, S, C, E)                                                                       \
    I(configVersion, 14)                                                                                               \
    B(enabled, true)                                                                                                   \
    E(Source, source, Source::Any) /* [BF] Java default is SPOTIFY; ANY so Apple Music works out of the box */                                                                                 \
    I(syncOffsetMs, 0)                                                                                                 \
    B(showLyrics, true)                                                                                                \
    E(LyricsStyle, lyricsStyle, LyricsStyle::Floating)                                                                 \
    B(skyWords, false)                                                                                                 \
    B(followView, false)                                                                                               \
    B(viewLock, false)                                                                                                 \
    B(showUnderground, false)                                                                                          \
    B(musicNotes, true)                                                                                                \
    B(randomPosition, true)                                                                                            \
    B(allCaps, false)                                                                                                  \
    F(floatHeight, 0.0f)                                                                                               \
    F(lyricsScale, 1.0f)                                                                                               \
    F(lyricsDistance, 1.0f)                                                                                            \
    F(skyDistance, 150.0f)                                                                                             \
    I(textShadow, 1)                                                                                                   \
    C(shadowColor, 0)                                                                                                  \
    B(varySizes, true)                                                                                                 \
    B(arcLayout, true)                                                                                                 \
    I(maxLines, 2)                                                                                                     \
    S(font, "lyricsflow_script")                                                                                       \
    E(TextStyle, textStyle, TextStyle::Neon)                                                                           \
    S(theme, "neon_pink")                                                                                              \
    B(customTheme, false)                                                                                              \
    C(singleColor, 0)                                                                                                  \
    C(customMain, int32_t(0xFFFFE3F6))                                                                                 \
    C(customA, int32_t(0xFFFF5AC8))                                                                                    \
    C(customB, int32_t(0xFF6FD8FF))                                                                                    \
    B(twoColor, true)                                                                                                  \
    F(twoColorStrength, 0.6f)                                                                                          \
    C(twoColorColor, 0)                                                                                                \
    B(glow, true)                                                                                                      \
    F(textOpacity, 1.0f)                                                                                               \
    E(EnterAnim, enterAnim, EnterAnim::Word)                                                                           \
    E(ExitAnim, exitAnim, ExitAnim::FloatUp)                                                                           \
    F(animSpeed, 1.0f)                                                                                                 \
    B(beatGlow, false)                                                                                                 \
    S(skyPreset, "off")                                                                                                \
    B(stars, true)                                                                                                     \
    F(starDensity, 0.6f)                                                                                               \
    B(motes, false)                                                                                                    \
    F(moteDensity, 0.5f)                                                                                               \
    B(streaks, true)                                                                                                   \
    F(streakDensity, 0.5f)                                                                                             \
    B(weatherRain, false)                                                                                              \
    B(weatherSnow, false)                                                                                              \
    B(weatherEmbers, false)                                                                                            \
    B(showTopBar, true)                                                                                                \
    E(TopBarStyle, topBarStyle, TopBarStyle::Karaoke)                                                                  \
    S(topBarFont, "outfit")                                                                                            \
    F(topBarScale, 1.0f)                                                                                               \
    F(topBarX, 0.5f)                                                                                                   \
    F(topBarY, 0.02f)                                                                                                  \
    F(screenLyricsX, 0.5f)                                                                                             \
    F(screenLyricsY, 0.32f)                                                                                            \
    B(showOnlyWhenPlaying, true)                                                                                       \
    B(mediaKeys, true)                                                                                                 \
    /* ---- [BF] look extras (the Java mod draws flat colour passes only; our SDF shader adds these) */                \
    F(outlineWidth, 0.0f)              /* 0..0.35 of the glyph's SDF range */                                          \
    C(outlineColor, 0)                 /* 0 = darken(theme.a) */                                                       \
    F(glowWidth, 0.25f)                /* soft halo radius, SDF units */                                               \
    F(glowStrength, 0.55f)             /* 0 = off; only used when glow = true */                                       \
    C(glowColor, 0)                    /* 0 = theme.a */                                                               \
    B(gradient, false)                 /* vertical fill gradient main -> gradientColor */                              \
    C(gradientColor, 0)                /* 0 = theme.b */                                                               \
    B(perLetterColor, false)           /* cycle theme main/a/b per letter */                                           \
    F(hueShiftSpeed, 0.0f)             /* animated hue rotation, turns per second (0 = off) */                         \
    F(italic, 0.0f)                    /* slant: x shift per unit of height (0.2 = ~11 deg, video B caps) */           \
    /* ---- LyricsFlow 2.1 (app.config.Config / LFConfig additions used by the world panel) */                       \
    B(skyImage, false)                 /* "My picture": your own image as the sky */                                   \
    S(skyImageFile, "")                /* file in <lyricsflow>/sky (png/jpg, 2:1 panorama); "" = first one */          \
    F(weatherAmount, 1.0f)             /* 0 .. 1 sky weather particles */                                              \
    F(skyStrength, 0.8f)               /* 0.1 .. 1 */                                                                  \
    F(skySpeed, 1.0f)                  /* 0.25 .. 2 */                                                                 \
    F(skyPattern, 1.0f)                /* 0.2 .. 1.5 */                                                                \
    B(skyShuffle, false)               /* random sky per song */                                                       \
    B(skyFitSong, true)                /* [LyricsForge] Random picks skies that fit the song (mood + its arc) */      \
    B(skyCurated, true)                /* [LyricsForge] Random leaves out the effect-looking skies */                 \
    B(fxSkyMusic, true)                /* sky reacts to music */                                                       \
    F(skyLively, 0.6f)                 /* 0 .. 1 sky motion */                                                         \
    F(skyBreath, 1.0f)                 /* 0 = off, breathing glow speed */                                             \
    I(skyPalette, -1)                  /* -1 = preset aurora colours, else WorldSky.PALETTES index */                  \
    B(skyDay, true)                    /* keep the sky in daytime too */                                               \
    B(fxDayNight, false)               /* dim the live sky by daylight */                                              \
    B(fxAurora, true)                                                                                                  \
    B(fxStars, true)                                                                                                   \
    B(fxWeather, true)                                                                                                 \
    B(fxSkyColor, true)                                                                                                \
    B(fxConstellations, true)                                                                                          \
    I(customTop, 0x0A0618)             /* CUSTOM sky colours (RGB) */                                                  \
    I(customHorizon, 0x2A1648)                                                                                         \
    I(customAurora1, 0xA391FF)                                                                                         \
    I(customAurora2, 0xFFFFFF)                                                                                         \
    B(customAurora, true)                                                                                              \
    B(clock, false)                    /* top bar extras */                                                            \
    B(fps, false)                                                                                                      \
    B(ping, false)                                                                                                     \
    B(customHotbar, false)             /* glass hotbar */                                                              \
    I(playerStyle, 0)                  /* [BF] music player card: 0 Classic, 1 Glass (frosted, RF blur), 2 Neon */    \
    B(uiSounds, true)                  /* menu sounds */                                                               \
    /* ---- LyricsFlow 2.1 performance (perf.AutoPerf / Profiles / FpsBoost) */                                      \
    I(quality, 2)                      /* 0 Performance, 1 Balanced, 2 Pretty */                                       \
    B(autoPerf, true)                  /* Automatic FPS: lowers effects when FPS drop */                               \
    I(targetFps, 0)                    /* 0 = auto (60) */                                                             \
    I(profile, 0)                      /* 0 Chill, 1 PvP */                                                            \
    B(fpsBoost, false)                 /* game settings boost (applied to options.txt on the next restart) */          \
    B(vsync, false)                    /* [BF] mirrors the game option for the FPS target (set by the glue) */         \
    I(maxFps, 0)                       /* [BF] game max framerate, 0 = unlimited (set by the glue) */                  \
    /* ---- [BF] falling letters (new feature, not in LyricsFlow) */                                                   \
    B(fxWave, false)                   /* [BF] letters ripple in a wave */                                             \
    B(fxRainbow, false)                /* [BF] colours cycle (hueShiftSpeed 0.12 when that is 0) */                    \
    B(fxBeatShake, false)              /* [BF] words shake a little on the beat */                                     \
    B(songMemory, true)                /* [BF] remember each song's energy map (lyricsflow_memory, never audio) */     \
    B(director, true)                  /* [BF] Conductor: section-aware glow / punch / letter fx budget */             \
    B(titleCard, true)                 /* [BF] movie-style title + artist card when a song starts */                   \
    B(heroWords, true)                 /* [BF] held notes get the hero treatment (bigger, brighter) */                 \
    B(contrastGuard, true)             /* [BF] dark outline + shadow when the sky behind the words is bright */        \
    B(constellations, true)            /* [BF] the sky writes the song: hook lines become constellations */           \
    I(reactivity, 1)                   /* [BF] Director: 0 soft, 1 balanced, 2 dynamic, 3 cinematic */                 \
    S(scene, "")                       /* [BF] Scene Studio scene id last applied ("" = hand-made look) */          \
    B(songScenes, true)                /* [BF] remember the scene picked for each song and bring it back */            \
    B(albumColors, false)              /* [BF] letter / glow / star colour from the album art (eased per song) */      \
    B(cinemaGrade, true)               /* [BF] Director colour grade + vignette of the world per scene (RF, D3D12) */  \
    B(autoFont, true)                  /* [LyricsForge] each song gets a font that fits it (mood + cover); off = your font */ \
    B(colorWords, true)                /* [LyricsForge] colour words in the lyrics (pink, blue) turn that colour when sung */ \
    B(skyFog, true)                    /* [LyricsForge] far terrain fades into the sky's horizon colour (BF fog hook) */ \
    B(worldLight, true)                /* [LyricsForge] the sky's colour and light fall on the world; drops flood it (RF) */ \
    B(impactFrames, true)              /* [BF] a soft light flash + bloom / CA kick on drops and chorus hits (RF) */   \
    B(letterbox, true)                 /* [BF] cinema bars ease in during builds and big hooks (RF) */               \
    B(portals, false)                  /* [BF] a ring of light opens in the sky on choruses / drops (off: Moments) */ \
    B(chorusVariety, true)             /* [BF] each chorus gets its own moment from the sky's deck, the last one all */ \
    B(lyricMoments, true)              /* [BF] sung words about rain / fire / snow / stars / love ... touch the world */ \
    B(songArc, true)                   /* [BF] choruses grow through the song, the bridge goes quiet and strange */   \
    B(instrumentalFx, true)            /* [BF] no singing: ground rings on the kicks, horizon equaliser, livelier sky */ \
    B(songOutro, true)                 /* [BF] end credits: the sky dims, title + artist + the last line rise and fade */ \
    B(songTint, true)                  /* [BF] each song's cover colour tints the scene's grade a little */           \
    B(directorCuts, true)              /* [BF] Showcase cuts to a new camera angle on drops / choruses / calm */     \
    B(kineticWords, true)              /* [BF] lyric-video words: short lines stack big, hooks grow, drops slam in */ \
    B(chorusLooks, true)               /* [BF] the world changes on a chorus: chorus sky, light wave, meteors, letters */ \
    B(dropMoment, true)                /* [BF] held breath before a known drop (dim, slow), then it all lands at once */ \
    B(autoScene, false)                /* [BF] Auto director: pick a scene for each song from its energy */          \
    B(clipMode, false)                 /* [BF] Clip mode: save an NVIDIA Instant Replay clip after a song's big moment */ \
    S(clipHotkey, "ctrl+shift+f10")    /* [BF] the replay "save" hotkey: OBS Save Replay (bind it there), or NVIDIA alt+f10 */\
    B(clipVertical, true)              /* [BF] also make a 1080x1920 TikTok copy of each clip (needs ffmpeg) */      \
    S(clipFolder, "")                  /* [BF] where the recorder saves ("" = your Videos folder) */                \
    S(ffmpegPath, "")                  /* [BF] ffmpeg.exe ("" = find it: tools folder, PATH, winget) */              \
    F(intensity, 1.0f)                 /* [BF] Cinema home: one dial for how big the whole show is, 0.3 .. 1.6 */     \
    B(shotLanguage, true)              /* [BF] Director framing: calm = wide (far, smaller), high = close-up */      \
    B(translations, true)              /* [BF] show translations under the lines (NetEase / your override file) */  \
    B(hudPlayer, false)                /* [BF] corner music player (LyricsFlow 2.2 Extra): cover, time, controls */  \
    B(hudPlayerLyrics, true)           /* [BF] ... with the line being sung and the next two */                     \
    B(hudRound, true)                  /* [BF] rounded LyricsForge look (off = square Minecraft look) */            \
    B(hudColorFromCover, false)        /* [BF] highlights follow the song cover instead of the lyrics colour */     \
    F(hudPlayerX, 0.01f)               /* [BF] 0 left .. 1 right */                                                 \
    F(hudPlayerY, 0.62f)               /* [BF] 0 top .. 1 bottom */                                                 \
    F(hudPlayerScale, 1.0f)                                                                                            \
    B(snowAnySky, false)               /* [BF] LyricsFlow 2.2 "Snow": snow falls around you with any sky */          \
    B(showcase, false)                 /* [BF] Showcase: camera sways behind you, soft focus, bars (RF, D3D12) */    \
    B(pinned, false)                   /* [BF] floating lyrics stay where you pinned them */                        \
    F(pinX, 0.0f)                                                                                                      \
    F(pinY, 0.0f)                                                                                                      \
    F(pinZ, 0.0f)                                                                                                      \
    F(pinYaw, 0.0f)                                                                                                    \
    F(stereoDrift, 0.5f)               /* [BF] lyrics lean toward the side the sound is on, 0 off .. 1 */           \
    B(wordRibbon, true)                /* [BF] a ribbon of light runs under the line with the singing */             \
    B(materialImpacts, true)           /* [BF] falling letters kick up what they land on (chips, puffs, splashes) */ \
    B(atmosphere, true)                /* [BF] glowing motes around you that sparkle with the treble, flare on drops */ \
    B(fovBreath, false)                /* [BF] camera FOV tightens in builds, punches out on drops (changes the view) */ \
    I(letterFx, 0)                     /* [BF] animated letters: 0 none, 1 neon, 2 fire, 3 water, 4 ice, 5 electric,   \
                                          6 lava, 7 glitch, 8 galaxy, 9 liquid ink, 10 holographic (RF glyph shader) */                   \
    C(letterFxColor, 0)                /* [BF] effect colour, 0 = the text colour */                                   \
    F(letterFxSpeed, 1.0f)             /* [BF] 0.25 .. 3 */                                                            \
    F(letterFxIntensity, 1.0f)         /* [BF] 0.2 .. 2 */                                                             \
    F(fxIntensity, 1.0f)               /* [BF] strength of the break-apart exits 0.2 .. 2 */                           \
    B(spreadOut, false)                /* [BF] more room between lyric lines/words (floating + sky) */                 \
    B(wholeLine, false)                /* [BF] floating: the line stays one readable row (reference fall video) */     \
    B(fallingLetters, false)                                                                                           \
    E(LetterLand, letterLand, LetterLand::Flat)                                                                        \
    I(fallDelayMs, 350)                /* after a word's exit starts */                                                \
    F(gravity, 18.0f)                  /* blocks / s^2 */                                                              \
    F(bounce, 0.25f)                   /* restitution 0..1 */                                                          \
    F(friction, 0.6f)                  /* tangential damping on contact 0..1 */                                        \
    F(spin, 1.0f)                      /* initial spin multiplier */                                                   \
    I(letterLifetimeMs, 6000)          /* resting time before fade */                                                  \
    I(letterFadeMs, 1200)                                                                                              \
    I(maxLetters, 400)                                                                                                 \
    /* ---- [BF] combat safety (PvP on Hive / CubeCraft) */                                                            \
    B(combatCalm, true)                /* [LyricsForge] PvP calm: in a fight the camera, bars, flashes and darkening step aside */ \
    B(combatSafe, false)                                                                                               \
    F(keepOutDeg, 14.0f)               /* no lyrics within this angle of the crosshair */                              \
    F(combatAlpha, 0.35f)              /* opacity cap for a while after hitting / being hit */                         \
    I(combatHoldMs, 2500)

struct Config {
#define BF_LC_B(n, d) bool n = d;
#define BF_LC_I(n, d) int n = d;
#define BF_LC_F(n, d) float n = d;
#define BF_LC_S(n, d) std::string n = d;
#define BF_LC_C(n, d) int32_t n = int32_t(d);
#define BF_LC_E(T, n, d) T n = d;
    BF_LYRICS_CONFIG_FIELDS(BF_LC_B, BF_LC_I, BF_LC_F, BF_LC_S, BF_LC_C, BF_LC_E)
#undef BF_LC_B
#undef BF_LC_I
#undef BF_LC_F
#undef BF_LC_S
#undef BF_LC_C
#undef BF_LC_E
    std::map<std::string, int> songOffsets; // per-song manual offset, key = lower(artist + "\0" + title)
};

/// Overlay every known key present in `j` onto cfg (unknown keys ignored). Returns the number of keys applied.
int         applyJson(Config& cfg, json::Value const& j);
/// Full config as pretty JSON.
std::string toJson(Config const& cfg);

std::string_view enumName(Source v);
std::string_view enumName(LyricsStyle v);
std::string_view enumName(TextStyle v);
std::string_view enumName(EnterAnim v);
std::string_view enumName(ExitAnim v);
std::string_view enumName(TopBarStyle v);
std::string_view enumName(LetterLand v);

// ---- themes (dev.lyricsflow.render.Themes, exact colours)
struct Theme {
    std::string_view id;
    uint32_t         main, a, b, skyTop, skyBottom; // ARGB
};
Theme const* findTheme(std::string_view id);
std::vector<Theme> allThemes(); // Themes.ALL order (the panel's colour dots)
/// Themes.current(): custom theme / single colour override applied.
Theme        currentTheme(Config const& cfg);

// ---- colour helpers (dev.lyricsflow.util.Col, Java int truncation semantics)
uint32_t colLerp(uint32_t c1, uint32_t c2, float t);
uint32_t colAlpha(uint32_t c, float mul);
uint32_t colDarken(uint32_t c, float x);
uint32_t colBrighten(uint32_t c, float x);

} // namespace bf::lyrics
