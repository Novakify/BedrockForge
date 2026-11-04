// LyricsForge - [BF] musical portals. Original code for Bedrock Forge; see Portal.h.
#include "lyrics/core/Portal.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Conductor.h"
#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {

void Portal::step(In const& in) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    mSince += dt;
    if (mAge >= 0) { mAge += dt; if (mAge > kLife) mAge = -1; }
    MusicState const* m = in.music;
    if (!in.enabled || !m || !m->active) return;
    if (mAge < 0 && mSince > kGap && (m->chorusNow || m->dropNow)) { // opens up and ahead of where you look
        mAge = 0; mSince = 0;
        mYaw = in.camYaw + (std::fmod(float(in.now) * 37.0f, 40.0f) - 20.0f);
        mPitch = std::clamp(in.camPitch - 22.0f, -55.0f, -14.0f);
        mPower = m->dropNow ? 1.0f : 0.75f;
    }
}

void Portal::emit(In const& in, std::vector<BfGlyphV1>& out) const {
    if (mAge < 0) return;
    float const k = mAge / kLife;
    float const open = k < 0.18f ? k / 0.18f : k > 0.75f ? std::max(0.0f, (1.0f - k) / 0.25f) : 1.0f; // open, hold, close
    float const e = open * open * (3 - 2 * open);
    if (e < 0.01f) return;
    Vec3 const c = dir(mYaw, mPitch) * double(kRadius);          // centre (relative to the eye = origin)
    Vec3 const f = c.normalized() * -1.0;                          // facing the player
    Vec3 const r = Vec3{0, 1, 0}.cross(f).normalized(), u = f.cross(r).normalized();
    float const R = kRadius * float(std::tan(rad(15.0 + 5.0 * mPower))) * (0.35f + 0.65f * e);
    MusicState const* m = in.music;
    float const beat = m ? m->beat : 0.0f, energy = m ? m->energy : 0.5f;
    uint32_t const col = toRgba(in.color | 0xFF000000u), white = toRgba(0xFFFFFFFFu);
    auto quad = [&](Vec3 p, Vec3 hr, Vec3 hu, uint32_t rgba, float alpha, float round, float soft) {
        if (alpha <= 0.004f) return;
        BfGlyphV1 q{};
        q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
        q.right[0] = float(hr.x); q.right[1] = float(hr.y); q.right[2] = float(hr.z);
        q.up[0] = float(hu.x); q.up[1] = float(hu.y); q.up[2] = float(hu.z);
        q.fill = q.fill2 = rgba; q.alpha = std::min(1.0f, alpha); q.outlineW = round; q.glowW = soft;
        q.uv[2] = q.uv[3] = 1.0f; q.flags = BF_GLYPH_SOLID;
        out.push_back(q);
    };
    double const spin = double(in.now) * 0.6;
    // the rim: 56 short glowing segments, a second faint ring breathing outside it
    int const N = 56;
    for (int ring = 0; ring < 2; ++ring) {
        float const rr = R * (ring == 0 ? 1.0f : 1.12f + 0.05f * beat);
        for (int i = 0; i < N; ++i) {
            double const a0 = spin * (ring ? -0.5 : 1.0) + i * 6.2831853 / N, a1 = a0 + 6.2831853 / N;
            Vec3 const p0 = c + (r * std::cos(a0) + u * std::sin(a0)) * double(rr), p1 = c + (r * std::cos(a1) + u * std::sin(a1)) * double(rr);
            Vec3 const mid = (p0 + p1) * 0.5, half = (p1 - p0) * 0.55;
            Vec3 const th = f.cross(half.normalized()) * double(R * (ring ? 0.008f : 0.016f) * (1.0f + 0.6f * beat));
            float const flick = 0.75f + 0.25f * float(std::sin(i * 1.7 + in.now * 5.0));
            quad(mid, half, th, ring ? white : col, e * (ring ? 0.5f : 1.0f) * flick, 1.0f, 2.0f);
        }
    }
    // the swirl inside: dots spiralling into the centre
    for (int i = 0; i < 40; ++i) {
        double const t = std::fmod(i * 0.137 + in.now * (0.25 + 0.25 * energy), 1.0);
        double const a = i * 2.39996 + t * 6.0 + spin;
        float const  d = float(1.0 - t) * R * 0.92f;
        Vec3 const p = c + (r * std::cos(a) + u * std::sin(a)) * double(d);
        float const s = R * 0.012f * float(0.5 + t);
        quad(p, r * double(s), u * double(s), i % 3 ? col : white, e * float(0.25 + 0.6 * t), 1.0f, 1.5f);
    }
    // core glow
    quad(c, r * double(R * 0.25f), u * double(R * 0.25f), col, e * (0.12f + 0.2f * beat), 1.0f, 30.0f);
}

} // namespace bf::lyrics
