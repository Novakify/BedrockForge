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
}

void Pulse::emit(In const& in, std::vector<BfGlyphV1>& out) const {
    if (mOn < 0.01f) return;
    MusicState const* m = in.music;
    float const bass = m ? m->bass : 0, mid = m ? m->mid : 0, treble = m ? m->treble : 0, energy = m ? m->energy : 0.5f;
    uint32_t const col = toRgba(in.color | 0xFF000000u);
    uint32_t const tip = toRgba(0xFF000000u | (((((in.color >> 16) & 0xFF) * 2 + 255) / 3) << 16) |
                                (((((in.color >> 8) & 0xFF) * 2 + 255) / 3) << 8) | (((in.color & 0xFF) * 2 + 255) / 3));
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
    int const seg = std::max(32, int(72 * in.density));
    for (auto const& r : mRings) {
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
    // the horizon equaliser: thin standing bars all around you, heights from the bands
    if (!in.bars) return;
    int const bars = std::max(40, int(120 * in.density));
    double const t = in.now;
    for (int i = 0; i < bars; ++i) {
        double const az = i * 6.2831853 / bars;
        float const swell  = float(0.5 + 0.5 * std::sin(az * 3.0 + t * 0.9));               // bass: slow, wide
        float const ripple = float(0.5 + 0.5 * std::sin(az * 11.0 - t * 3.1));              // mids
        float const sparkle = float(0.5 + 0.5 * std::sin(az * 29.0 + t * 7.3 + i * 0.37));  // treble: fast, fine
        float h;
        if (m && m->hasSpec) { // the real spectrum: two mirrored copies around you, lows in front and behind
            double const rel = std::fmod(az - (90.0 + double(in.camYaw)) * 3.14159265 / 180.0 + 6.2831853 * 4, 3.14159265);
            float const u = float(std::fabs(rel / 3.14159265 * 2.0 - 1.0));      // 0 (front / back) .. 1 (sides)
            float const fb = u * 15.0f;
            int const b0 = std::clamp(int(fb), 0, 15), b1 = std::min(15, b0 + 1);
            float const v = m->spec[b0] + (m->spec[b1] - m->spec[b0]) * (fb - float(b0));
            float const lift = std::clamp((v - 0.25f) / 0.6f, 0.0f, 1.0f);       // the floor of the mix stays low
            h = kHorizon * (0.03f + 0.30f * lift * lift) * (0.8f + 0.4f * mFlare);
        } else
            h = kHorizon * (0.05f + 0.22f * bass * swell + 0.12f * mid * ripple + 0.08f * treble * sparkle) *
                (0.6f + 0.5f * energy + 0.6f * mFlare);
        Vec3 const base{std::cos(az) * kHorizon, -kEye + 1.0, std::sin(az) * kHorizon};
        Vec3 const up{0, h * 0.5, 0};
        Vec3 const side = Vec3{-std::sin(az), 0, std::cos(az)} * double(kHorizon * 3.14159 / bars * 0.16);
        float const a = mOn * (0.30f + 0.35f * bass + 0.35f * mFlare);
        // [BF] audit: flat solid posts (every 6th white) read as a fence. Light, not objects: each bar is a beam that is
        // brightest at its tip and fades into the ground, all one colour.
        BfGlyphV1 q{};
        Vec3 const p = base + up;
        q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
        Vec3 const sd = side * 0.8;
        q.right[0] = float(sd.x); q.right[1] = float(sd.y); q.right[2] = float(sd.z);
        q.up[0] = float(up.x); q.up[1] = float(up.y); q.up[2] = float(up.z);
        q.fill = tip; q.fill2 = col & 0x00FFFFFFu;
        q.alpha = std::min(1.0f, a); q.outlineW = 0.15f; q.glowW = 3.0f;
        q.uv[2] = q.uv[3] = 1.0f; q.flags = BF_GLYPH_SOLID | BF_GLYPH_GRADIENT;
        if (q.alpha > 0.004f) out.push_back(q);
    }
}

} // namespace bf::lyrics
