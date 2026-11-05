// LyricsForge - [BF] the beat takes the stage. Original code for Bedrock Forge; see Pulse.h.
#include "lyrics/core/Pulse.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Conductor.h"
#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {

void Pulse::step(In const& in) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    MusicState const* m = in.music;
    float const want = in.enabled && m && m->active ? m->instrumental : 0.0f;
    mOn += (want - mOn) * (1.0f - std::exp(-dt / (want > mOn ? 0.8f : 0.4f)));
    for (auto& r : mRings) r.age += dt;
    std::erase_if(mRings, [](Ring const& r) { return r.age > kRingLife; });
    mSince += dt;
    float const beat = m ? m->beat : 0.0f;
    // a kick = the beat envelope jumping up (rising edge), at most ~5 per second
    if (mOn > 0.05f && beat > 0.55f && mPrevBeat < 0.45f && mSince > 0.2f && mRings.size() < 8) {
        mRings.push_back({0.0f, std::clamp(0.6f + 0.6f * beat + (m ? 0.4f * m->impact : 0.0f), 0.5f, 1.4f)});
        mSince = 0;
    }
    mPrevBeat = beat;
    if (m && (m->dropNow || m->chorusNow)) mFlare = 1;
    mFlare *= std::exp(-dt / 0.6f);
    // the horizon equaliser: beams of light standing on the skyline all around you, heights from the bands. Drawn
    // by the sky (behind the terrain): as world quads they stood in front of hills and trees like fence posts.
    mBarEl.clear();
    mBarA = 0;
    mBarRgb = in.color & 0xFFFFFF;
    if (!in.bars || mOn < 0.01f) return;
    float const bass = m ? m->bass : 0, mid = m ? m->mid : 0, treble = m ? m->treble : 0, energy = m ? m->energy : 0.5f;
    int const bars = std::max(40, int(72 * in.density)); // [LyricsForge] fewer, softer pillars (120 read as a fence of wires)
    double const t = in.now;
    mBarEl.resize(size_t(bars));
    for (int i = 0; i < bars; ++i) {
        double const az = i * 6.2831853 / bars;
        float const swell  = float(0.5 + 0.5 * std::sin(az * 3.0 + t * 0.9));
        float const ripple = float(0.5 + 0.5 * std::sin(az * 11.0 - t * 3.1));
        float const sparkle = float(0.5 + 0.5 * std::sin(az * 29.0 + t * 7.3 + i * 0.37));
        float h; // fraction of the horizon distance, as before
        if (m && m->hasSpec) { // the real spectrum: two mirrored copies around you, lows in front and behind
            double const rel = std::fmod(az - (90.0 + double(in.camYaw)) * 3.14159265 / 180.0 + 6.2831853 * 4, 3.14159265);
            float const u = float(std::fabs(rel / 3.14159265 * 2.0 - 1.0));
            float const fb = u * 15.0f;
            int const b0 = std::clamp(int(fb), 0, 15), b1 = std::min(15, b0 + 1);
            float const v = m->spec[b0] + (m->spec[b1] - m->spec[b0]) * (fb - float(b0));
            float const lift = std::clamp((v - 0.25f) / 0.6f, 0.0f, 1.0f);
            h = 0.03f + 0.30f * lift * lift * (0.8f + 0.4f * mFlare);
        } else
            h = (0.05f + 0.22f * bass * swell + 0.12f * mid * ripple + 0.08f * treble * sparkle) * (0.6f + 0.5f * energy + 0.6f * mFlare);
        mBarEl[size_t(i)] = 2.5f + 1.5f * float(std::atan(double(h)) * 57.29578); // tall enough to clear a treeline
    }
    mBarA = std::min(1.0f, mOn * (0.45f + 0.35f * bass + 0.35f * mFlare));
}

void Pulse::emit(In const& in, std::vector<BfGlyphV1>& out) const {
    if (mOn < 0.01f) return;
    MusicState const* m = in.music;
    uint32_t const col = toRgba(in.color | 0xFF000000u);
    auto quad = [&](Vec3 p, Vec3 hr, Vec3 hu, uint32_t rgba, float alpha, float soft, float round = 1.0f) {
        if (alpha <= 0.004f) return;
        BfGlyphV1 q{};
        q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
        q.right[0] = float(hr.x); q.right[1] = float(hr.y); q.right[2] = float(hr.z);
        q.up[0] = float(hu.x); q.up[1] = float(hu.y); q.up[2] = float(hu.z);
        q.fill = q.fill2 = rgba; q.alpha = std::min(1.0f, alpha); q.outlineW = round; q.glowW = soft;
        q.uv[2] = q.uv[3] = 1.0f; q.flags = BF_GLYPH_SOLID;
        out.push_back(q);
    };
    // ground rings: a thin, soft pulse of light rolling outward along the ground (reworked after an in-game look: the
    // first version was a thick band with white dashes and read as a cheap hoop). One colour, smooth, faint.
    // [LyricsForge] off: a flat ring at foot height can't follow the terrain (it sliced through blocks and broke into
    // beads on slopes). The beat lives in the sky and the world light instead.
    int const seg = std::max(32, int(72 * in.density));
    if (false) for (auto const& r : mRings) {
        float const k = r.age / kRingLife, e = 1.0f - (1.0f - k) * (1.0f - k);
        float const R = 0.8f + (kRingMax - 0.8f) * e * (0.7f + 0.3f * r.power);
        float const a = mOn * (1.0f - k) * (1.0f - k) * 0.38f * r.power;
        float const w = 0.035f + 0.05f * (1.0f - k);
        for (int i = 0; i < seg; ++i) {
            double const a0 = i * 6.2831853 / seg, a1 = a0 + 6.2831853 / seg;
            Vec3 const p0{std::cos(a0) * R, -kEye, std::sin(a0) * R}, p1{std::cos(a1) * R, -kEye, std::sin(a1) * R};
            Vec3 const mid3 = (p0 + p1) * 0.5, half = (p1 - p0) * 0.52, radial = Vec3{mid3.x, 0, mid3.z}.normalized() * double(w);
            quad(mid3 + Vec3{0, 0.04, 0}, half, radial, col, a, 4.0f);
        }
    }

}

} // namespace bf::lyrics
