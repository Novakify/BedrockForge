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

/// Per-frame values WorldSky.renderSky feeds the shader besides the param row.
struct Frame {
    float alpha      = 1; // vertex Color.a
    float brightness = 1; // vertex Color.r * 2
    float t          = 0; // UV0.x = skyTime * 2.2
};

/// WorldSky's clock + fades (renderSky): advance with the real frame time.
class Clock {
public:
    /// dtSec clamped to 0.1 like Java. night: 0 day .. 1 night (Minecraft 13000..23000 ticks); beat: 0..1 pulse;
    /// calm: 1 while a line is playing, eases to 0.7 between lines (only with fxSkyMusic).
    Frame tick(Preset const* p, Config const& cfg, double dtSec, float night, float beat, bool lineActive,
               bool playing);
    double time() const { return mTime; }

private:
    double      mTime = 1000.0, mBreath = 0;
    float       mVisible = 0, mCalm = 1, mLiveFade = 0;
    std::string mLast;
};

} // namespace bf::lyrics::sky
