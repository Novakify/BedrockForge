// LyricsFlow port - world panel (see Panel.h). Ported from LyricsFlow 2.1 app.cosmetic.WorldPanel (374m, MIT) for
// Fernando's personal build: layout, sizes, colours, timings and option mapping follow the 2.1 bytecode. Original C++.
//
// Differences, on purpose:
// - Unicode glyphs the Minecraft font draws through its fallback fonts (play / pause / skip / repeat / note / close)
//   are drawn with LyricsFlow's own icon PNGs from the UI atlas.
// - Radial-gradient discs (Java ring()) are drawn as flat discs.
// - The Cosmetics tab (hats, death animations, glow ring: not in the Bedrock port) is the FPS page instead.
// - "Normal" / "Open full menu" / "Back to the normal menu" ask the game side to open Bedrock Forge's menu (there is no
//   LyricsFlow full-screen menu in the port yet).
// - Sliders also follow a held click ([BF] drag); Java sets them on the click only.
#include "lyrics/core/Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "lyrics/core/Perf.h"
#include "lyrics/core/Text.h"

namespace bf::lyrics {

namespace {
// WorldPanel constants (RGB)
constexpr uint32_t ACCENT  = 0x9D7DFF; // 10321407
constexpr uint32_t ACCENT2 = 0xDCD0FF; // 14471167
constexpr uint32_t DEEP2   = 0x7A5AE8; // 8018664 (selected gradient bottom)
constexpr uint32_t TOP     = 0x1A1634; // 1709620
constexpr uint32_t BOTTOM  = 0x0B0A17; // 723479
constexpr uint32_t EDGE1   = 0xB9A9F0; // 12167664 (border top)
constexpr uint32_t EDGE2   = 0x6E58C8; // 7231688  (border bottom)
constexpr uint32_t WHITE   = 0xFFFFFF;
constexpr uint32_t GREY    = 0xC5C5C5; // 12961221
constexpr uint32_t GREEN   = 0x1ED760; // 2021216
constexpr uint32_t CLOSE_R = 0xFF5A7A; // 16734842
constexpr uint32_t BTN_IN  = 0x2A2645; // 2762309
constexpr uint32_t NOCOVER = 0x2C2C36; // 2894902

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float outCubic(float t) { t = 1 - t; return 1 - t * t * t; }
float inOut(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }
uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
    t = clamp01(t);
    auto ch = [&](int s) { return uint32_t(std::lround(float((a >> s) & 255) + (float((b >> s) & 255) - float((a >> s) & 255)) * t)) << s; };
    return ch(16) | ch(8) | ch(0);
}
uint32_t opaque(uint32_t rgb) { return 0xFF000000u | (rgb & 0xFFFFFF); }

std::string fmtTime(int64_t ms) { // WorldPanel.fmt: m:ss
    int64_t s = std::max<int64_t>(0, ms / 1000);
    char    b[24];
    std::snprintf(b, sizeof b, "%d:%02d", int(s / 60), int(s % 60));
    return b;
}
std::string fontName(std::string const& id) {
    static std::map<std::string, std::string> const names{{"lyricsflow_script", "LyricsFlow"}, {"outfit", "Outfit"}, {"outfit_bold", "Outfit Bold"},
                                                          {"greatvibes", "Great Vibes"},       {"kaushan", "Kaushan"}, {"dancing", "Dancing"},
                                                          {"pacifico", "Pacifico"},            {"poppins", "Poppins"}, {"minecraft", "Minecraft"},
                                                          // [BF] display fonts (OFL / Apache, Google Fonts)
                                                          {"bebas", "Bebas Tall"}, {"bangers", "Comic Bangers"}, {"monoton", "Neon Tubes"},
                                                          {"gothic", "Gothic"}, {"creepster", "Creepy"}, {"arcade", "Arcade Pixel"},
                                                          {"orbitron", "Sci-Fi"}, {"marker", "Marker"}, {"glitch", "Glitch"},
                                                          {"typewriter", "Typewriter"}, {"abril", "Fat Serif"}, {"fredoka", "Bubble"},
                                                          {"righteous", "Retro"}, {"rocksalt", "Scribble"}, {"nosifer", "Blood Drip"},
                                                          {"audiowide", "Racing"}, {"shrikhand", "Groovy"}, {"cinzel", "Royal"}};
    auto it = names.find(id);
    return it == names.end() ? id : it->second;
}
} // namespace

std::vector<Panel::SkyPresetInfo> const& Panel::skyOrder() { // SkyPreset.order() + enum colours (top, horizon)
    static std::vector<SkyPresetInfo> const v{
        {"inferno", "Inferno", 0x1A0402, 0x7A1C06},           {"matrix", "Matrix", 0x000804, 0x021C0C},
        {"prism", "Prism", 0x070414, 0x1A1036},               {"pulsar", "Pulsar", 0x02030C, 0x0A1430},
        {"aquarell", "Watercolor", 0xE6C8E8, 0xFFE4D2},        {"synthwave", "Synthwave", 0x0B0524, 0x5A1A6E},
        {"aurora", "Aurora", 0x020A14, 0x0B2A33},             {"nebula", "Nebula", 0x07031A, 0x1E0A3A},
        {"black_hole", "Black hole", 0x010005, 0x0B0618},     {"cosmos", "Cosmos", 0x03020C, 0x12092C},
        {"neon_city", "Neon City", 0x050816, 0x2A0E4A},       {"golden_hour", "Golden Hour", 0x2A2350, 0xFF9C5C},
        {"cotton_candy", "Cotton candy", 0x4F90F5, 0xFFC8E4}, {"sakura", "Sakura", 0x3A1A4C, 0xFFB4D9},
        {"frost", "Frost night", 0x0A1A30, 0x5F94C8},         {"midnight", "Midnight", 0x02030A, 0x101A3A},
        {"deep_sea", "Deep Sea", 0x02171E, 0x0B4A5A},         {"blood_moon", "Blood Moon", 0x150306, 0x4A0A12},
        {"storm", "Storm", 0x0E1118, 0x2E3442},               {"custom", "Custom sky", 0x0A0618, 0x2A1648},
    };
    return v;
}

