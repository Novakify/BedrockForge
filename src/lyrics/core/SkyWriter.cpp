// LyricsForge - [BF] "The sky writes the song". Original code for Bedrock Forge; see SkyWriter.h.
#include "lyrics/core/SkyWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {

namespace {
std::string norm(std::string_view s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(uint8_t(c))) o += char(std::tolower(uint8_t(c)));
    return o;
}
float hash01(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return float(x & 0xFFFFFF) / float(0xFFFFFF);
}
float easeOut(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
} // namespace

bool isHookLine(Lyrics const& l, Line const& line) {
    std::string const k = norm(line.text);
    if (k.size() < 6) return false;
    int n = 0;
    for (auto const& x : l.lines)
        if (x.index >= 0 && norm(x.text) == k && ++n >= 2) return true;
    return false;
}

void SkyWriter::step(In const& in) {
    float const dt = std::clamp(in.dt, 0.0f, 0.25f);
    if (in.songSeq != mSeq) { // the song changed: the old sky dissolves
        for (auto& c : mCons) mDying.push_back(std::move(c));
        mCons.clear();
        mSeq = in.songSeq;
        mLastLine = -1;
        mSinceWrite = 1e9f;
        mFinale = 0;
        mLastEnd = -1;
        if (in.lyrics)
            for (auto const& l : in.lyrics->lines) if (l.index >= 0) mLastEnd = std::max(mLastEnd, l.end);
    }
    for (auto& c : mDying) { c.fade -= dt / 3.0f; c.age += dt; }
    mDying.erase(std::remove_if(mDying.begin(), mDying.end(), [](Constellation const& c) { return c.fade <= 0; }), mDying.end());
    for (auto& c : mCons) c.age += dt;
    for (auto& c : mEnd) c.age += dt;
    for (auto& c : mEnd) // the finale has risen and held: it dissolves
        if (c.age > kFinaleHold + (c.stars.empty() ? 0.0f : c.stars.back().delay)) mDying.push_back(std::move(c));
    mEnd.erase(std::remove_if(mEnd.begin(), mEnd.end(), [&](Constellation const& c) { return c.stars.empty() || c.age > kFinaleHold + c.stars.back().delay; }), mEnd.end());
    mSinceWrite += dt;
    if (!in.enabled && !mCons.empty()) { // [BF] switched off: the sky lets them go instead of freezing them
        for (auto& c : mCons) mDying.push_back(std::move(c));
        mCons.clear();
    }
    if (!in.enabled || !in.lyrics || in.t < 0) return;
    if (mLastEnd < 0)
        for (auto const& l : in.lyrics->lines) if (l.index >= 0) mLastEnd = std::max(mLastEnd, l.end);

    // a line just finished? (the line on screen changed)
    Line const* cur = in.lyrics->lineAt(in.t);
    int const   now = cur && cur->index >= 0 ? cur->index : -1;
    if (now != mLastLine) {
        if (mLastLine >= 0 && in.playing)
            for (auto const& l : in.lyrics->lines)
                if (l.index == mLastLine) {
                    bool const hooked = isHookLine(*in.lyrics, l) || (in.music && in.music->inHook > 0.6f);
                    if (hooked && mSinceWrite > 6.0f && int(mCons.size()) < kMaxPerSong) write(l, in);
                    break;
                }
        mLastLine = now;
    }
    // finale: the last lines of the song light every constellation at once
    bool const finale = mLastEnd > 0 && in.t > mLastEnd - 6000 && in.t < mLastEnd + 15000;
    mFinale += ((finale ? 1.0f : 0.0f) - mFinale) * (1.0f - std::exp(-dt / (finale ? 0.6f : 2.0f)));
}

