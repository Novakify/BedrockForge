// LyricsForge - [BF] corner music player. Original code for Bedrock Forge; see PlayerHud.h.
#include "lyrics/core/PlayerHud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "lyrics/core/Text.h"
#include "lyrics/core/TopBar.h"

namespace bf::lyrics {

namespace {
std::string fit(FontSet const& f, std::string const& font, std::string s, float maxEm) {
    if (f.width(s, font) <= maxEm) return s;
    auto cps = text::codepoints(s);
    while (!cps.empty()) {
        cps.pop_back();
        std::string t;
        for (uint32_t c : cps) text::append(t, c);
        t = text::trimmed(t) + "\xE2\x80\xA6";
        if (f.width(t, font) <= maxEm) return t;
    }
    return "\xE2\x80\xA6";
}
std::string mmss(int64_t ms) {
    int64_t const s = std::max<int64_t>(0, ms / 1000);
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", int(s / 60), int(s % 60));
    return b;
}
} // namespace

std::string const& PlayerHud::fitCached(FontSet const& f, std::string const& font, std::string const& s, float maxEm) {
    for (auto& x : mFits)
        if (x.in == s && x.font == font && std::fabs(x.maxEm - maxEm) < 0.01f) return x.out;
    if (mFits.size() >= 12) mFits.erase(mFits.begin());
    mFits.push_back({font, s, fit(f, font, s, maxEm), maxEm});
    return mFits.back().out;
}

void PlayerHud::build(SceneInput const& in, Extra const& ex, Config const& c, FontSet const& fonts, std::vector<BfGlyphV1>& out) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    bool const want = c.enabled && c.hudPlayer && !in.title.empty();
    mVis = anim::approach(mVis, want ? 1.0f : 0.0f, want ? 6.0f : 5.0f, dt);
    if (mVis < 0.01f) return;
    float const a = anim::easeOutCubic(mVis) * (0.35f + 0.65f * in.focusA);
    if (in.songSeq != mSeq) { mSeq = in.songSeq; mPos = float(std::max<int64_t>(0, in.t)); }
    mPos += (float(std::max<int64_t>(0, in.t)) - mPos) * std::min(1.0f, dt * 10.0f);

    // ---- geometry in GUI px, then screen-height units (s = 1 / guiH)
    float const k = std::clamp(c.hudPlayerScale, 0.5f, 2.0f), H = std::max(1.0f, in.guiH), W = in.guiW, s = 1.0f / H;
    bool const  round = c.hudRound;
    std::string const font = round ? std::string("outfit") : std::string("minecraft");
    std::string const bold = round ? std::string("outfit_bold") : std::string("minecraft");
    bool const  lyr = c.hudPlayerLyrics && in.lyrics && !in.lyrics->lines.empty();
    float const w = 150 * k, coverS = 34 * k, pad = 6 * k;
    float const h = coverS + 2 * pad + (lyr ? 34 * k : 0);
    float const x = std::clamp(c.hudPlayerX, 0.0f, 1.0f) * std::max(0.0f, W - w), y = std::clamp(c.hudPlayerY, 0.0f, 1.0f) * std::max(0.0f, H - h);
    Theme const th = currentTheme(c);
    uint32_t const acc = c.hudColorFromCover && ex.coverAccent ? (0xFF000000u | ex.coverAccent) : (0xFF000000u | th.a);
    float const rad = round ? 0.18f : 0.0f;

    auto rect = [&](float rx, float ry, float rw, float rh, uint32_t col, float al, float r = 0.0f, float soft = 0.0f) {
        out.push_back(solidRect(rx * s, ry * s, rw * s, rh * s, col, al, r, soft));
    };
    auto text = [&](std::string const& t, std::string const& f, float left, float top, float size, uint32_t col, float al) {
        if (t.empty()) return;
        float const wpx = fonts.width(t, f) * size;
        DrawWord d;
        d.text = t; d.font = f; d.screen = true; d.plain = true; d.argb = col;
        d.origin = {double((left + wpx / 2) * s), double((top + size / 2) * s), 0};
        d.scale = size * s; d.alpha = al;
        if (Font const* ff = fonts.get(f)) buildWordGlyphs(d, *ff, c, makeStyle(c, 0), {}, out);
    };