// ---------------------------------------------------------------- open / close / mapping
void Panel::open(Vec3 eye, Vec3 look, int64_t nowMs) {
    double len = std::sqrt(look.x * look.x + look.z * look.z);
    float  fx = len < 1e-4 ? 0.0f : float(look.x / len), fz = len < 1e-4 ? 1.0f : float(look.z / len);
    mPos = {eye.x + fx * 3.2, eye.y - 0.05, eye.z + fz * 3.2};
    mNx = -fx; mNz = -fz;
    mRx = -fz; mRz = fx;
    mBorn = nowMs; mClosing = -1; mPageTime = 0;
    mHov.clear(); mHot.clear(); mActive.clear();
    mHovered = false;
}
void Panel::close(int64_t nowMs) {
    if (mBorn >= 0 && mClosing < 0) mClosing = nowMs;
}
bool Panel::visible(int64_t nowMs) const { return mBorn >= 0 && (mClosing < 0 || nowMs - mClosing < 900); }

Vec3 Panel::toWorld(float x, float y) const {
    return {mPos.x + mRx * (x - 190.0f) * kPanelK, mPos.y + (111.0f - y) * kPanelK, mPos.z + mRz * (x - 190.0f) * kPanelK};
}

std::optional<std::array<float, 2>> Panel::aimAt(Vec3 eye, Vec3 look) const {
    double d = look.x * mNx + look.z * mNz;
    if (d > -1e-4) return std::nullopt;
    double t = ((mPos.x - eye.x) * mNx + (mPos.z - eye.z) * mNz) / d;
    if (t <= 0 || t > 14.0) return std::nullopt;
    double hx = eye.x + look.x * t - mPos.x, hy = eye.y + look.y * t - mPos.y, hz = eye.z + look.z * t - mPos.z;
    float  u = float((hx * mRx + hz * mRz) / kPanelK) + 190.0f;
    float  v = 111.0f - float(hy / kPanelK);
    if (u < 14.0f || v < 0.0f || u > kPanelW || v > kPanelH) return std::nullopt;
    return std::array<float, 2>{u, v};
}

// ---------------------------------------------------------------- frame context
struct Panel::Ctx {
    Panel&                   p;
    PanelInput const&        in;
    FontSet const&           fonts;
    PanelAssets const&       assets;
    std::vector<BfGlyphV1>&  out;
    std::vector<BfGlyphV1>   late; // text: drawn after all shapes (Java "late" runnables)
    std::vector<PanelAction> actions;
    Font const*              font = nullptr;
    float                    bodyA = 1, pageA = 1, recA = 1;
    Vec3                     base, ax, by; // px -> world (relative to origin): P = base + ax * x + by * y
    bool                     changed = false;
    std::string              hotNow;

    void frame(Vec3 b, Vec3 x, Vec3 y) { base = b; ax = x; by = y; }
    Vec3 at(float x, float y) const { return base + ax * double(x) + by * double(y); }

    // ---- interaction (panel px; last frame's hot id decides clicks, like the end-first walk over Java's hits list)
    bool over(float x, float y, float w, float h) const { return p.mHovered && p.mHx >= x && p.mHy >= y && p.mHx < x + w && p.mHy < y + h; }
    float hv(std::string const& id, bool on) {
        float& v = p.mHov[id];
        v += ((on ? 1.0f : 0.0f) - v) * std::min(1.0f, in.dt * 14.0f);
        return v;
    }
    float hit(std::string const& id, float x, float y, float w, float h) {
        bool o = over(x, y, w, h);
        if (o) hotNow = id;
        return hv(id, o);
    }
    bool clicked(std::string const& id) const { return in.click && p.mHot == id; }

    // ---- primitives
    void quad(float cx, float cy, float hw, float hh, BfGlyphV1& q) const {
        Vec3 c = at(cx, cy), r = ax * double(hw), u = by * double(-hh);
        q.pos[0] = float(c.x); q.pos[1] = float(c.y); q.pos[2] = float(c.z);
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
    }
    /// WorldPanel.round: rounded rect, vertical gradient c1 (top) -> c2 (bottom).
    void round(float x, float y, float w, float h, float r, uint32_t c1, uint32_t c2, float alpha) {
        if (alpha <= 0.003f || w <= 0 || h <= 0) return;
        BfGlyphV1 q{};
        quad(x + w / 2, y + h / 2, w / 2, h / 2, q);
        q.fill     = toRgba(opaque(c1));
        q.fill2    = toRgba(opaque(c2));
        q.alpha    = alpha;
        float half = std::min(w, h) / 2;
        q.outlineW = clamp01(std::min(r, half) / std::max(0.01f, half));
        q.glowW    = 1.0f;
        q.flags    = BF_GLYPH_SOLID | (c1 != c2 ? BF_GLYPH_GRADIENT : 0u);
        out.push_back(q);
    }
    void disc(float cx, float cy, float r, uint32_t rgb, float alpha) { round(cx - r, cy - r, 2 * r, 2 * r, r, rgb, rgb, alpha); }
    void imageAt(int atlas, UiRect const& uv, float x, float y, float w, float h, float alpha, uint32_t tint = WHITE, bool isLate = false) {
        if (atlas < 0 || alpha <= 0.003f) return;
        BfGlyphV1 q{};
        quad(x + w / 2, y + h / 2, w / 2, h / 2, q);
        q.uv[0] = uv.u0; q.uv[1] = uv.v0; q.uv[2] = uv.u1; q.uv[3] = uv.v1;
        q.fill = q.fill2 = toRgba(opaque(tint));
        q.alpha = alpha;
        q.atlas = uint32_t(atlas);
        q.flags = BF_GLYPH_IMAGE;
        (isLate ? late : out).push_back(q);
    }
    /// isLate = drawn with the text, on top of everything (Java draws glyph icons through the late text pass)
    bool image(std::string const& name, float x, float y, float w, float h, float alpha, uint32_t tint = WHITE, bool isLate = false) {
        auto it = assets.ui.find(name);
        if (assets.uiAtlas < 0 || it == assets.ui.end()) return false;
        imageAt(assets.uiAtlas, it->second, x, y, w, h, alpha, tint, isLate);
        return true;
    }
    // ---- text in Minecraft-font units: scale 1 = 7 px cap height, y = top of the text (Java textAt)
    float mcSize(float scale) const { return font ? 7.0f * scale / std::max(0.1f, font->capHeight) : 8.0f * scale; }
    float textW(std::string_view s, float scale) const { return font ? font->width(s) * mcSize(scale) : float(s.size()) * 6.0f * scale; }
    std::string ellip(std::string s, float maxW, float scale) const { // WorldPanel.ellip
        if (textW(s, scale) <= maxW) return s;
        auto cps = text::codepoints(s);
        while (!cps.empty()) {
            cps.pop_back();
            std::string t;
            for (uint32_t cp : cps) text::append(t, cp);
            t += "...";
            if (textW(t, scale) <= maxW) return t;
        }
        return "...";
    }
    void text(std::string const& s, float x, float y, float scale, uint32_t rgb, float alpha, bool centered = false) {
        if (!font || s.empty() || alpha <= 0.01f) return;
        float const size = mcSize(scale);
        float       pen  = centered ? x - textW(s, scale) / 2 : x;
        float const base = y + 7.0f * scale; // baseline under a 7 px cap
        for (uint32_t cp : text::codepoints(s)) {
            GlyphMetrics const* g = font->find(cp);
            if (!g) g = font->find(cp == 0x2039 ? '<' : cp == 0x203A ? '>' : cp == 0x2026 ? '.' : cp == 0xB7 ? '-' : '?');
            float adv = g ? g->advance : 0.5f;
            if (g && !g->empty) {
                BfGlyphV1 q{};
                quad(pen + (g->pl + g->pr) * 0.5f * size, base - (g->pb + g->pt) * 0.5f * size, (g->pr - g->pl) * 0.5f * size,
                     (g->pt - g->pb) * 0.5f * size, q);
                q.uv[0] = g->u0; q.uv[1] = g->v0; q.uv[2] = g->u1; q.uv[3] = g->v1;
                q.fill = q.fill2 = toRgba(opaque(rgb));
                q.alpha   = alpha;
                q.atlas   = font->atlasIndex;
                q.pxRange = font->pxRange;
                q.flags   = font->flags;
                late.push_back(q);
            }
            pen += adv * size;
        }
    }