bool SkyWriter::make(Line const& l, float az, float elev, uint32_t seedK, bool letters, Constellation& c) const {
    c.text = l.text;
    std::vector<std::pair<size_t, size_t>> words; // [begin, end) of each word (or letter) in the text
    for (size_t i = 0; i < l.text.size();) {
        while (i < l.text.size() && l.text[i] == ' ') ++i;
        size_t j = i;
        while (j < l.text.size() && l.text[j] != ' ') ++j;
        if (letters) { // [LyricsForge] a short final line: one star per letter
            for (size_t q = i; q < j;) {
                unsigned char const ch = uint8_t(l.text[q]);
                size_t const len = ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
                if (std::isalnum(ch) || ch >= 0x80) words.push_back({q, q + len});
                q += len;
            }
            i = j;
            continue;
        }
        size_t n = 0;
        for (size_t q = i; q < j; ++q) n += std::isalnum(uint8_t(l.text[q])) || uint8_t(l.text[q]) >= 0x80;
        if (n) words.push_back({i, j});
        i = j;
    }
    if (words.size() > (letters ? 12u : 9u)) words.resize(letters ? 12 : 9);
    int const   n    = int(words.size());
    float const step = std::min(letters ? 6.0f : 9.0f, 54.0f / float(std::max(1, n)));
    uint32_t    seed = uint32_t(l.start) * 2654435761u + seedK;
    int         prevStar = -1;
    for (int i = 0; i < n; ++i) {
        size_t const len = letters ? 4 : words[size_t(i)].second - words[size_t(i)].first;
        float const ax  = (float(i) - float(n - 1) * 0.5f) * step + (hash01(seed + uint32_t(i) * 131u) - 0.5f) * step * 0.5f;
        float const e   = elev + (hash01(seed + uint32_t(i) * 97u) - 0.5f) * 9.0f;
        Star s;
        s.dir   = dir(az + ax / std::max(0.3f, std::cos(float(rad(e)))), -e);
        s.delay = 0.12f * float(c.stars.size());
        s.word  = i;
        s.mag   = std::clamp(0.65f + 0.09f * float(len), 0.65f, 1.35f);
        c.stars.push_back(s);
        int const idx = int(c.stars.size()) - 1;
        if (prevStar >= 0) c.links.emplace_back(prevStar, idx);
        prevStar = idx;
        if (!letters && len >= 7) { // a faint companion off to the side, linked as a branch
            Star b = s;
            float const be = e + (hash01(seed + uint32_t(i) * 53u) > 0.5f ? 4.5f : -4.5f);
            b.dir   = dir(az + (ax + step * 0.35f) / std::max(0.3f, std::cos(float(rad(be)))), -be);
            b.delay = s.delay + 0.06f;
            b.mag   = 0.55f;
            c.stars.push_back(b);
            c.links.emplace_back(idx, int(c.stars.size()) - 1);
        }
    }
    return c.stars.size() >= 3;
}

void SkyWriter::write(Line const& l, In const& in) {
    int const k = int(mCons.size());
    static float const azOff[] = {-38, 30, -6, 56, -62, 12, -24};
    Constellation c;
    if (!make(l, in.camYaw + azOff[k % 7], 46.0f - 8.0f * float(k % 3), uint32_t(k), false, c)) return;
    mCons.push_back(std::move(c));
    mSinceWrite = 0;
}

void SkyWriter::finale(Line const& l, In const& in) {
    Constellation c;
    if (!make(l, in.camYaw, 30.0f, 977u, false, c)) { c = {}; if (!make(l, in.camYaw, 30.0f, 977u, true, c)) return; }
    mEnd.clear();
    mEnd.push_back(std::move(c));
}

