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
enum class EnterAnim : int { Word, Fade, Rise, Neon }; // Neon = 2.1 "Typed out (LyricsFlow)" (animates like Word for now)
enum class ExitAnim : int { FloatUp, Drift, Fade };
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
    F(skyStrength, 0.85f)              /* 0.1 .. 1 */                                                                  \
    F(skySpeed, 1.0f)                  /* 0.25 .. 2 */                                                                 \
    F(skyPattern, 1.0f)                /* 0.2 .. 1.5 */                                                                \
    B(skyShuffle, false)               /* random sky per song */                                                       \
    B(fxSkyMusic, false)               /* sky reacts to music */                                                       \
    B(clock, false)                    /* top bar extras */                                                            \
    B(fps, false)                                                                                                      \
    B(ping, false)                                                                                                     \
    B(customHotbar, false)             /* glass hotbar */                                                              \
    B(uiSounds, true)                  /* menu sounds */                                                               \
    /* ---- [BF] falling letters (new feature, not in LyricsFlow) */                                                   \
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