    // ---- widgets (WorldPanel.toggle2 / toggle / option / slider / roundBtn / pager)
    void toggle2(std::string const& id, float x, float y, float w, std::string const& label, bool on, std::function<void()> const& run, float a) {
        float const h  = 16;
        float       hv = hit(id, x, y, w, h);
        round(x, y, w, h, 6, WHITE, WHITE, (0.035f + 0.05f * hv) * a);
        text(ellip(label, w - 34, 0.68f), x + 7, y + 5, 0.68f, WHITE, 0.92f * a);
        float sx = x + w - 25, sy = y + 4;
        round(sx, sy, 19, 8, 4, on ? ACCENT : WHITE, on ? DEEP2 : WHITE, (on ? 0.95f : 0.16f) * a);
        disc(on ? sx + 15 : sx + 4, sy + 4, 3.2f, WHITE, a);
        if (clicked(id)) { run(); changed = true; }
    }
    void toggle(int row, std::string const& label, bool on, std::function<void()> const& run, float a) {
        toggle2("t" + std::to_string(row), 206, 47.0f + float(row) * 20, 160, label, on, run, a);
    }
    void option(std::string const& id, float x, float y, float w, float h, std::string const& label, bool sel, std::function<void()> const& run, float a) {
        float hv = hit(id, x, y, w, h);
        if (sel) round(x, y, w, h, 6, ACCENT, DEEP2, 0.88f * a);
        else round(x, y, w, h, 6, WHITE, WHITE, (0.035f + 0.06f * hv) * a);
        text(ellip(label, w - 12, 0.66f), x + 7, y + (h - 6) / 2, 0.66f, WHITE, (sel ? 1.0f : 0.8f + 0.2f * hv) * a);
        if (clicked(id)) { run(); changed = true; }
    }
    /// v in 0..1; returns the new 0..1 value (set by a click on the track; [BF] follows a held click).
    float slider(std::string const& id, std::string const& label, float y, float v, float a) {
        v = clamp01(v);
        float const x0 = 258, w = 366 - x0;
        float       hvv = hv(id, over(206, y - 4, 160, 14));
        if (over(x0 - 4, y - 4, w + 8, 14)) hotNow = id;
        if (clicked(id)) p.mActive = id;
        if (p.mActive == id && in.down && p.mHovered) {
            float nv = clamp01((p.mHx - x0) / w);
            if (nv != v) { v = nv; changed = true; }
        }
        text(label, 206, y, 0.66f, WHITE, (0.7f + 0.3f * hvv) * a);
        round(x0, y + 1.5f, w, 4, 2, WHITE, WHITE, (0.12f + 0.06f * hvv) * a);
        round(x0, y + 1.5f, std::max(4.0f, w * v), 4, 2, ACCENT2, ACCENT, a);
        disc(x0 + w * v, y + 3.5f, 4.5f + hvv, WHITE, a);
        return v;
    }
    bool roundBtn(std::string const& id, float cx, float cy, float r, std::string const& icon, std::string const& fallback, float a) {
        float h = hit(id, cx - r, cy - r, 2 * r, 2 * r);
        disc(cx, cy, r, lerpRgb(BTN_IN, ACCENT, h), a);
        float const is = r * 1.05f;
        if (!image(icon, cx - is / 2, cy - is / 2, is, is, a, WHITE, true)) text(fallback, cx, cy - 3, 0.75f, WHITE, a, true);
        return clicked(id);
    }
    void pager(std::string const& id, float y, int page, int count, int& target, float a) {
        if (count <= 1) return;
        float hl = hit(id + "<", 320, y, 14, 12), hr = hit(id + ">", 352, y, 14, 12);
        round(320, y, 14, 12, 6, WHITE, WHITE, (0.08f + 0.2f * hl) * a);
        round(352, y, 14, 12, 6, WHITE, WHITE, (0.08f + 0.2f * hr) * a);
        text("\xE2\x80\xB9", 325, y + 2.5f, 0.75f, WHITE, a); // ‹
        text("\xE2\x80\xBA", 357, y + 2.5f, 0.75f, WHITE, a); // ›
        text(std::to_string(page + 1) + "/" + std::to_string(count), 336, y + 3, 0.6f, WHITE, 0.7f * a);
        if (clicked(id + "<")) target = (page + count - 1) % count;
        if (clicked(id + ">")) target = (page + 1) % count;
    }
};