    // card: shadow, body, rim
    rect(x - 2 * k, y - 1 * k, w + 4 * k, h + 4 * k, 0xFF000000u, 0.35f * a, round ? 0.22f : 0.0f, 5 * k);
    rect(x, y, w, h, 0xF0141221u, 0.82f * a, rad);
    rect(x, y, w, 1.0f * k, acc, 0.55f * a, 0.0f);
    // cover (or a vinyl dot)
    float const cx = x + pad, cy = y + pad;
    if (ex.coverAtlas >= 0) {
        BfGlyphV1 q = solidRect(cx * s, cy * s, coverS * s, coverS * s, 0xFFFFFFFFu, a, round ? 0.16f : 0.0f);
        q.flags = BF_GLYPH_SCREEN | BF_GLYPH_IMAGE;
        q.atlas = uint32_t(ex.coverAtlas);
        q.outlineW = round ? 0.16f : 0.0f;
        out.push_back(q);
    } else {
        rect(cx, cy, coverS, coverS, 0xFF2A2340u, a, round ? 1.0f : 0.0f);
        rect(cx + coverS * 0.35f, cy + coverS * 0.35f, coverS * 0.3f, coverS * 0.3f, acc, a, 1.0f);
    }
    // title / artist
    float const tx = cx + coverS + pad, tw = w - (tx - x) - pad - 14 * k;
    float const ts = 8.2f * k, as = 6.6f * k;
    text(fitCached(fonts, bold, in.title, tw / ts), bold, tx, cy + 1 * k, ts, 0xFFFFFFFFu, a);
    text(fitCached(fonts, font, in.artist, tw / as), font, tx, cy + 11.5f * k, as, 0xFFB9B3D6u, a);
    // equalizer (5 bars, beat-driven)
    float const bx = x + w - pad - 12 * k, by = cy + 10 * k;
    for (int i = 0; i < 5; ++i) {
        float const target = in.playing ? 0.25f + 0.75f * std::clamp(in.beat * (0.6f + 0.4f * std::sin(float(in.nowSeconds) * (5 + i) + i)), 0.0f, 1.0f)
                                         : 0.12f;
        mBars[i] += (target - mBars[i]) * std::min(1.0f, dt * 14.0f);
        float const bh = 2 * k + 9 * k * mBars[i];
        rect(bx + float(i) * 2.6f * k, by - bh, 1.8f * k, bh, acc, 0.9f * a, 0.0f);
    }
    // time bar
    float const barY = cy + coverS - 8 * k;
    float const dur = float(std::max<int64_t>(1, ex.durationMs)), p = ex.durationMs > 0 ? std::clamp(mPos / dur, 0.0f, 1.0f) : 0.0f;
    rect(tx, barY, tw + 12 * k, 2.2f * k, 0xFF3A3450u, a, round ? 1.0f : 0.0f);
    rect(tx, barY, (tw + 12 * k) * p, 2.2f * k, acc, a, round ? 1.0f : 0.0f);
    if (round) rect(tx + (tw + 12 * k) * p - 2.2f * k, barY - 1.1f * k, 4.4f * k, 4.4f * k, 0xFFFFFFFFu, a, 1.0f);
    float const tms = 5.6f * k;
    text(mmss(int64_t(mPos)), font, tx, barY + 3.5f * k, tms, 0xFF9A93B5u, a);
    if (ex.durationMs > 0) {
        std::string const d = mmss(ex.durationMs);
        text(d, font, tx + tw + 12 * k - fonts.width(d, font) * tms, barY + 3.5f * k, tms, 0xFF9A93B5u, a);
    }
    // the line being sung and the next two
    if (lyr) {
        Line const* cur = in.lyrics->lineAt(in.t);
        if (!cur) cur = in.lyrics->lastStartedBefore(in.t);
        std::vector<Line const*> rows;
        bool seen = cur == nullptr;
        for (auto const& ln : in.lyrics->lines) {
            if (ln.index < 0) continue;
            if (!seen) { if (&ln == cur) { seen = true; rows.push_back(&ln); } continue; }
            rows.push_back(&ln);
            if (rows.size() >= 3) break;
        }
        float ly = cy + coverS + pad + 1 * k;
        for (size_t i = 0; i < rows.size(); ++i) {
            float const sz = i == 0 ? 7.6f * k : 6.4f * k;
            uint32_t const col = i == 0 ? acc : 0xFF9A93B5u;
            std::string const t = fitCached(fonts, i == 0 ? bold : font, show(rows[i]->text, c.allCaps), (w - 2 * pad) / sz);
            text(t, i == 0 ? bold : font, x + pad, ly, sz, i == 0 ? colLerp(col, 0xFFFFFFFFu, 0.35f) : col, a * (i == 0 ? 1.0f : 0.8f - 0.2f * float(i)));
            ly += sz + 3.2f * k;
        }
    }
}

} // namespace bf::lyrics
