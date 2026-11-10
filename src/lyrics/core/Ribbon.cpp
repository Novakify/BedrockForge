// LyricsForge - [BF] word ribbon. Original code for Bedrock Forge; see Ribbon.h.
#include "lyrics/core/Ribbon.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Scene.h"

namespace bf::lyrics {

void Ribbon::push(Pt const& p) {
    if (!mPts.empty()) {
        Pt const& b = mPts.back();
        if (b.line == p.line && b.screen == p.screen && (b.pos - p.pos).length() < 1e-4) { mPts.back().t = p.t; return; }
    }
    mPts.push_back(p);
    while (mPts.size() > 160) mPts.pop_front();
}

void Ribbon::step(std::vector<DrawWord> const& words, WidthFn const& width, Vec3 origin, double now, float energy, bool enabled) {
    mNow    = now;
    mEnergy = energy;
    while (!mPts.empty() && now - mPts.front().t > kTail) mPts.pop_front();
    if (!enabled) return;
    DrawWord const* cur = nullptr; // the word being sung that started last
    for (auto const& w : words)
        if (w.sung && !w.plain && w.sungMs >= 0 && w.durMs > 0 && w.alpha > 0.05f && (!cur || w.sungMs < cur->sungMs)) cur = &w;
    if (!cur) return;
    double const total = double(width(cur->text, cur->font));
    double const p     = std::clamp(double(cur->sungMs) / double(cur->durMs), 0.0, 1.0);
    double const lx    = -total / 2 + total * p, ly = -0.62;
    Pt pt;
    pt.pos    = wordPoint(*cur, lx, ly, origin);
    Vec3 const above = wordPoint(*cur, lx, ly + 0.09, origin);
    pt.up     = above - pt.pos;   // 0.09 em "up" in output space = half the ribbon thickness
    pt.t      = now;
    pt.line   = cur->key / 1000;
    pt.screen = cur->screen;
    pt.alpha  = cur->alpha;
    push(pt);
}

void Ribbon::emit(uint32_t argb, float glow, std::vector<BfGlyphV1>& out) const {
    if (mPts.size() < 2) return;
    uint32_t const rgba = toRgba(argb | 0xFF000000u);
    for (size_t i = 1; i < mPts.size(); ++i) {
        Pt const& a = mPts[i - 1];
        Pt const& b = mPts[i];
        if (a.line != b.line || a.screen != b.screen) continue; // a new line starts its own ribbon
        float const age = float(mNow - b.t), k = std::clamp(1.0f - age / float(kTail), 0.0f, 1.0f);
        if (k <= 0.01f) continue;
        Vec3 const mid = (a.pos + b.pos) * 0.5, half = (b.pos - a.pos) * 0.5;
        double const len = half.length();
        if (len < 1e-6) continue;
        Vec3 const up = (a.up + b.up) * 0.5 * double(0.5 + 0.5 * k) * double(1.0f + 0.8f * mEnergy);
        BfGlyphV1 q{};
        q.pos[0] = float(mid.x); q.pos[1] = float(mid.y); q.pos[2] = float(mid.z);
        Vec3 const r = half + half * (std::min(len, up.length()) / len); // overlap neighbours a little (no gaps)
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(up.x); q.up[1] = float(up.y); q.up[2] = float(up.z);
        q.fill = q.fill2 = rgba;
        q.alpha    = std::min(1.0f, (0.5f + 0.5f * glow) * k * b.alpha);
        q.outlineW = 1.0f;
        q.glowW    = 2.0f;
        q.uv[2] = q.uv[3] = 1.0f;
        q.flags = BF_GLYPH_SOLID | (b.screen ? BF_GLYPH_SCREEN : 0u);
        out.push_back(q);
    }
    // bright head
    Pt const& h = mPts.back();
    if (mNow - h.t < 0.08) {
        BfGlyphV1 q{};
        q.pos[0] = float(h.pos.x); q.pos[1] = float(h.pos.y); q.pos[2] = float(h.pos.z);
        Vec3 const u = h.up * 2.2;
        Vec3 r = u;                       // a round dot: right = up rotated in the plane (approx: same length)
        if (h.screen) r = Vec3{-u.y, u.x, 0};
        else if (mPts.size() >= 2) { Vec3 d = (h.pos - mPts[mPts.size() - 2].pos); if (d.length() > 1e-6) r = d.normalized() * u.length(); }
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
        q.fill = q.fill2 = toRgba(0xFFFFFFFFu);
        q.alpha    = std::min(1.0f, 0.8f * h.alpha);
        q.outlineW = 1.0f;
        q.glowW    = 2.5f;
        q.uv[2] = q.uv[3] = 1.0f;
        q.flags = BF_GLYPH_SOLID | (h.screen ? BF_GLYPH_SCREEN : 0u);
        out.push_back(q);
    }
}

} // namespace bf::lyrics