// ---------------------------------------------------------------- frame
std::vector<PanelAction> Panel::frame(PanelInput const& in, Vec3 origin, Config& cfg, FontSet const& fonts, PanelAssets const& assets,
                                      PanelMedia const& media, std::vector<BfGlyphV1>& out) {
    if (!visible(in.nowMs)) {
        mHovered = false;
        return {};
    }
    float const age = float(in.nowMs - mBorn), ca = mClosing >= 0 ? float(in.nowMs - mClosing) : -1.0f;
    float t9, t10, t11, t12; // record flight, panel unfold, body fade, record content (WorldPanel.render)
    if (ca < 0) {
        t9  = inOut(clamp01(age / 900.0f));
        t10 = outCubic(clamp01((age - 820.0f) / 460.0f));
        t11 = outCubic(clamp01((age - 1100.0f) / 300.0f));
        t12 = inOut(clamp01((age - 1150.0f) / 400.0f));
    } else {
        t12 = 1 - inOut(clamp01(ca / 220.0f));
        t11 = 1 - clamp01(ca / 160.0f);
        t10 = 1 - inOut(clamp01((ca - 120.0f) / 300.0f));
        t9  = 1 - inOut(clamp01((ca - 380.0f) / 480.0f));
    }
    (void)t12;
    mSpin += (3.5f + 16.5f * std::exp(-age / 420.0f) + (ca >= 0 ? 10.0f * clamp01(ca / 600.0f) : 0.0f)) * in.dt;

    // aim (only once the panel has landed, and not while closing)
    mHovered = false;
    if (age >= 1200.0f && ca < 0)
        if (auto h = aimAt(in.eye, in.look)) { mHovered = true; mHx = (*h)[0]; mHy = (*h)[1]; }
    if (!in.down) mActive.clear();

    Ctx c{*this, in, fonts, assets, out};
    c.font  = fonts.get(assets.pixelFont);
    c.bodyA = t11;
    c.recA  = std::min(1.0f, (ca < 0 ? age : 900.0f - ca) / 150.0f);
    double const bob = std::sin(double(in.nowMs) / 1200.0) * 0.012 * t10;
    Vec3 const   R{mRx, 0, mRz}, U{0, 1, 0}, N{mNx, 0, mNz};
    Vec3 const   P = mPos + Vec3{0, bob, 0} - origin; // panel centre relative to the frame origin
    double const K = kPanelK;

    if (t10 > 0.01f) {
        // the panel unfolds sideways out of the record (pivot = local x -0.966 = px 98)
        double const sx = t10;
        c.frame(P + R * (K * (98.0 - 190.0 - 98.0 * sx)) + U * (K * 111.0), R * (K * sx), U * (-K));
        float const a21 = std::min(1.0f, t10 * 1.5f);
        for (int i = 4; i >= 1; --i) // soft purple glow
            c.round(62 - float(i) * 2.5f, -float(i) * 2.5f, 318 + float(i) * 5, 222 + float(i) * 5, 19 + float(i) * 2.5f, ACCENT, ACCENT, 0.05f * a21);
        c.round(61.2f, -0.8f, 319.6f, 223.6f, 19.8f, EDGE1, EDGE2, 0.35f * a21);
        c.round(62, 0, 318, 222, 19, TOP, BOTTOM, 0.96f * a21);
        c.round(150, 0.6f, 144, 1.6f, 0.8f, ACCENT, ACCENT2, 0.7f * a21);
        if (t11 > 0.01f) body(c, cfg, media);
    }

    // the record card: flies up from in front of the camera to the panel's left on an arc, tipping up as it lands
    if (c.recA > 0.01f) {
        Vec3 const fin   = P + R * -0.966 + Vec3{0, 0.0735, 0};
        Vec3 const start = in.eye - origin + in.look * 0.9 + in.up * -0.3;
        Vec3       pos   = start + (fin - start) * double(t9);
        pos.y += std::sin(3.14159265358979 * double(t9)) * 0.45;
        double const s  = 0.12 + 0.88 * outCubic(t9);
        double const th = -(1.0 - t9) * 70.0 * 3.14159265358979 / 180.0;
        Vec3 const   Ur = U * std::cos(th) + N * std::sin(th);
        c.frame(pos, R * (K * s), Ur * (-K * s));
        c.round(-84.8f, -104.8f, 169.6f, 223.6f, 19.8f, EDGE1, EDGE2, 0.35f * c.recA);
        c.round(-84, -104, 168, 222, 19, TOP, BOTTOM, 0.97f * c.recA);
        record(c, media);
    }

    // crosshair dot on the panel
    if (mHovered) {
        c.frame(P + R * (K * -190.0) + U * (K * 111.0), R * K, U * (-K));
        c.disc(mHx, mHy, 5.5f, ACCENT, 0.35f);
        c.disc(mHx, mHy, 2.2f, WHITE, 0.95f);
    }
    out.insert(out.end(), c.late.begin(), c.late.end());

    mHot = c.hotNow;
    if (c.changed) c.actions.push_back({PanelAction::Kind::SaveConfig, 0});
    return std::move(c.actions);
}

