// LyricsFlow port - live sky data + parameter packing. See Sky.h.
#include "lyrics/core/Sky.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Config.h"

namespace bf::lyrics::sky {

namespace {
struct Row {
    char const* id;
    uint32_t    top, horizon;
    float       strength, stars;
    bool        aurora;
    uint32_t    a1, a2;
    Weather     weather;
    uint32_t    moon;
    bool        shooting;
    uint32_t    lyric;
};
// SkyPreset enum data, in ordinal order.
Row const kRows[] = {
#include "lyrics/core/SkyPresets.inc"
};

// WorldSky.PALETTES (Config.skyPalette >= 0 overrides the aurora pair).
uint32_t const kPalettes[][2] = {
    {0x2EE6D6, 0x9AF6FF}, {0x46FFB0, 0x7CE8FF}, {0xFFB35C, 0xFF6E6E}, {0x9FD8FF, 0xFFFFFF}, {0xD14BFF, 0x4BD8FF},
};

Texel rgba(uint32_t rgb, int a) {
    return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), uint8_t(a)};
}
int b8(float v) { return std::clamp(int(std::lround(v * 255.0f)), 0, 255); }
uint32_t pair(double hi, double lo) {
    auto c = [](double v) { return uint32_t(std::clamp<long long>(std::llround(v), 0, 65535)); };
    return c(hi) << 16 | c(lo);
}
Texel word(uint32_t v) { return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }
double enc(float v) { return (std::clamp(v, -2.0f, 2.0f) + 2.0) / 4.0 * 65535.0; }
float  outCubic(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - (1 - t) * (1 - t) * (1 - t); }
} // namespace

std::vector<Preset> const& presets() {
    static std::vector<Preset> const v = [] {
        std::vector<Preset> o;
        int                 i = 0;
        for (auto const& r : kRows)
            o.push_back({r.id, r.top, r.horizon, r.strength, r.stars, r.aurora, r.a1, r.a2, r.weather, r.moon,
                         r.shooting, r.lyric, i++});
        return o;
    }();
    return v;
}

std::vector<std::string_view> const& order() {
    static std::vector<std::string_view> const v{
        "summer", "dawn", "dusk", "bright_night", "silver_lining", // [LyricsForge] the realistic skies, newest first
        "rift", "supernova", "warp", "silk", "eclipse", // LyricsFlow 2.2
        "inferno",  "matrix", "prism",  "pulsar",    "aquarell",  "synthwave",   "aurora",
        "nebula",   "black_hole", "cosmos", "neon_city", "golden_hour", "cotton_candy", "sakura",
        "frost",    "midnight", "deep_sea", "blood_moon", "storm", "custom"};
    return v;
}

std::optional<Preset> find(std::string_view id, Config const& cfg) {
    for (auto const& p : presets()) {
        if (id != p.id) continue;
        Preset o = p;
        if (id == "custom") {
            o.top = uint32_t(cfg.customTop) & 0xFFFFFF;
            o.horizon = uint32_t(cfg.customHorizon) & 0xFFFFFF;
            o.aurora  = cfg.customAurora;
            o.aurora1 = uint32_t(cfg.customAurora1) & 0xFFFFFF;
            o.aurora2 = uint32_t(cfg.customAurora2) & 0xFFFFFF;
        }
        return o;
    }
    return std::nullopt;
}

// [BF] where a sky's signature sits (deg az, the shader's / dirOf convention): the dome turns this to the player
float frontAz(Preset const& p) {
    std::string_view const id = p.id;
    if (id == "prism") return 200; // [LyricsForge] the rainbow (opposite the sun) is the signature
    if (auto b = body(p)) return float(b->az);
    struct F { char const* id; float az; };
    static constexpr F kF[] = {{"warp", 0}, {"pulsar", 30}, {"supernova", 200}, {"eclipse", 160}, {"rift", 180}, {"polar", 0}, {"galaxy", 190},
                               {"ocean", 180}, {"ice", 0}, {"purple", 185}, {"sunset", 30}, {"cherry", 0}, {"crimson", 0},
                               {"thunder", 0}, {"astral", 200}, {"eclipse_heart", 160}, {"dream_ocean", 0}, {"meteor", 0}, {"event_horizon", 170}, {"deep_voyage", 180}, {"mirror_sky", 180}, {"lake_sunset", 180}, {"lupine_sunset", 182}};
    for (auto const& f : kF) if (id == f.id) return f.az;
    return p.painted() ? 0.0f : 180.0f;
}

