// LyricsForge - [BF] Scene Studio. Original code for Bedrock Forge; see Studio.h.
#include "lyrics/core/Studio.h"

#include <cstdio>

#include "lyrics/core/Json.h"

namespace bf::lyrics {

namespace {
using E = EnterAnim;
using X = ExitAnim;
int ei(E e) { return int(e); }
int xi(X x) { return int(x); }
// letter fx ids: 0 none, 1 neon, 2 fire, 3 water, 4 ice, 5 electric, 6 lava, 7 glitch, 8 galaxy, 9 ink, 10 holo
} // namespace

std::vector<StudioScene> const& builtinScenes() {
    static std::vector<StudioScene> const s = [] {
        std::vector<StudioScene> v;
        auto add = [&](StudioScene x) { v.push_back(std::move(x)); };
        //   id                  name                  mood
        add({"celestial_dream", "Celestial Dream", "soft auroras, stars, constellation hooks", "aurora", "lavender", 1,
             int32_t(0xFFB9A2FF), false, 0.7f, 0.8f, ei(E::Fade), xi(X::FloatUp), int(LyricsStyle::Sky), 0, true, 0.6f});
        add({"cosmic_collapse", "Cosmic Collapse", "black hole, galaxy letters, heavy bass", "black_hole", "galaxy", 8, 0,
             false, 1.0f, 1.2f, ei(E::Zoom), xi(X::Explode), int(LyricsStyle::Sky), 3, true, 0.7f});
        add({"liquid_paradise", "Liquid Paradise", "deep sea, water letters, gentle flow", "deep_sea", "ocean", 3, 0, false,
             0.8f, 1.0f, ei(E::Rise), xi(X::Dissolve), int(LyricsStyle::Floating), 1, true, 0.5f});
        add({"neon_afterlife", "Neon Afterlife", "neon city, glitch, energetic", "neon_city", "vaporwave", 1, 0, true, 1.4f,
             1.3f, ei(E::Glitch), xi(X::Glitch), int(LyricsStyle::Floating), 2, false, 0.8f});
        add({"dreamcore", "Dreamcore", "pastel watercolour, floaty and surreal", "aquarell", "candy", 3, 0, false, 0.6f,
             0.7f, ei(E::Fade), xi(X::Drift), int(LyricsStyle::Sky), 0, true, 0.45f});
        add({"anime_impact", "Anime Impact", "storm sky, fire letters, big drops", "thunder", "fire", 2, 0, false, 1.4f,
             1.4f, ei(E::Drop), xi(X::Shatter), int(LyricsStyle::Floating), 3, false, 0.75f});
        add({"cinematic_romance", "Cinematic Romance", "golden hour, warm glow, elegant", "golden_hour", "sunset", 1,
             int32_t(0xFFFFC27A), false, 0.6f, 0.7f, ei(E::Fade), xi(X::FloatUp), int(LyricsStyle::Sky), 0, true, 0.55f});
        add({"melancholy", "Melancholy", "midnight blues, restrained, ice", "midnight", "midnight", 4, 0, false, 0.5f, 0.6f,
             ei(E::Fade), xi(X::Fade), int(LyricsStyle::Floating), 0, true, 0.35f});
        add({"ethereal_fantasy", "Ethereal Fantasy", "prism light, magical, airy", "prism", "aurora", 1, 0, true, 0.8f,
             0.9f, ei(E::Rise), xi(X::Dissolve), int(LyricsStyle::Sky), 1, true, 0.6f});
        add({"dark_cosmic", "Dark Cosmic", "nebula, galaxy lettering, mysterious", "nebula", "enchant", 8, 0, false, 0.8f,
             1.1f, ei(E::Zoom), xi(X::Burn), int(LyricsStyle::Sky), 2, true, 0.65f});
        add({"cyber_edit", "Cyber Edit", "matrix, electric letters, glitch cuts", "matrix", "matrix", 5, 0, false, 1.3f, 1.2f,
             ei(E::Glitch), xi(X::Glitch), int(LyricsStyle::Floating), 3, false, 0.8f});
        add({"blood_moon", "Blood Moon", "crimson, lava letters, intense", "blood_moon", "blood_moon", 6, 0, false, 1.0f,
             1.2f, ei(E::Drop), xi(X::Burn), int(LyricsStyle::Sky), 2, true, 0.7f});
        { StudioScene a{"album_glow", "Album Glow", "colours from the cover art, galaxy sky", "galaxy", "galaxy", 1, 0, false, 0.9f,
                        1.0f, ei(E::Rise), xi(X::Dissolve), int(LyricsStyle::Sky), 1, true, 0.6f};
          a.albumColors = true; add(a); }
        // [LyricsForge] the four new painted skies, each with its own letter material
        add({"astral_symphony", "Astral Symphony", "concert of stars, holographic letters", "astral", "aurora", 10, 0, false,
             0.8f, 1.0f, ei(E::Rise), xi(X::FloatUp), int(LyricsStyle::Sky), 2, true, 0.6f});
        add({"eclipse_heartbeat", "Eclipse Heartbeat", "black sun beating with the bass, lava", "eclipse", "blood_moon", 6, 0,
             false, 1.1f, 1.2f, ei(E::Zoom), xi(X::Burn), int(LyricsStyle::Sky), 3, true, 0.7f});
        add({"dream_ocean", "Dream Ocean", "pastel sea, ink letters bleeding softly", "dream_ocean", "ocean", 9,
             int32_t(0xFF5A3A8C), false, 0.7f, 0.9f, ei(E::Fade), xi(X::Dissolve), int(LyricsStyle::Sky), 0, true, 0.4f});
        add({"meteor_chorus", "Meteor Chorus", "meteors in waves, neon, chorus lights up", "meteor", "midnight", 1, 0, false,
             1.0f, 1.0f, ei(E::Drop), xi(X::Shatter), int(LyricsStyle::Sky), 2, true, 0.7f});
        return v;
    }();
    return s;
}

std::optional<StudioScene> findScene(std::string_view id, std::vector<StudioScene> const& user) {
    for (auto const& s : user)
        if (s.id == id) return s;
    for (auto const& s : builtinScenes())
        if (s.id == id) return s;
    return std::nullopt;
}

void applyScene(Config& c, StudioScene const& s) {
    if (!s.sky.empty()) c.skyPreset = s.sky;
    if (!s.theme.empty()) c.theme = s.theme;
    if (s.letterFx >= 0) {
        c.letterFx          = s.letterFx;
        c.letterFxColor     = s.fxColor;
        c.fxRainbow         = s.rainbow;
        c.letterFxSpeed     = s.fxSpeed;
        c.letterFxIntensity = s.fxStrength;
    }
    if (s.enter >= 0) c.enterAnim = EnterAnim(s.enter);
    if (s.exit >= 0) c.exitAnim = ExitAnim(s.exit);
    if (s.style >= 0) c.lyricsStyle = LyricsStyle(s.style);
    if (s.glow >= 0) c.glowStrength = s.glow;
    c.reactivity     = s.reactivity;
    c.constellations = s.constellations;
    c.albumColors    = s.albumColors;
    c.scene          = s.id;
}

StudioScene captureScene(Config const& c, std::string id, std::string name) {
    StudioScene s;
    s.id = std::move(id);
    s.name = std::move(name);
    s.mood = "your scene";
    s.sky = c.skyPreset;
    s.theme = c.theme;
    s.letterFx = c.letterFx;
    s.fxColor = c.letterFxColor;
    s.rainbow = c.fxRainbow;
    s.fxSpeed = c.letterFxSpeed;
    s.fxStrength = c.letterFxIntensity;
    s.enter = int(c.enterAnim);
    s.exit = int(c.exitAnim);
    s.style = int(c.lyricsStyle);
    s.reactivity = c.reactivity;
    s.constellations = c.constellations;
    s.glow = c.glowStrength;
    s.albumColors = c.albumColors;
    return s;
}

namespace {
std::string q(std::string const& s) {
    std::string o = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if (uint8_t(ch) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += ch;
    }
    return o + "\"";
}
} // namespace

std::string sceneToJson(StudioScene const& s) {
    char b[512];
    std::snprintf(b, sizeof b,
                  ",\n  \"letterFx\": %d, \"fxColor\": %d, \"rainbow\": %s, \"fxSpeed\": %.3f, \"fxStrength\": %.3f,\n"
                  "  \"enter\": %d, \"exit\": %d, \"style\": %d, \"reactivity\": %d, \"constellations\": %s, \"glow\": %.3f, \"albumColors\": %s\n}\n",
                  s.letterFx, s.fxColor, s.rainbow ? "true" : "false", s.fxSpeed, s.fxStrength, s.enter, s.exit, s.style,
                  s.reactivity, s.constellations ? "true" : "false", s.glow, s.albumColors ? "true" : "false");
    return "{\n  \"id\": " + q(s.id) + ", \"name\": " + q(s.name) + ", \"mood\": " + q(s.mood) + ",\n  \"sky\": " + q(s.sky) +
           ", \"theme\": " + q(s.theme) + b;
}

std::optional<StudioScene> sceneFromJson(std::string_view text) {
    auto j = json::parse(text);
    if (!j || !j->isObject() || (*j)["id"].asString().empty()) return std::nullopt;
    auto const& v = *j;
    StudioScene s;
    s.id = v["id"].asString();
    s.name = v["name"].asString().empty() ? s.id : v["name"].asString();
    s.mood = v["mood"].asString();
    s.sky = v["sky"].asString();
    s.theme = v["theme"].asString();
    s.letterFx = int(v["letterFx"].asInt(-1));
    s.fxColor = int32_t(v["fxColor"].asInt(0));
    s.rainbow = v["rainbow"].asBool(false);
    s.fxSpeed = float(v["fxSpeed"].asNumber(1));
    s.fxStrength = float(v["fxStrength"].asNumber(1));
    s.enter = int(v["enter"].asInt(-1));
    s.exit = int(v["exit"].asInt(-1));
    s.style = int(v["style"].asInt(-1));
    s.reactivity = int(v["reactivity"].asInt(1));
    s.constellations = v["constellations"].asBool(true);
    s.glow = float(v["glow"].asNumber(-1));
    s.albumColors = v["albumColors"].asBool(false);
    return s;
}

} // namespace bf::lyrics