// ---------------------------------------------------------------- record card (left), centred on px (98, 104)
void Panel::record(Ctx& c, PanelMedia const& np) {
    float const a  = c.recA;
    float const cs = 124, cx = -cs / 2, cy = -86;
    if (np.hasTrack && c.assets.cover >= 0) {
        c.round(cx - 1.5f, cy - 1.5f, cs + 3, cs + 3, 9, ACCENT, EDGE2, 0.35f * a);
        c.imageAt(c.assets.cover, UiRect{}, cx, cy, cs, cs, a);
    } else {
        c.round(cx, cy, cs, cs, 10, EDGE2, NOCOVER, a);
        c.text("?", 0, cy + cs / 2 - 14, 3.0f, WHITE, 0.8f * a, true);
    }
    std::string const title  = np.hasTrack ? np.title : "No music";
    std::string const artist = np.hasTrack ? np.artist : "";
    c.text(c.ellip(title, 150, 0.85f), 0, 46, 0.85f, WHITE, a, true);
    if (!artist.empty()) c.text(c.ellip(artist, 150, 0.62f), 0, 59, 0.62f, GREY, 0.85f * a, true);

    float const bw = 136, bx = -bw / 2, by = 70;
    float       pr  = 0;
    int64_t     pos = 0;
    if (np.hasTrack && np.durationMs > 0) {
        pos = mActive == "seek" && mHovered ? int64_t(clamp01((mHx - 30) / 136) * float(np.durationMs)) : np.positionMs;
        pr  = clamp01(float(double(pos) / double(np.durationMs)));
    }
    c.round(bx, by, bw, 3, 1.5f, WHITE, WHITE, 0.18f * a);
    if (pr > 0) c.round(bx, by, std::max(3.0f, bw * pr), 3, 1.5f, GREEN, GREEN, a);
    if (np.hasTrack) {
        c.text(fmtTime(pos), bx, 75.5f, 0.5f, GREY, 0.8f * a);
        std::string d = fmtTime(np.durationMs);
        c.text(d, bx + bw - c.textW(d, 0.5f), 75.5f, 0.5f, GREY, 0.8f * a);
        std::string const src = np.sourceApp == "Browser" ? "YouTube" : np.sourceApp;
        if (!src.empty()) {
            float const       tw   = c.textW(src, 0.6f), x0 = -(tw + 15) / 2;
            std::string const logo = src == "Spotify" ? "logo.spotify" : src == "Apple Music" ? "logo.apple_music" : "logo.youtube";
            c.image(logo, x0 - 2, 82, 10, 10, a);
            c.text(src, x0 + 13, 84, 0.6f, WHITE, a);
        }
    }
}

// ---------------------------------------------------------------- body (player buttons, header, tabs, page)
void Panel::body(Ctx& c, Config& cfg, PanelMedia const& np) {
    float const a = c.bodyA;
    float const y = 207;
    if (c.roundBtn("prev", 70, y, 8, "icon.prev", "<<", a)) c.actions.push_back({PanelAction::Kind::Prev, 0});
    if (c.roundBtn("play", 98, y, 10, np.playing ? "icon.pause" : "icon.play", np.playing ? "||" : ">", a))
        c.actions.push_back({PanelAction::Kind::PlayPause, 0});
    if (c.roundBtn("next", 126, y, 8, "icon.next", ">>", a)) c.actions.push_back({PanelAction::Kind::Next, 0});
    if (c.roundBtn("repeat", 150, y, 6.5f, "icon.refresh", "R", a)) c.actions.push_back({PanelAction::Kind::Repeat, 0});
    if (np.repeat == 1) c.text("1", 153, y + 0.5f, 0.45f, ACCENT2, a);
    else if (np.repeat == 2) c.disc(155, y + 4, 1.2f, ACCENT2, a);
    c.hit("cover", 36, 18, 124, 124);
    if (c.clicked("cover")) c.actions.push_back({PanelAction::Kind::PlayPause, 0});
    c.hit("seek", 30, 169, 136, 10);
    if (c.clicked("seek") && np.hasTrack && np.durationMs > 0) {
        mActive = "seek";
        c.actions.push_back({PanelAction::Kind::Seek, int64_t(clamp01((mHx - 30) / 136) * float(np.durationMs))});
    }

    c.text("LyricsFlow", 206, 10, 0.85f, WHITE, a);
    float hc = c.hit("x", 351, 6, 15, 15);
    c.disc(358.5f, 13.5f, 7.5f, lerpRgb(WHITE, CLOSE_R, hc), (0.08f + 0.55f * hc) * a);
    if (!c.image("icon.close", 355.5f, 10.5f, 6, 6, a, WHITE, true)) c.text("x", 355.5f, 10.5f, 0.7f, WHITE, a);
    if (c.clicked("x")) c.actions.push_back({PanelAction::Kind::Close, 0});
    std::string const nrm = "Normal";
    float const       nw  = c.textW(nrm, 0.62f) + 12;
    float             hn  = c.hit("nrm", 346 - nw, 6, nw, 15);
    c.round(346 - nw, 6, nw, 15, 7.5f, WHITE, WHITE, (0.07f + 0.12f * hn) * a);
    c.text(nrm, 352 - nw, 10.5f, 0.62f, WHITE, (0.8f + 0.2f * hn) * a);
    if (c.clicked("nrm")) c.actions.push_back({PanelAction::Kind::OpenFullMenu, 0});

    static char const* const tabs[] = {"Sky", "Lyrics", "Look", "FX", "FPS", "More"};
    float const tw = (160.0f - 3.0f * 5) / 6;
    for (int i = 0; i < 6; ++i) {
        float       tx  = 206 + float(i) * (tw + 3);
        bool        sel = mPage == i;
        std::string id  = "tab" + std::to_string(i);
        float       h   = c.hit(id, tx, 203, tw, 14);
        if (sel) c.round(tx, 203, tw, 14, 7, ACCENT, DEEP2, 0.9f * a);
        else c.round(tx, 203, tw, 14, 7, WHITE, WHITE, (0.05f + 0.08f * h) * a);
        std::string lab = c.ellip(tabs[i], tw - 6, 0.62f);
        c.text(lab, tx + (tw - c.textW(lab, 0.62f)) / 2, 207, 0.62f, WHITE, (sel ? 1.0f : 0.7f + 0.3f * h) * a);
        if (c.clicked(id) && !sel) { mPage = i; mPageTime = c.in.nowMs; }
    }
    std::string const line = np.hasTrack ? np.title + "  \xC2\xB7  " + np.artist : std::string("No music");
    c.image("icon.music", 206, 28, 5, 5, 0.75f * a, ACCENT2, true);
    c.text(c.ellip(line, 150, 0.62f), 214, 28, 0.62f, ACCENT2, 0.75f * a);

    c.pageA = a * outCubic(clamp01(float(c.in.nowMs - mPageTime) / 220.0f));
    switch (mPage) {
    case 0: skies(c, cfg); break;
    case 1: lyrics(c, cfg); break;
    case 2: look(c, cfg, c.fonts); break;
    case 3: fx(c, cfg); break;
    case 4: fps(c, cfg); break;
    default: more(c, cfg); break;
    }
}