std::optional<Body> body(Preset const& p) {
    std::string_view id = p.id;
    if (id == "summer") return Body{150, 52, 2.4, 0xFFF6E0, BodyKind::Sun};
    if (id == "dawn") return Body{110, 9, 3.0, 0xFFD8B8, BodyKind::Sun};
    if (id == "dusk") return Body{200, 4, 3.4, 0xFFB070, BodyKind::Sun};
    if (id == "silver_lining") return Body{160, 30, 2.6, 0xFFF0D8, BodyKind::Sun};
    if (id == "bright_night") return Body{200, 32, 2.6, 0xEAF0FF, BodyKind::Moon};
    if (id == "synthwave") return Body{180, 17, 10, 0xFFD36B, BodyKind::Synth}; // [BF] higher: its striped half clears the trees
    if (id == "golden_hour") return Body{200, 4, 3.6, 0xFFC27A, BodyKind::Sun};
    if (id == "cotton_candy") return Body{120, 38, 2.4, 0xFFF4E0, BodyKind::Sun};
    if (id == "sakura") return Body{60, 9, 2.6, 0xFFE2EE, BodyKind::Sun};
    if (id == "cosmos") return Body{60, 28, 4.2, 0xE8E4F2, BodyKind::Moon};
    if (id == "black_hole") return Body{165, 24, 5.2, 0xFFB45A, BodyKind::Eclipse};
    if (id == "blood_moon") return Body{200, 22, 4.4, 0xFF4038, BodyKind::Moon};
    if (id == "frost") return Body{200, 18, 2.4, 0xFFF6EA, BodyKind::Sun};        // [LyricsForge] polar day: halo + sundogs
    if (id == "prism") return Body{20, 16, 2.2, 0xFFF4E0, BodyKind::Sun};         // behind you: the rainbow is in front
    if (id == "ocean") return Body{150, 48, 2.4, 0xFFF6E0, BodyKind::Sun};
    if (id == "dream_ocean") return Body{120, 8, 3.0, 0xFFE0D0, BodyKind::Sun};
    if (id == "sunset") return Body{200, 2, 4.0, 0xFFA050, BodyKind::Sun};
    if (id == "crimson") return Body{200, 2, 4.2, 0xFF6040, BodyKind::Sun};
    if (id == "cherry") return Body{200, 28, 2.6, 0xFFE0F0, BodyKind::Moon};
    if (id == "ice") return Body{200, 26, 2.4, 0xEAF6FF, BodyKind::Moon};
    if (id == "aquarell") return Body{200, -5, 2.4, 0xFFB080, BodyKind::Sun}; // just set: the clouds high up still catch it
    if (p.moon != 0) return Body{200, 26, 2.4, p.moon, BodyKind::Moon};
    return std::nullopt;
}

uint32_t aurora1(Preset const& p, Config const& cfg) {
    int i = cfg.skyPalette;
    if (i >= 0 && i < int(std::size(kPalettes))) return kPalettes[i][0];
    return p.aurora1 != 0 ? p.aurora1 : p.lyricColor;
}
uint32_t aurora2(Preset const& p, Config const& cfg) {
    int i = cfg.skyPalette;
    if (i >= 0 && i < int(std::size(kPalettes))) return kPalettes[i][1];
    return p.aurora2 != 0 ? p.aurora2 : 0xFFFFFF;
}

Params pack(Preset const& p, uint32_t a1, uint32_t a2, double time, Config const& cfg, float const invRot[9], float domeYaw) {
    float pattern = std::clamp(cfg.skyPattern, 0.2f, 1.5f);
    float lively  = std::clamp(cfg.skyLively, 0.0f, 1.0f);
    float breath  = cfg.skyBreath <= 0.01f
                      ? 1.0f
                      : 0.85f + 0.15f * float(std::sin(time * 0.45 * double(std::max(0.2f, cfg.skyBreath))));
    float auroraOn = cfg.fxAurora ? 1.0f : 0.0f;

    Params o{};
    o[0]   = rgba(p.top, 255);
    o[1]   = rgba(p.horizon, 255);
    o[2]   = rgba(a1, 255);
    o[3]   = rgba(a2, 255);
    auto b = body(p);
    o[4]   = b ? rgba(b->color, int(b->kind) + 1) : Texel{0, 0, 0, 0};
    o[5]   = {uint8_t(b8(pattern / 1.5f)), uint8_t(b8(lively)), uint8_t(b8(breath)), uint8_t(b8(auroraOn))};
    o[6]   = b ? word(pair(b->az * 100.0, (b->el + 90.0) * 100.0)) : Texel{0, 0, 0, 0};
    uint32_t w7 = (b ? (pair(b->radius * 1000.0, 0) & 0xFFFF0000u) : 0u) | uint32_t(p.ordinal) << 8;
    o[7]   = word(w7);
    float const dy = float(std::remainder(double(domeYaw), 360.0)) / 360.0f; // [BF] -0.5..0.5 turns
    float m[10] = {invRot[0], invRot[1], invRot[2], invRot[3], invRot[4], invRot[5], invRot[6], invRot[7], invRot[8], dy};
    for (int i = 0; i < 5; ++i) o[8 + i] = word(pair(enc(m[i * 2]), enc(m[i * 2 + 1])));
    return o;
}

namespace {
struct JavaRandom { // java.util.Random
    uint64_t seed;
    explicit JavaRandom(int64_t s) : seed((uint64_t(s) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1)) {}
    int   next(int bits) { seed = (seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1); return int(int64_t(seed) >> (48 - bits)); }
    float nextFloat() { return float(next(24)) / float(1 << 24); }
};
constexpr double kPiD = 3.14159265358979323846;
} // namespace

