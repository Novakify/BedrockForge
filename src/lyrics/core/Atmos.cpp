// LyricsForge - [BF] music-reactive atmosphere. Original code for Bedrock Forge; see Atmos.h.
#include "lyrics/core/Atmos.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Conductor.h"
#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {

float Atmos::rnd() {
    mRng ^= mRng << 13; mRng ^= mRng >> 17; mRng ^= mRng << 5;
    return float(mRng & 0xFFFFFF) / float(0xFFFFFF);
}

void Atmos::respawn(Mote& m, Vec3 const& cam, bool anywhere) {
    double const r  = anywhere ? 2.5 + 10.0 * std::sqrt(rnd()) : kRadius - 1.0;
    double const az = rnd() * 6.2831853, el = (rnd() - 0.35) * 1.4;
    m.pos   = cam + Vec3{std::cos(az) * std::cos(el), std::sin(el) * 0.6 + 0.3, std::sin(az) * std::cos(el)} * r;
    m.vel   = Vec3{(rnd() - 0.5) * 0.25, 0.04 + rnd() * 0.12, (rnd() - 0.5) * 0.25};
    m.size  = 0.022f + 0.03f * rnd();  // [LyricsForge] half size, blocks: fine specks (the soft halo makes them read)
    m.phase = rnd() * 6.2831853f;
    m.seed  = rnd();
}

void Atmos::step(In const& in) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    bool const  on = in.enabled && in.music && in.music->active;
    mOn += ((on ? 1.0f : 0.0f) - mOn) * (1.0f - std::exp(-dt / 1.2f));
    if (mOn < 0.002f && !on) { mMotes.clear(); return; }
    int const want = std::max(8, int(float(kCount) * std::clamp(in.density, 0.25f, 1.0f)));
    while (int(mMotes.size()) < want) { Mote m; respawn(m, in.camPos, true); mMotes.push_back(m); }
    if (int(mMotes.size()) > want) mMotes.resize(size_t(want));
    MusicState const* ms = in.music;
    float const energy = ms ? ms->energy : 0.0f, ts = ms ? std::clamp(ms->timeScale, 0.25f, 2.5f) : 1.0f;
    float const push = ms ? ms->impact : 0.0f;
    for (auto& m : mMotes) {
        Vec3 const away = m.pos - in.camPos;
        double const d  = away.length();
        if (d > kRadius + 1.5) { respawn(m, in.camPos, false); continue; } // walked away: it reappears ahead / around
        // drift, a little faster with the energy; a drop blows them outwards for a moment
        Vec3 swirl{std::sin(in.now * 0.4 + m.seed * 9.0) * 0.08, 0, std::cos(in.now * 0.37 + m.seed * 7.0) * 0.08};
        Vec3 v = (m.vel + swirl) * double((0.5f + energy) * ts);
        if (push > 0.05f && d > 1e-3) v += away * (1.0 / d) * double(push * 1.6f);
        m.pos += v * double(dt);
    }
}

void Atmos::emit(In const& in, Vec3 const& origin, std::vector<BfGlyphV1>& out) const {
    if (mMotes.empty() || mOn < 0.01f) return;
    MusicState const* ms = in.music;
    float const treble = ms ? ms->treble : 0, onset = ms ? ms->onset : 0, impact = ms ? ms->impact : 0, budget = ms ? ms->budget : 0.5f;
    double const y = rad(in.camYaw), p = rad(in.camPitch);
    Vec3 const f{-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
    Vec3 const r = Vec3{-f.z, 0, f.x}.normalized();
    Vec3 const u = r.cross(f).normalized();
    // [LyricsForge] a bright speck (the tint pushed toward white) inside a soft halo of the tint: dust catching light,
    // not flat discs
    auto mixW = [](uint32_t c, float k) {
        auto ch = [&](int s) { return uint32_t(float((c >> s) & 255) + (255.0f - float((c >> s) & 255)) * k) << s; };
        return ch(16) | ch(8) | ch(0);
    };
    uint32_t const rgba = toRgba(mixW(in.color & 0xFFFFFF, 0.55f) | 0xFF000000u);
    uint32_t const halo = toRgba((in.color & 0xFFFFFF) | 0xFF000000u);
    for (auto const& m : mMotes) {
        Vec3 const rel = m.pos - in.camPos;
        double const d = rel.length();
        if (d < 1.2 || rel.dot(f) < 0) continue;                         // too close / behind
        float const edge = float(std::clamp((kRadius - d) / 3.0, 0.0, 1.0)) * float(std::clamp((d - 1.2) / 1.5, 0.0, 1.0));
        float const tw   = 0.55f + 0.45f * std::sin(float(in.now) * (2.0f + 3.0f * m.seed) + m.phase);
        float const lvl  = (0.28f + 0.55f * treble * tw + 0.35f * onset * m.seed + 0.6f * impact) * (0.6f + 0.6f * budget);
        float const a    = std::clamp(lvl, 0.0f, 0.9f) * edge * mOn;
        if (a < 0.01f) continue;
        float const s = m.size * (1.0f + 0.6f * treble + 0.8f * impact);
        Vec3 const c = m.pos - origin;
        auto put = [&](float sz, uint32_t col, float alpha, float soft) {
            BfGlyphV1 q{};
            q.pos[0] = float(c.x); q.pos[1] = float(c.y); q.pos[2] = float(c.z);
            q.right[0] = float(r.x * sz); q.right[1] = float(r.y * sz); q.right[2] = float(r.z * sz);
            q.up[0] = float(u.x * sz); q.up[1] = float(u.y * sz); q.up[2] = float(u.z * sz);
            q.fill = q.fill2 = col;
            q.alpha    = alpha;
            q.outlineW = 1.0f;  // round
            q.glowW    = soft;  // soft edge, px
            q.uv[2] = q.uv[3] = 1.0f;
            q.flags = BF_GLYPH_SOLID;
            out.push_back(q);
        };
        put(s * 4.0f, halo, a * 0.22f, 14.0f); // the halo
        put(s, rgba, std::min(1.0f, a * 1.2f), 2.0f); // the speck
    }
}

} // namespace bf::lyrics
