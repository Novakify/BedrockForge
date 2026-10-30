// LyricsFlow port - top karaoke bar. Layout/behaviour from LyricsFlow v2.0's TopBar (374m, MIT); see TopBar.h.
#include "lyrics/core/TopBar.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Text.h"

namespace bf::lyrics {

BfGlyphV1 solidRect(float x, float y, float w, float h, uint32_t argb, float alpha, float radius, float softPx) {
    BfGlyphV1 q{};
    q.pos[0]   = x + w * 0.5f;
    q.pos[1]   = y + h * 0.5f;
    q.right[0] = w * 0.5f;
    q.up[1]    = -h * 0.5f; // screen units grow downwards; +up of the quad = towards the top of the screen
    q.uv[0] = 0; q.uv[1] = 0; q.uv[2] = 1; q.uv[3] = 1;
    q.fill = q.fill2 = toRgba(argb);
    q.alpha    = alpha;
    q.outlineW = radius;
    q.glowW    = softPx;
    q.flags    = BF_GLYPH_SCREEN | BF_GLYPH_SOLID;
    return q;
}

namespace {
constexpr uint32_t kBarBg    = 0xF0161321u;
constexpr uint32_t kUpcoming = 0xFFB9B3D6u;
constexpr uint32_t kDimText  = 0xFF9A93B5u;
constexpr uint32_t kStatus   = 0xFFE8E4F5u;

std::string ellipsize(FontSet const& f, std::string const& font, std::string s, float maxEm) {
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
} // namespace

void TopBar::build(SceneInput const& in, Config const& c, FontSet const& fonts, float aspect, std::vector<BfGlyphV1>& out) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    // ---- phases (TopBar events)
    if (in.songSeq != mSeq) { mSeq = in.songSeq; mPhase = Phase::Title; mPhaseAge = 0; mHlInit = false; mScroll = 0; mPlaying = in.playing; }
    if (in.playing != mPlaying) {
        mPhase    = in.playing ? Phase::Resumed : Phase::Paused;
        mPhaseAge = 0;
        mPlaying  = in.playing;
    }
    mPhaseAge += dt;
    mPausedFor = in.playing ? 0 : mPausedFor + dt;
    bool const haveLyrics = in.lyrics && !in.lyrics->lines.empty();
    if (mPhase == Phase::Title && mPhaseAge > 3.2f && haveLyrics) { mPhase = Phase::Lyrics; mPhaseAge = 0; }
    if (mPhase == Phase::Resumed && mPhaseAge > 1.3f) { mPhase = haveLyrics ? Phase::Lyrics : Phase::Title; mPhaseAge = 0; }
    if (mPhase == Phase::Paused && in.playing) { mPhase = Phase::Resumed; mPhaseAge = 0; }

    bool want = !in.title.empty() && c.showTopBar && c.enabled;
    mVis      = anim::approach(mVis, want ? 1.0f : 0.0f, want ? 7.0f : 5.0f, dt);
    if (mVis < 0.01f) return;
    float dim = (!in.playing && mPausedFor > 2.0f) ? 0.6f : 1.0f;
    dim *= 0.25f + 0.75f * in.focusA;
    float const a = anim::easeOutCubic(mVis) * dim;

    // ---- geometry in GUI px (Java units), converted to screen-height units at the end (s = 1 / guiH)
    float const k  = c.topBarScale, W = in.guiW, H = in.guiH, s = 1.0f / std::max(1.0f, H);
    bool const  mc = c.topBarFont == "minecraft";
    float const h = 18 * k, ts = (mc ? 9.0f : 8.6f) * k;
    float const barsW = 14 * k, pad = 7 * k;
    std::string const& font = c.topBarFont;
    Theme const th     = currentTheme(c);
    uint32_t const acc = colLerp(th.a, 0xFF8B5CF6u, 0.45f);