std::vector<Star> const& stars() {
    static std::vector<Star> const v = [] {
        JavaRandom        r(1337);
        std::vector<Star> o(1300);
        for (auto& s : o) {
            s.az     = r.nextFloat() * 360.0f;
            s.el     = float(std::asin(0.04 + double(r.nextFloat()) * 0.96) * 180.0 / kPiD);
            s.size   = r.nextFloat() < 0.1f ? 1.8f : 1.0f;
            s.bright = 0.35f + r.nextFloat() * 0.65f;
            s.phase  = r.nextFloat() * 6.28f;
            double a = double(s.az) * kPiD / 180.0, e = double(s.el) * kPiD / 180.0;
            s.x = float(-std::sin(a) * std::cos(e)); s.y = float(std::sin(e)); s.z = float(std::cos(a) * std::cos(e));
        }
        return o;
    }();
    return v;
}

void drawStars(Preset const& p, Config const& cfg, int quality, float alpha, double time, float const fwd[3],
               std::vector<StarDraw>& out) {
    static float const kQ[3] = {0.4f, 0.65f, 1.0f};
    auto const&        all   = stars();
    int   n    = int(std::min(float(all.size()), 520.0f * p.stars * kQ[std::clamp(quality, 0, 2)]));
    float live = cfg.skyLively;
    for (int i = 0; i < n; ++i) {
        auto const& s = all[size_t(i)];
        if (double(s.x) * fwd[0] + double(s.y) * fwd[1] + double(s.z) * fwd[2] < 0.05) continue;
        double ph = double(s.phase);
        float  tw = float(0.75 + 0.25 * std::sin(time * (0.8 + ph * 0.2) + ph));
        float  a  = s.bright * tw * alpha * std::min(1.0f, s.el / 10.0f);
        float  k  = live * float(std::pow(0.5 + 0.5 * std::sin(time * (1.3 + ph * 0.4) + double(s.phase * 3.0f)), 3.0)) +
                  (1 - live) * 0.5f;
        out.push_back({s.x, s.y, s.z, float(0.0016 * 1.7 * double(s.size)), a * (0.25f + 0.75f * k)});
    }
}

namespace {
float ease(float v, float target, double dt, double speed) { // Anim.get(): v += (t - v) * (1 - e^(-speed dt))
    return v + (target - v) * float(1.0 - std::exp(-speed * dt));
}
} // namespace

float nightOf(long long dayTime) {
    long long t = ((dayTime % 24000) + 24000) % 24000;
    if (t >= 13000 && t <= 23000) return 1;
    if (t > 12000 && t < 13000) return float(t - 12000) / 1000.0f;
    if (t > 23000) return 1 - float(t - 23000) / 1000.0f;
    return 0;
}

Frame Clock::tick(Preset const* p, Config const& cfg, In const& in) {
    double dt = std::min(0.1, std::max(0.0, in.dtSec));
    mTime += dt * std::clamp(cfg.skySpeed, 0.1f, 3.0f) * std::clamp(in.rate, -1.5, 2.5);
    mBreath += dt * cfg.skyBreath * 1.3;
    mVisible = ease(mVisible, p ? 1.0f : 0.0f, dt, 3.0);
    mOutside = ease(mOutside, in.skyOpen ? 1.0f : 0.0f, dt, 2.0);
    mPic     = ease(mPic, in.picture ? 1.0f : 0.0f, dt, 2.5);
    std::string id = p ? p->id : "";
    if (id != mLast) { // [LyricsForge] sky to sky: the shader sweeps the new one in (no fade through the vanilla sky)
        mLiveFade = (!mLast.empty() && !id.empty()) ? 1.0f : 0.0f;
        mLast = id;
    }
    mLiveFade = std::min(1.0f, mLiveFade + float(dt / 0.9));

    Frame f;
    if (cfg.fxSkyMusic && in.playing) {
        f.beat = in.beat;
        mCalm  = ease(mCalm, in.lineActive ? 1.0f : 0.7f, dt, 1.5);
    } else {
        mCalm = 1;
    }
    f.bright     = (!cfg.skyDay && cfg.fxDayNight) ? 0.35f + 0.65f * in.night : 1.0f;
    f.visible    = mVisible;
    f.outside    = mOutside;
    f.picture    = mPic;
    f.picBright  = cfg.fxDayNight ? 0.45f + 0.55f * (1 - in.night) : 1.0f;
    f.alpha      = mVisible * outCubic(mLiveFade);
    // [BF] a scene change: the new sky emerges from a soft burst of light instead of popping in
    float const burst = mLiveFade < 1 ? std::sin(3.14159265f * mLiveFade) * 0.6f : 0.0f;
    f.brightness = f.bright * (0.92f + 0.08f * mCalm) * (1 + 0.03f * f.beat) /* [LyricsForge] was 0.12: the dome blinked on every beat */ * (1 + burst);
    f.time       = mTime;
    f.t          = float(mTime * 2.2);
    return f;
}

} // namespace bf::lyrics::sky