// ---------------------------------------------------------------- pages
void Panel::skies(Ctx& c, Config& cfg) {
    float const a   = c.pageA;
    auto const& ord = skyOrder();
    bool const  on  = cfg.skyPreset != "off";
    c.toggle2("skon", 206, 45, 78, "Sky", on, [&] { cfg.skyPreset = on ? "off" : ord[0].id; }, a);
    c.toggle2("skpic", 288, 45, 78, "My picture", cfg.skyImage, [&] { cfg.skyImage = !cfg.skyImage; }, a);
    int const per = 6, pages = int((ord.size() + per - 1) / per);
    mSkyPage      = ((mSkyPage % pages) + pages) % pages;
    float const w = 48, h = 27;
    for (int i = 0; i < per; ++i) {
        size_t idx = size_t(mSkyPage * per + i);
        if (idx >= ord.size()) break;
        auto const& sp   = ord[idx];
        float       x    = 206 + float(i % 3) * (w + 8), y = 66 + float(i / 3) * (h + 13);
        bool        sel  = cfg.skyPreset == sp.id;
        std::string id   = std::string("sky") + sp.id;
        float       hv   = c.hit(id, x - 1, y - 1, w + 2, h + 12);
        float       lift = hv * 1.2f;
        if (sel) c.round(x - 2, y - 2 - lift, w + 4, h + 4, 6, ACCENT, ACCENT2, a);
        else if (hv > 0.01f) c.round(x - 1.5f, y - 1.5f - lift, w + 3, h + 3, 5.5f, WHITE, WHITE, 0.5f * hv * a);
        if (!c.image(std::string("sky.") + sp.id, x, y - lift, w, h, a)) c.round(x, y - lift, w, h, 4, sp.top, sp.horizon, a);
        c.text(c.ellip(sp.name, w, 0.56f), x + 1, y + h + 3, 0.56f, WHITE, (sel ? 1.0f : 0.75f + 0.25f * hv) * a);
        if (c.clicked(id)) { cfg.skyPreset = sp.id; c.changed = true; }
    }
    c.pager("sk", 146, mSkyPage, pages, mSkyPage, a);
    cfg.skyStrength = 0.1f + 0.9f * c.slider("str", "Strength", 162, (cfg.skyStrength - 0.1f) / 0.9f, a);
    cfg.skySpeed    = 0.25f + 1.75f * c.slider("spd", "Speed", 178, (cfg.skySpeed - 0.25f) / 1.75f, a);
    cfg.skyPattern  = 0.2f + 1.3f * c.slider("pat", "Pattern", 194, (cfg.skyPattern - 0.2f) / 1.3f, a);
}

void Panel::lyrics(Ctx& c, Config& cfg) {
    float const a = c.pageA;
    c.toggle(0, "Lyrics in the sky", cfg.showLyrics, [&] { cfg.showLyrics = !cfg.showLyrics; }, a);
    bool const fl = cfg.lyricsStyle == LyricsStyle::Floating;
    c.toggle(1, "Floating words", fl, [&] { cfg.lyricsStyle = fl ? LyricsStyle::Sky : LyricsStyle::Floating; }, a);
    c.toggle2("beat", 206, 87, 78, "Pulse to the beat", cfg.beatGlow, [&] { cfg.beatGlow = !cfg.beatGlow; }, a);
    c.toggle2("notes", 288, 87, 78, "Music notes", cfg.musicNotes, [&] { cfg.musicNotes = !cfg.musicNotes; }, a);
    c.toggle2("smus", 206, 105, 78, "Sky reacts to music", cfg.fxSkyMusic, [&] { cfg.fxSkyMusic = !cfg.fxSkyMusic; }, a);
    c.toggle2("ugr", 288, 105, 78, "Underground", cfg.showUnderground, [&] { cfg.showUnderground = !cfg.showUnderground; }, a);
    c.toggle2("spread", 206, 123, 160, "Spread lyrics out", cfg.spreadOut, [&] { cfg.spreadOut = !cfg.spreadOut; }, a);
    cfg.textOpacity = 0.2f + 0.8f * c.slider("lop", "Opacity", 141, (cfg.textOpacity - 0.2f) / 0.8f, a);
    cfg.lyricsScale = 0.4f + 2.1f * c.slider("lsz", "Size", 155, (cfg.lyricsScale - 0.4f) / 2.1f, a);
    // [BF] how far the floating words stand (Scene: 5..7.5 blocks x 0.4..2)
    cfg.lyricsDistance = 0.4f + 1.6f * c.slider("ldist", "Distance", 169, (std::clamp(cfg.lyricsDistance, 0.4f, 2.0f) - 0.4f) / 1.6f, a);
    offset(c, cfg, 184);
}

void Panel::offset(Ctx& c, Config& cfg, float y) {
    float const a = c.pageA;
    c.text("Lyrics timing", 206, y, 0.66f, WHITE, 0.85f * a);
    std::string v = (cfg.syncOffsetMs > 0 ? "+" : "") + std::to_string(cfg.syncOffsetMs) + " ms";
    c.text(v, 330 - c.textW(v, 0.66f) / 2, y, 0.66f, WHITE, a);
    float hl = c.hit("offl", 294, y - 4, 14, 14), hr = c.hit("offr", 352, y - 4, 14, 14);
    c.round(294, y - 4, 14, 14, 7, WHITE, WHITE, (0.06f + 0.12f * hl) * a);
    c.round(352, y - 4, 14, 14, 7, WHITE, WHITE, (0.06f + 0.12f * hr) * a);
    c.text("\xE2\x80\xB9", 299, y, 0.66f, WHITE, a);
    c.text("\xE2\x80\xBA", 357, y, 0.66f, WHITE, a);
    if (c.clicked("offl")) { cfg.syncOffsetMs = std::max(-2000, cfg.syncOffsetMs - 100); c.changed = true; }
    if (c.clicked("offr")) { cfg.syncOffsetMs = std::min(2000, cfg.syncOffsetMs + 100); c.changed = true; }
}