    // content: title / status / karaoke line
    Line const* line = nullptr;
    if (mPhase == Phase::Lyrics && haveLyrics) {
        line = in.lyrics->lineAt(in.t);
        if (!line) {
            Line const* last = in.lyrics->lastStartedBefore(in.t);
            if (last && in.t - last->end < 2500) line = last;
        }
    }
    std::string key = line ? "L" + std::to_string(line->index) : (mPhase == Phase::Paused ? "S" : "T" + in.title);
    if (key != mKey) { mKey = key; mSwap = 0; if (line) { mScroll = 0; mHlInit = false; } }
    mSwap = anim::approach(mSwap, 1, 9, dt);

    float textEm = line ? fonts.width(show(line->text, c.allCaps), font) : fonts.width(in.title + "  \xC2\xB7  " + in.artist, font);
    float maxText = std::min(240 * k, W * 0.55f);
    float targetW = std::max(110 * k, h + std::min(textEm * ts, maxText) + barsW + 2 * pad);
    mWidth        = anim::approach(mWidth, targetW, 8, dt);
    float const x = std::clamp(c.topBarX * W - mWidth / 2, 2.0f, W - mWidth - 2);
    float const y = std::clamp(c.topBarY * H - 10 * (1 - anim::easeOutCubic(mVis)), 1.0f, H - h - 1);
    auto rect = [&](float rx, float ry, float rw, float rh, uint32_t col, float al, float r = 1.0f, float soft = 0.0f) {
        out.push_back(solidRect(rx * s, ry * s, rw * s, rh * s, col, al, r, soft));
    };

    // ---- pill: shadow, body, thin outline
    rect(x - 2.5f * k, y - 1.5f * k, mWidth + 5 * k, h + 5 * k, 0xFF000000u, 0.5f * a * 0.6f, 1.0f, 5 * k);
    rect(x - 0.5f, y - 0.5f, mWidth + 1, h + 1, 0x40FFFFFFu, a);
    rect(x, y, mWidth, h, kBarBg, a);

    // ---- left icon: pause sign / spinning vinyl
    float const icx = x + h / 2 + k, icy = y + h / 2, ir = 0.34f * h;
    if (in.playing) mSpin += dt * 2.4f;
    if (mPhase == Phase::Paused) {
        rect(icx - ir, icy - ir, 2 * ir, 2 * ir, kStatus, a);
        for (float off : {-0.42f, 0.14f}) rect(icx + off * ir, icy - 0.45f * ir, 0.28f * ir, 0.9f * ir, 0xFF161321u, a, 0.3f);
    } else {
        rect(icx - ir, icy - ir, 2 * ir, 2 * ir, colLerp(acc, 0xFFFFFFFFu, 0.25f), a);
        rect(icx - 0.72f * ir, icy - 0.72f * ir, 1.44f * ir, 1.44f * ir, 0xFF2A2340u, a);
        float dx = 0.45f * ir * std::cos(mSpin), dy = 0.45f * ir * std::sin(mSpin);
        rect(icx + dx - 0.16f * ir, icy + dy - 0.16f * ir, 0.32f * ir, 0.32f * ir, 0x90FFFFFFu, a);
        rect(icx - 0.22f * ir, icy - 0.22f * ir, 0.44f * ir, 0.44f * ir, acc, a);
    }

