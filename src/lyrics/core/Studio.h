// LyricsForge - [BF] Scene Studio: one scene = a whole look (sky, lyric material, colours, entry/exit, style, how
// strongly the Director reacts, constellations). Original code for Bedrock Forge.
//
// A scene is applied on top of the config (the fields it names), so picking one changes everything coherently - the
// "one cinematic system" idea: lyrics, sky, letter material and the Director's taste move together. Built-in scenes
// cover the cinematic presets of the vision doc; user scenes are JSON files in config/lyricsflow_scenes/ (save /
// duplicate / edit / load), and a scene can be remembered per song (config/lyricsflow_songscenes.json).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lyrics/core/Config.h"

namespace bf::lyrics {

struct StudioScene {
    std::string id, name, mood;   // mood = one line for the panel ("soft auroras, constellation lyrics")
    std::string sky;              // sky preset id ("" = keep)
    std::string theme;            // colour theme id ("" = keep)
    int         letterFx = -1;    // -1 = keep
    int32_t     fxColor  = 0;
    bool        rainbow  = false;
    float       fxSpeed = 1, fxStrength = 1;
    int         enter = -1, exit = -1; // EnterAnim / ExitAnim ints, -1 = keep
    int         style = -1;            // LyricsStyle, -1 = keep
    int         reactivity = 1;        // 0 soft, 1 balanced, 2 dynamic, 3 cinematic
    bool        constellations = true;
    float       glow = -1;             // glowStrength, -1 = keep
    bool        albumColors = false;   // colours from the album art
    // [BF] chorus transformation: what the world turns into on a chorus / drop ("" / -1 = the scene's default)
    std::string chorusSky;
    int         chorusFx = -1, chorusExit = -1;
};

/// [BF] the chorus look of the current scene: sky to blend to (may be "" = keep), letter material and exit while
/// lifted (-1 = keep). Built-in scenes have hand-picked defaults; a hand-made look (no scene) keeps its sky and
/// just shatters its letters.
struct ChorusLook {
    std::string sky;
    int         letterFx = -1, exit = -1;
};
/// variant: per-song variety (odd = the scene's second chorus sky), e.g. songVariant(trackKey)
ChorusLook chorusLook(Config const& cfg, std::vector<StudioScene> const& user = {}, uint32_t variant = 0);
inline uint32_t songVariant(std::string_view key) { uint32_t h = 2166136261u; for (char c : key) { h ^= uint8_t(c); h *= 16777619u; } return h >> 7; }
/// [BF] Auto director: a scene for this song from its energy (0..1, < 0 = unknown -> balanced pool), stable per song
/// (the same key always gets the same pick). "" = nothing to pick.
std::string autoSceneFor(std::string_view songKey, float energy);

/// the built-in scenes (Celestial Dream first)
std::vector<StudioScene> const& builtinScenes();
std::optional<StudioScene>      findScene(std::string_view id, std::vector<StudioScene> const& user = {});
/// writes the scene's fields into cfg and remembers cfg.scene = id
void applyScene(Config& cfg, StudioScene const& s);
/// capture the current look as a scene (Scene Studio "save as")
StudioScene captureScene(Config const& cfg, std::string id, std::string name);

std::string                 sceneToJson(StudioScene const& s);
std::optional<StudioScene>  sceneFromJson(std::string_view text);

} // namespace bf::lyrics