void Panel::look(Ctx& c, Config& cfg, FontSet const& fonts) {
    float const a = c.pageA;
    c.text("Text color", 206, 45, 0.62f, WHITE, 0.55f * a);
    auto const themes = allThemes();
    int const  n      = std::min<int>(int(themes.size()), 20);
    for (int i = 0; i < n; ++i) {
        auto const& th  = themes[size_t(i)];
        float       x   = 212 + float(i % 10) * 16, y = 60 + float(i / 10) * 15;
        bool        sel = !cfg.customTheme && cfg.singleColor == 0 && cfg.theme == th.id;
        std::string id  = "th" + std::string(th.id);
        float       hv  = c.hit(id, x - 6, y - 6, 12, 12);
        if (sel) { c.disc(x, y, 6.6f, WHITE, a); c.disc(x, y, 5.2f, BOTTOM, a); }
        float r = 4.6f + hv * 0.6f;
        c.disc(x, y, r, th.a & 0xFFFFFF, a);
        c.disc(x, y, r * 0.55f, th.main & 0xFFFFFF, a);
        if (c.clicked(id)) { cfg.theme = std::string(th.id); cfg.customTheme = false; cfg.singleColor = 0; c.changed = true; }
    }
    std::vector<Font const*> fl;
    for (auto const& f : fonts.all()) fl.push_back(&f);
    int const per = 6, pages = std::max(1, int((fl.size() + per - 1) / per));
    mFontPage     = ((mFontPage % pages) + pages) % pages;
    c.text("Font", 206, 86, 0.62f, WHITE, 0.55f * a);
    for (int i = 0; i < per; ++i) {
        size_t idx = size_t(mFontPage * per + i);
        if (idx >= fl.size()) break;
        Font const* f = fl[idx];
        c.option("f" + f->id, 206 + float(i % 2) * 82, 95 + float(i / 2) * 17, 78, 14, fontName(f->id), cfg.font == f->id, [&] { cfg.font = f->id; }, a);
    }
    c.pager("fp", 84, mFontPage, pages, mFontPage, a);
    c.toggle2("caps", 206, 148, 78, "Capitals", cfg.allCaps, [&] { cfg.allCaps = !cfg.allCaps; }, a);
    c.toggle2("glow", 288, 148, 78, "Glow", cfg.glow, [&] { cfg.glow = !cfg.glow; }, a);
    bool const typed = cfg.enterAnim == EnterAnim::Neon;
    c.toggle2("typed", 206, 167, 160, "Typed out (LyricsFlow)", typed, [&] { cfg.enterAnim = typed ? EnterAnim::Word : EnterAnim::Neon; }, a);
    static char const* const sh[] = {"No shadow", "Soft", "Strong"};
    for (int i = 0; i < 3; ++i)
        c.option("sh" + std::to_string(i), 206 + float(i) * 54, 186, 50, 13, sh[i], cfg.textShadow == i, [&, i] { cfg.textShadow = i; }, a);
}

// [BF] The "Ground Drop" preset (reference video): plain white Minecraft text a few blocks ahead, letters fall one
// by one and lie on the blocks, still readable.
void applyGroundLook(Config& cfg) {
    static char const* const kLook = R"({"lyricsStyle":"FLOATING","font":"minecraft","theme":"minecraft","textStyle":"CLEAN",
        "textShadow":1,"twoColor":false,"glow":false,"varySizes":false,"arcLayout":false,"enterAnim":"WORD","exitAnim":"FALL",
        "maxLines":1,"fallingLetters":true,"letterLand":"FLAT","fallDelayMs":200,"gravity":16,"bounce":0.1,"friction":0.8,
        "spin":0.3,"letterLifetimeMs":6000,"letterFadeMs":1500,"lyricsDistance":0.65,"lyricsScale":0.75,"fxWave":false,
        "fxRainbow":false,"fxBeatShake":false,"wholeLine":true})";
    if (auto j = json::parse(kLook)) applyJson(cfg, *j);
}