    // ---- text area
    float const tx = x + h + 2 * k, tw = mWidth - h - barsW - pad - 2 * k;
    float const textY = (h - (mc ? ts * 0.78f : ts)) / 2 + (mc ? 0.5f * k : -0.3f * k);
    float const ty    = y + textY + (1 - anim::easeOutCubic(mSwap)) * 6 * k;
    float const ta    = a * anim::easeOutCubic(mSwap);
    size_t const clipFrom = out.size();
    auto word = [&](std::string const& t, float left, uint32_t col) { // left-aligned at GUI x
        if (t.empty()) return;
        float    wpx = fonts.width(t, font) * ts;
        DrawWord d;
        d.text   = t;
        d.font   = font;
        d.screen = true;
        d.plain  = true;
        d.argb   = col;
        d.origin = {double((left + wpx / 2) * s), double((ty + ts / 2) * s), 0};
        d.scale  = ts * s;
        d.alpha  = ta;
        if (Font const* f = fonts.get(font)) buildWordGlyphs(d, *f, c, makeStyle(c, 0), {}, out);
    };
    if (line) { // karaoke
        int    wi = line->wordAt(in.t);
        mWord     = wi;
        float  sp = fonts.width(" ", font) * ts;
        std::vector<float> xs, ws;
        float  total = 0;
        for (auto const& w : line->words) {
            float ww = fonts.width(show(w.text, c.allCaps), font) * ts;
            xs.push_back(total);
            ws.push_back(ww);
            total += ww + sp;
        }
        total = std::max(0.0f, total - sp);
        float target = 0;
        if (wi >= 0 && total > tw) target = std::clamp(xs[size_t(wi)] + ws[size_t(wi)] / 2 - 0.55f * tw, 0.0f, total - tw);
        mScroll = anim::approach(mScroll, target, 6, dt);
        if (wi >= 0) {
            float hx = xs[size_t(wi)] - 2.2f, hw = ws[size_t(wi)] + 4.4f;
            if (!mHlInit) { mHlX = hx; mHlW = hw; mHlInit = true; }
            mHlX = anim::approach(mHlX, hx, 16, dt);
            mHlW = anim::approach(mHlW, hw, 16, dt);
            float px = tx - mScroll + mHlX;
            if (px + mHlW > tx && px < tx + tw) rect(px, ty - 1.3f, mHlW, ts + 2.6f, acc, 0.95f * a, 3.0f / ((ts + 2.6f) * 0.5f));
        }
        for (size_t i = 0; i < line->words.size(); ++i)
            word(show(line->words[i].text, c.allCaps), tx - mScroll + xs[i], (wi >= 0 && int(i) <= wi) ? 0xFFFFFFFFu : kUpcoming);
    } else if (mPhase == Phase::Paused) {
        word(ellipsize(fonts, font, "Paused  \xC2\xB7  " + in.title, tw / ts), tx, kStatus);
    } else {
        std::string title = ellipsize(fonts, font, in.title, tw / ts);
        word(title, tx, 0xFFFFFFFFu);
        float used = fonts.width(title, font) * ts;
        if (used < tw * 0.9f && !in.artist.empty())
            word(ellipsize(fonts, font, "  \xC2\xB7  " + in.artist, (tw - used) / ts), tx + used, kDimText);
    }
    // the Java bar scissors the text area; we drop glyphs whose centre falls outside it
    float const clipL = tx * s, clipR = (tx + tw) * s;
    out.erase(std::remove_if(out.begin() + std::ptrdiff_t(clipFrom), out.end(),
                             [&](BfGlyphV1 const& q) { return !(q.flags & BF_GLYPH_SOLID) && (q.pos[0] < clipL || q.pos[0] > clipR); }),
              out.end());

    // ---- equalizer bars
    float const bx = x + mWidth - barsW - pad + 3 * k;
    for (int i = 0; i < 4; ++i) {
        float tgt = in.playing ? 0.25f + 0.35f * std::fabs(std::sin(float(in.nowSeconds) * (3.1f + 1.7f * float(i)) + float(i))) +
                                     0.45f * in.beat * (i % 2 == 0 ? 1.0f : 0.7f)
                               : 0.18f;
        mBars[i] = anim::approach(mBars[i], std::min(1.0f, tgt), 14, dt);
        float bh = std::max(1.5f * k, mBars[i] * h * 0.5f);
        rect(bx + 3 * k * float(i), y + (h - bh) / 2, 1.8f * k, bh, colLerp(th.b, acc, float(i) / 3.0f), a);
    }
    (void)aspect;
}

} // namespace bf::lyrics
