// LyricsFlow port - live sky data + shader parameters (dev.lyricsflow.app.sky.SkyPreset / LiveSky / LiveSkyShader,
// LyricsFlow 2.1 by 374m, MIT). The look itself is the live_sky shader (resources/lyricsflow/shaders); this module
// owns the preset table and packs the 16x1 RGBA8 parameter row the shader reads, byte-for-byte like
// LiveSkyShader.params().
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bf::lyrics {
struct Config;
}

namespace bf::lyrics::sky {

enum class Weather : int { None, Rain, Snow, Blizzard, Thunder, Embers, Fireflies, Petals };
enum class BodyKind : int { Sun = 0, Moon = 1, Eclipse = 2, Synth = 3 };

struct Preset {
    char const* id;      // lower-case enum name ("synthwave")
    uint32_t    top, horizon;
    float       strength, stars;
    bool        aurora;
    uint32_t    aurora1, aurora2;
    Weather     weather;
    uint32_t    moon;
    bool        shootingStars;
    uint32_t    lyricColor;
    int         ordinal; // enum ordinal = shader PRESET (>= 20 painted)
    bool painted() const { return ordinal >= 20; }
};

struct Body {
    double   az, el, radius; // degrees, degrees, shader units
    uint32_t color;
    BodyKind kind;
};

/// All 29 presets in enum order (index == ordinal).
std::vector<Preset> const& presets();
/// SkyPreset.order(): the 20 presets the menu shows.
std::vector<std::string_view> const& order();
/// By id; "custom" gets the CUSTOM colours from cfg (SkyPreset.applyCustom). nullopt for "off"/unknown.
std::optional<Preset> find(std::string_view id, Config const& cfg);

/// LiveSky.body(): sun/moon for the presets that have one.
std::optional<Body> body(Preset const& p);
/// WorldSky.aurora1/2 (palette override, else preset colours with LyricsFlow's fallbacks).
uint32_t aurora1(Preset const& p, Config const& cfg);
uint32_t aurora2(Preset const& p, Config const& cfg);

/// One texel = {r, g, b, a} bytes.
using Texel  = std::array<uint8_t, 4>;
using Params = std::array<Texel, 16>;

/// LiveSkyShader.params(): `time` is WorldSky.skyTime (seconds, speed-scaled), `invRot` the inverse of the view
/// rotation, row-major m00 m01 m02 m10 ... (identity when the dome is drawn in world axes).
Params pack(Preset const& p, uint32_t a1, uint32_t a2, double time, Config const& cfg, float const invRot[9]);

/// WorldSky.initStars(): 1300 stars from java.util.Random(1337), same order of draws.
struct Star {
    float az, el, size, bright, phase; // degrees, degrees, 1 or 1.8, 0.35..1, 0..6.28
    float x, y, z;                     // unit direction (+Y up, az 0 = +Z)
};
std::vector<Star> const& stars();

/// One star quad of WorldSky.drawStars: a flat square of colour kStarColor facing the camera; `size` is its side as a
/// fraction of the sky distance (= angular size in radians).
struct StarDraw {
    float x, y, z, size, alpha;
};
/// quality 0..2 (Q_STARS 0.4 / 0.65 / 1), alpha = fade * day/night * (0.85 + 0.3 beat), time = Clock::time(),
/// fwd = camera forward (stars behind the camera are skipped like Java).
void drawStars(Preset const& p, Config const& cfg, int quality, float alpha, double time, float const fwd[3],
               std::vector<StarDraw>& out);
inline constexpr uint32_t kStarColor = 0xF4F0FF;

/// Per-frame values WorldSky.renderSky computes (fades, music, day/night) - the shader's vertex inputs plus what the
/// other layers (stars, weather, picture) are scaled by.
struct Frame {
    float alpha      = 1; // live shader: vertex Color.a = visible * liveFade
    float brightness = 1; // live shader: vertex Color.r * 2
    float t          = 0; // live shader: UV0.x = skyTime * 2.2
    float visible    = 0; // f14: sky on (Anim 3/s)
    float bright     = 1; // f27: day/night dimming
    float beat       = 0; // f21: beat pulse (fxSkyMusic)
    float outside    = 1; // f15: 1 under open sky, eases to 0 underground / indoors (Anim 2/s) - weather only
    float picture    = 0; // "My picture" fade (Anim 2.5/s)
    float picBright  = 1; // picture brightness (day/night)
    double time      = 0; // skyTime (seconds, speed-scaled)
};

/// WorldSky's clock + fades (renderSky); call once per frame.
class Clock {
public:
    struct In {
        double dtSec      = 0;     // real frame time (clamped to 0.1 like Java)
        float  night      = 1;     // 0 day .. 1 night (Minecraft time 13000..23000 = 1, 1000-tick ramps)
        float  beat       = 0;     // NowPlaying.beatPulse
        bool   lineActive = true;  // a lyric line is playing (else "calm" eases to 0.7)
        bool   playing    = false;
        bool   skyOpen    = true;  // the sky is visible above the player's head (underground fade)
        bool   picture    = false; // "My picture" is on and loaded
    };
    Frame  tick(Preset const* p, Config const& cfg, In const& in);
    double time() const { return mTime; }

private:
    double      mTime = 1000.0, mBreath = 0;
    float       mVisible = 0, mCalm = 1, mLiveFade = 0, mOutside = 1, mPic = 0;
    std::string mLast;
};

/// Minecraft day time (ticks) -> the "night" factor renderSky uses.
float nightOf(long long dayTime);

} // namespace bf::lyrics::sky