// [BF] FX tab: how words come in and leave (incl. the falling-letter physics and the break-apart exits), plus
// effects that run while a line is up.
void Panel::fx(Ctx& c, Config& cfg) {
    float const a = c.pageA;
    c.text("Words come in", 206, 45, 0.62f, WHITE, 0.55f * a);
    static std::pair<EnterAnim, char const*> const ins[] = {{EnterAnim::Word, "Pop"},   {EnterAnim::Fade, "Fade"}, {EnterAnim::Rise, "Rise"},
                                                            {EnterAnim::Drop, "Drop"},  {EnterAnim::Spin, "Spin"}, {EnterAnim::Zoom, "Zoom"},
                                                            {EnterAnim::Glitch, "Glitch"}, {EnterAnim::Neon, "Typed"}};
    for (int i = 0; i < 8; ++i) {
        auto [v, lab] = ins[i];
        c.option("fi" + std::to_string(i), 206 + float(i % 4) * 40.5f, 53 + float(i / 4) * 15, 38, 13, lab, cfg.enterAnim == v,
                 [&cfg, v = v] { cfg.enterAnim = v; }, a);
    }
    c.text("Words leave", 206, 86, 0.62f, WHITE, 0.55f * a);
    static std::pair<ExitAnim, char const*> const outs[] = {{ExitAnim::FloatUp, "Float up"}, {ExitAnim::Drift, "Drift"},     {ExitAnim::Fade, "Fade"},
                                                            {ExitAnim::Fall, "Fall"}, {ExitAnim::Shatter, "Shatter"}, {ExitAnim::Dissolve, "Dissolve"},
                                                            {ExitAnim::Explode, "Explode"},  {ExitAnim::Burn, "Burn"},       {ExitAnim::Glitch, "Glitch"}};
    for (int i = 0; i < 9; ++i) {
        auto [v, lab] = outs[i];
        c.option("fo" + std::to_string(i), 206 + float(i % 3) * 54, 94 + float(i / 3) * 15, 51, 13, lab, cfg.exitAnim == v,
                 [&cfg, v = v] {
                     cfg.exitAnim       = v;
                     cfg.fallingLetters = v == ExitAnim::Fall; // the old toggle follows the choice
                 },
                 a);
    }
    c.toggle2("fxw", 206, 142, 78, "Wave", cfg.fxWave, [&] { cfg.fxWave = !cfg.fxWave; }, a);
    c.toggle2("fxr", 288, 142, 78, "Rainbow", cfg.fxRainbow, [&] { cfg.fxRainbow = !cfg.fxRainbow; }, a);
    c.toggle2("fxs", 206, 160, 78, "Beat shake", cfg.fxBeatShake, [&] { cfg.fxBeatShake = !cfg.fxBeatShake; }, a);
    c.toggle2("fxl", 288, 160, 78, "Land flat", cfg.letterLand == LetterLand::Flat,
              [&] { cfg.letterLand = cfg.letterLand == LetterLand::Flat ? LetterLand::Tumble : LetterLand::Flat; }, a);
    if (cfg.exitAnim == ExitAnim::Fall) { // [BF] one click to the reference-video look (small white pixel text, low, close)
        bool const on = cfg.font == "minecraft" && !cfg.glow && cfg.maxLines == 1 && cfg.wholeLine && cfg.letterLand == LetterLand::Flat;
        c.toggle2("fxg", 206, 182, 160, "Ground look (video)", on, [&cfg] { applyGroundLook(cfg); }, a);
    } else
        cfg.fxIntensity = 0.2f + 1.8f * c.slider("fxi", "Break power", 186, (std::clamp(cfg.fxIntensity, 0.2f, 2.0f) - 0.2f) / 1.8f, a);
}

// [BF] The 4th tab: LyricsFlow 2.1's performance settings (Profiles, quality, Auto FPS, FPS boost) laid out with the
// WorldPanel widgets. (2.1 has Cosmetics here: hats / death animations are not in the Bedrock port.)
void Panel::fps(Ctx& c, Config& cfg) {
    float const a = c.pageA;
    c.text("Profile", 206, 45, 0.62f, WHITE, 0.55f * a);
    c.option("pf0", 206, 54, 78, 14, "Chill", cfg.profile == 0, [&] { applyProfile(cfg, 0); }, a);
    c.option("pf1", 288, 54, 78, 14, "PvP", cfg.profile == 1, [&] { applyProfile(cfg, 1); }, a);
    c.text("Graphics (lyrics & sky)", 206, 75, 0.62f, WHITE, 0.55f * a);
    static char const* const q[] = {"Performance", "Balanced", "Pretty"};
    for (int i = 0; i < 3; ++i)
        c.option("q" + std::to_string(i), 206 + float(i) * 54, 84, 50, 13, q[i], cfg.quality == i, [&, i] { cfg.quality = i; }, a);
    c.toggle2("auto", 206, 103, 160, "Automatic FPS", cfg.autoPerf, [&] { cfg.autoPerf = !cfg.autoPerf; }, a);
    float tv = c.slider("tgt", cfg.targetFps > 0 ? "Target " + std::to_string(cfg.targetFps) : std::string("Target auto"), 126,
                        float(cfg.targetFps) / 240.0f, a);
    cfg.targetFps = int(std::lround(tv * 240.0f / 5.0f)) * 5;
    if (cfg.targetFps > 0 && cfg.targetFps < 30) cfg.targetFps = 0; // the low end of the track = auto
    c.toggle2("boost", 206, 141, 160, "FPS boost (game, next start)", cfg.fpsBoost, [&] { cfg.fpsBoost = !cfg.fpsBoost; }, a);
    std::string st = !cfg.autoPerf ? "Auto off"
                   : c.in.perfLevel > 0 ? "Auto is saving: level " + std::to_string(c.in.perfLevel) + " of 6"
                                        : std::string("All on - FPS are fine");
    c.text(st, 206, 165, 0.6f, ACCENT2, 0.85f * a);
    char b[64];
    std::snprintf(b, sizeof b, "FPS %d  \xC2\xB7  Mod %.2f ms", c.in.fps, c.in.modMs);
    c.text(b, 206, 177, 0.56f, WHITE, 0.6f * a);
}

void Panel::more(Ctx& c, Config& cfg) {
    float const a = c.pageA;
    c.toggle(0, "Top bar", cfg.showTopBar, [&] { cfg.showTopBar = !cfg.showTopBar; }, a);
    c.toggle(1, "Clock", cfg.clock, [&] { cfg.clock = !cfg.clock; }, a);
    c.toggle2("fps", 206, 87, 78, "FPS", cfg.fps, [&] { cfg.fps = !cfg.fps; }, a);
    c.toggle2("png", 288, 87, 78, "Ping", cfg.ping, [&] { cfg.ping = !cfg.ping; }, a);
    c.toggle2("hb", 206, 107, 78, "Glass hotbar", cfg.customHotbar, [&] { cfg.customHotbar = !cfg.customHotbar; }, a);
    c.toggle2("snd", 288, 107, 78, "Menu sounds", cfg.uiSounds, [&] { cfg.uiSounds = !cfg.uiSounds; }, a);
    c.option("m0", 206, 128, 160, 15, "Open full menu", false, [&] { c.actions.push_back({PanelAction::Kind::OpenFullMenu, 0}); }, a);
    c.toggle2("m1", 206, 145, 160, "Random sky", cfg.skyShuffle, [&] { cfg.skyShuffle = !cfg.skyShuffle; }, a);
    c.option("m2", 206, 162, 160, 15, "Back to the normal menu", false, [&] { c.actions.push_back({PanelAction::Kind::OpenFullMenu, 0}); }, a);
    c.option("m3", 206, 179, 160, 15, "Close", false, [&] { c.actions.push_back({PanelAction::Kind::Close, 0}); }, a);
}

} // namespace bf::lyrics