void SkyWriter::emit(In const& in, Vec3 const& origin, std::vector<BfGlyphV1>& out) const {
    (void)origin;
    MusicState const* m = in.music;
    float const budget = m ? m->budget : 0.5f, treble = m ? m->treble : 0.0f, impact = m ? m->impact : 0.0f;
    Vec3 const  up0{0, 1, 0};
    bool const     lightSky = in.skyLuma > 0.45f; // stars on a pastel sky: coloured cores, dark rings, darker lines
    uint32_t const darkCol  = colDarken(in.color, 0.7f);
    uint32_t const lineCol  = lightSky ? colDarken(in.color, 0.45f) : in.color;
    float const    lineA    = lightSky ? 0.6f : 0.35f;
    auto quad = [&](Vec3 c, Vec3 r, Vec3 u, uint32_t argb, float alpha, float round, float soft) {
        if (alpha <= 0.004f) return;
        BfGlyphV1 q{};
        q.pos[0] = float(c.x); q.pos[1] = float(c.y); q.pos[2] = float(c.z);
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
        q.fill = q.fill2 = toRgba(argb | 0xFF000000u);
        q.alpha = std::min(1.0f, alpha);
        q.outlineW = round;
        q.glowW = soft;
        q.uv[2] = q.uv[3] = 1.0f;
        q.flags = BF_GLYPH_SOLID;
        out.push_back(q);
    };
    auto draw = [&](Constellation const& c, bool dying, bool hero = false) {
        float const life = std::min(1.0f, c.fade) * (dying ? 1.0f : 1.0f);
        float const bright = life * (hero ? 1.25f : 0.55f + 0.25f * budget + 0.35f * impact + 0.7f * mFinale);
        auto& pos = mPos;
        auto& shown = mShown;
        pos.assign(c.stars.size(), Vec3{});
        shown.assign(c.stars.size(), 0.0f);
        for (size_t i = 0; i < c.stars.size(); ++i) {
            auto const& s = c.stars[i];
            float const p = easeOut((c.age - s.delay) / 1.4f); // rises from below into place
            Vec3 d = s.dir;
            if (p < 1) d = (s.dir * double(p) + Vec3{s.dir.x, s.dir.y - 0.45, s.dir.z} * double(1 - p)).normalized();
            pos[i]   = d * double(kRadius);
            shown[i] = p;
        }
        float const R = kRadius;
        for (auto [a, b] : c.links) { // lines draw in once both ends have arrived
            float const lp = easeOut((c.age - std::max(c.stars[size_t(a)].delay, c.stars[size_t(b)].delay) - 1.2f) / 0.8f);
            if (lp <= 0) continue;
            Vec3 const A = pos[size_t(a)], B = A + (pos[size_t(b)] - A) * double(lp);
            Vec3 const mid = (A + B) * 0.5, axis = (B - A) * 0.5;
            Vec3 const n = (mid * -1.0).normalized();
            Vec3 const thick = n.cross(axis.normalized()).normalized() * double(R * 0.0011f);
            quad(mid, axis, thick, lineCol, lineA * bright, 1.0f, 1.5f);
        }
        for (size_t i = 0; i < c.stars.size(); ++i) {
            if (shown[i] <= 0) continue;
            Vec3 const n = (pos[i] * -1.0).normalized();
            Vec3 const r = up0.cross(n).normalized(), u = n.cross(r);
            float const tw = 0.8f + 0.2f * std::sin(float(in.now) * 2.3f + float(i) * 1.7f) +
                             0.35f * treble * hash01(uint32_t(i) * 31u + uint32_t(c.age * 4));
            float const a  = bright * tw * shown[i];
            float const mg = c.stars[i].mag;
            float const rc = R * 0.0048f * mg, rh = R * 0.014f * mg * (1.0f + 0.5f * mFinale);
            // [BF] audit: a coloured core with a dark outline read as a gem sticker on bright skies. A star is a white-hot
            // point in a tinted halo; on a bright sky only a faint soft shadow helps it read.
            if (lightSky) quad(pos[i], r * double(rc * 2.4f), u * double(rc * 2.4f), darkCol, 0.22f * a, 1.0f, 8.0f); // soft shadow
            quad(pos[i], r * double(rh), u * double(rh), in.color, (lightSky ? 0.3f : 0.22f) * a, 1.0f, 10.0f); // halo
            uint32_t const cc = in.color;
            uint32_t const warmWhite = 0xFF000000u | ((((cc >> 16) & 0xFF) + 3 * 255) / 4 << 16) |
                                       ((((cc >> 8) & 0xFF) + 3 * 255) / 4 << 8) | (((cc & 0xFF) + 3 * 255) / 4);
            quad(pos[i], r * double(rc), u * double(rc), warmWhite, a, 1.0f, 2.0f); // core
        }
    };
    for (auto const& c : mCons) draw(c, false);
    for (auto const& c : mEnd) draw(c, false, true);
    for (auto const& c : mDying) draw(c, true);
}

} // namespace bf::lyrics
