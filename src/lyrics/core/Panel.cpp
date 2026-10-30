// LyricsFlow port - in-world settings panel (see Panel.h). Original code for Bedrock Forge; layout and palette follow
// the LyricsFlow teaser panel by 374m, the options are LyricsFlow v2.0's (Pages.java) + the port's [BF] extras.
#include "lyrics/core/Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "lyrics/core/Text.h"

namespace bf::lyrics {

namespace {
// palette (ARGB), sampled from the teaser screenshot
constexpr uint32_t kCard      = 0xF8100F21; // card body
constexpr uint32_t kCardBack  = 0xF01A1736; // stacked "page" behind each card
constexpr uint32_t kEdge      = 0xFF5D52A6; // thin lavender card border
constexpr uint32_t kText      = 0xFFF4F2FF;
constexpr uint32_t kDim       = 0xFF8E8AA3;
constexpr uint32_t kFaint     = 0xFF5E5A72;
constexpr uint32_t kPill      = 0xFF1A1930; // toggle pills, tab pills
constexpr uint32_t kPillHot   = 0xFF26243F;
constexpr uint32_t kChip      = 0xFF2A263E;
constexpr uint32_t kAccent    = 0xFF8768E9; // toggle on
constexpr uint32_t kAccentTab = 0xFF9C84F5; // selected tab
constexpr uint32_t kLavender  = 0xFFBFA9FF; // slider fill
constexpr uint32_t kTrack     = 0xFF2E2E3D;
constexpr uint32_t kKnob      = 0xFFFCFCFE;
constexpr uint32_t kOff       = 0xFF464057;
constexpr uint32_t kGreen     = 0xFF1ED760; // progress (Spotify green, as in the teaser)
constexpr uint32_t kBarBack   = 0xFF3A3947;

uint32_t mix(uint32_t a, uint32_t b, float t) { return colLerp(a, b, std::clamp(t, 0.0f, 1.0f)); }

std::string fmtTime(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t s = ms / 1000;
    char    b[24];
    std::snprintf(b, sizeof b, "%d:%02d", int(s / 60), int(s % 60));
    return b;
}
std::string fmtF(char const* f, double v) {
    char b[32];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::string fontLabel(std::string const& id) {
    static std::map<std::string, std::string> const names{{"lyricsflow_script", "Script"}, {"outfit", "Outfit"}, {"outfit_bold", "Outfit Bold"},
                                                          {"greatvibes", "Great Vibes"}, {"kaushan", "Kaushan"}, {"dancing", "Dancing"},
                                                          {"pacifico", "Pacifico"},    {"poppins", "Poppins"}, {"minecraft", "Minecraft"}};
    auto it = names.find(id);
    return it == names.end() ? id : it->second;
}
} // namespace

// ---------------------------------------------------------------- sky presets (LyricsFlow v2.0 lang order)
std::vector<std::string> const& Panel::skyPresetIds() {
    static std::vector<std::string> const ids{"inferno", "matrix",      "prism",  "pulsar",       "watercolor", "synthwave",
                                              "northern_lights", "meteors", "sakura", "ocean",     "blood_moon", "golden_hour",
                                              "neon_city", "ice",         "toxic",  "void",         "lilac",      "storm",
                                              "lavender",  "sunset_beams", "cosmic", "eclipse",     "fireflies",  "night_lights"};
    return ids;
}
std::string Panel::skyPresetName(std::string const& id) {
    static std::map<std::string, std::string> const names{
        {"inferno", "Inferno"},     {"matrix", "Matrix"},         {"prism", "Prism"},         {"pulsar", "Pulsar"},
        {"watercolor", "Watercolor"}, {"synthwave", "Synthwave"}, {"northern_lights", "Northern Lights"}, {"meteors", "Meteor Shower"},
        {"sakura", "Sakura"},       {"ocean", "Ocean"},           {"blood_moon", "Blood Moon"}, {"golden_hour", "Golden Hour"},
        {"neon_city", "Neon City"}, {"ice", "Ice"},               {"toxic", "Toxic"},         {"void", "Void"},
        {"lilac", "Lilac Light"},   {"storm", "Storm"},           {"lavender", "Lavender"},   {"sunset_beams", "Sunset Beams"},
        {"cosmic", "Cosmic"},       {"eclipse", "Eclipse"},       {"fireflies", "Fireflies"}, {"night_lights", "Night Lights"}};
    auto it = names.find(id);
    return it == names.end() ? id : it->second;
}

// ---------------------------------------------------------------- frame context (immediate-mode helpers)
struct Panel::Ctx {
    Panel&                   p;
    PanelInput const&        in;
    FontSet const&           fonts;
    PanelAssets const&       assets;
    std::vector<BfGlyphV1>&  out;
    std::vector<PanelAction> actions;
    Font const*              font = nullptr;
    float                    k = 1;          // open fade
    std::optional<std::array<float, 2>> m;   // mouse in design px
    bool                     press = false;  // left button went down this frame
    bool                     release = false;
    std::string              hotNow;         // topmost widget under the mouse this frame
    bool                     changed = false;

    bool inside(float x, float y, float w, float h) const {
        return m && (*m)[0] >= x && (*m)[0] <= x + w && (*m)[1] >= y && (*m)[1] <= y + h;
    }
    /// Register a hit area; returns the smoothed hover 0..1 (from last frame's hot id).
    float hit(std::string const& id, float x, float y, float w, float h) {
        if (inside(x, y, w, h)) hotNow = id;
        float& hv = p.mHover[id];
        hv += ((p.mHot == id ? 1.0f : 0.0f) - hv) * std::min(1.0f, in.dt * 14.0f);
        return hv;
    }
    bool clicked(std::string const& id) const { return press && p.mHot == id; }

    void quad(float cx, float cy, float hw, float hh, BfGlyphV1& q) {
        Vec3 c = p.toView(cx, cy), r = p.toView(cx + hw, cy) - c, u = p.toView(cx, cy - hh) - c;
        q.pos[0] = float(c.x); q.pos[1] = float(c.y); q.pos[2] = float(c.z);
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
    }
    /// Rounded rectangle; radius in px (>= half the smaller side = pill), soft = edge softness in screen px.
    void rect(float x, float y, float w, float h, float radius, uint32_t argb, float soft = 1.0f) {
        if (w <= 0 || h <= 0 || (argb >> 24) == 0) return;
        BfGlyphV1 q{};
        quad(x + w / 2, y + h / 2, w / 2, h / 2, q);
        q.fill = q.fill2 = toRgba(argb);
        q.alpha    = k;
        q.outlineW = std::clamp(radius / std::max(0.01f, std::min(w, h) * 0.5f), 0.0f, 1.0f);
        q.glowW    = soft;
        q.flags    = BF_GLYPH_SOLID | BF_GLYPH_VIEW;
        out.push_back(q);
    }
    void circle(float cx, float cy, float r, uint32_t argb, float soft = 1.0f) { rect(cx - r, cy - r, 2 * r, 2 * r, r, argb, soft); }
    void image(std::string const& name, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFF) {
        auto it = assets.ui.find(name);
        if (assets.uiAtlas < 0 || it == assets.ui.end()) return;
        imageAt(assets.uiAtlas, it->second, x, y, w, h, tint);
    }
    bool hasImage(std::string const& name) const { return assets.uiAtlas >= 0 && assets.ui.count(name); }
    void imageAt(int atlas, UiRect const& uv, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFF) {
        BfGlyphV1 q{};
        quad(x + w / 2, y + h / 2, w / 2, h / 2, q);
        q.uv[0] = uv.u0; q.uv[1] = uv.v0; q.uv[2] = uv.u1; q.uv[3] = uv.v1;
        q.fill = q.fill2 = toRgba(tint);
        q.alpha = k;
        q.atlas = uint32_t(atlas);
        q.flags = BF_GLYPH_IMAGE | BF_GLYPH_VIEW;
        out.push_back(q);
    }
    float textW(std::string_view s, float size, Font const* f = nullptr) const {
        f = f ? f : font;
        return f ? f->width(s) * size : float(s.size()) * size * 0.5f;
    }
    std::string fit(std::string s, float size, float maxW) const {
        if (maxW <= 0 || textW(s, size) <= maxW) return s;
        auto cps = text::codepoints(s);
        while (!cps.empty()) {
            cps.pop_back();
            std::string t;
            for (uint32_t cp : cps) text::append(t, cp);
            t += "..";
            if (textW(t, size) <= maxW) return t;
        }
        return "..";
    }
    /// Text with its vertical middle (cap height) on y. align: 0 left, 1 centre, 2 right.
    void text(std::string s, float x, float y, float size, uint32_t argb, int align = 0, float maxW = 0, Font const* f = nullptr) {
        f = f ? f : font;
        if (!f || s.empty()) return;
        if (maxW > 0) s = fit(std::move(s), size, maxW);
        float w = f->width(s) * size;
        float pen = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
        float base = y + f->capHeight * size * 0.5f; // baseline (y down)
        for (uint32_t cp : text::codepoints(s)) {
            GlyphMetrics const* g = f->find(cp);
            if (!g) g = f->find('?');
            float adv = g ? g->advance : 0.5f;
            if (g && !g->empty) {
                float cx = pen + (g->pl + g->pr) * 0.5f * size, cy = base - (g->pb + g->pt) * 0.5f * size;
                BfGlyphV1 q{};
                quad(cx, cy, (g->pr - g->pl) * 0.5f * size, (g->pt - g->pb) * 0.5f * size, q);
                q.uv[0] = g->u0; q.uv[1] = g->v0; q.uv[2] = g->u1; q.uv[3] = g->v1;
                q.fill = q.fill2 = toRgba(argb);
                q.alpha   = k;
                q.atlas   = f->atlasIndex;
                q.pxRange = f->pxRange;
                q.flags   = f->flags | BF_GLYPH_VIEW;
                out.push_back(q);
            }
            pen += adv * size;
        }
    }

    // ---- widgets (teaser look)
    /// Toggle switch (pill track + knob) at x,y (24 x 12).
    void switchAt(float x, float y, bool on, float hv) {
        rect(x, y, 26, 13, 6.5f, on ? mix(kAccent, 0xFFA28BFF, hv * 0.4f) : mix(kOff, 0xFF5A5470, hv));
        circle(on ? x + 19.5f : x + 6.5f, y + 6.5f, 4.6f, kKnob);
    }
    /// A labelled toggle inside a dark pill (the teaser's "Sky [on]" row). Returns true when flipped.
    bool togglePill(std::string const& id, std::string const& label, float x, float y, float w, bool& v) {
        float hv = hit(id, x, y, w, 24);
        rect(x, y, w, 24, 8, mix(kPill, kPillHot, hv));
        text(label, x + 10, y + 12, 8.5f, kText, 0, w - 50);
        switchAt(x + w - 34, y + 5.5f, v, hv);
        if (clicked(id)) { v = !v; changed = true; return true; }
        return false;
    }
    /// Slider row: label on the left, track on the right. Returns the (possibly dragged) value.
    float slider(std::string const& id, std::string const& label, float x, float y, float w, float v, float lo, float hi, std::string const& valueText = {}) {
        float const tx = x + 84, tw = w - 84 - (valueText.empty() ? 6 : 40);
        float hv = hit(id, tx - 8, y - 9, tw + 16, 18);
        text(label, x, y, 8.5f, kText, 0, 80);
        if (clicked(id)) p.mActive = id;
        if (p.mActive == id && in.down && m) {
            float t = std::clamp(((*m)[0] - tx) / tw, 0.0f, 1.0f);
            float nv = lo + t * (hi - lo);
            if (nv != v) { v = nv; changed = true; }
        }
        float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
        rect(tx, y - 4, tw, 8, 4, kTrack);
        rect(tx, y - 4, std::max(8.0f, tw * t), 8, 4, mix(0xFF9E86F4, kLavender, 0.6f + 0.4f * hv));
        circle(tx + tw * t, y, 7.0f + hv * 0.8f, kKnob);
        if (!valueText.empty()) text(valueText, x + w, y, 7.5f, kDim, 2);
        return v;
    }
    /// Segmented chips; returns the selected index.
    int chips(std::string const& id, std::vector<std::string> const& labels, int sel, float x, float y, float w, float h = 20) {
        float cw = w / float(labels.size());
        rect(x, y, w, h, h / 2, kPill);
        for (size_t i = 0; i < labels.size(); ++i) {
            std::string cid = id + "." + std::to_string(i);
            float hv = hit(cid, x + cw * float(i), y, cw, h);
            bool  on = int(i) == sel;
            if (on) rect(x + cw * float(i) + 2, y + 2, cw - 4, h - 4, (h - 4) / 2, kAccentTab);
            else if (hv > 0.02f) rect(x + cw * float(i) + 2, y + 2, cw - 4, h - 4, (h - 4) / 2, mix(kPill, kPillHot, hv));
            text(labels[i], x + cw * (float(i) + 0.5f), y + h / 2, 8.0f, on ? 0xFFFFFFFF : kDim, 1, cw - 8);
            if (clicked(cid) && int(i) != sel) { sel = int(i); changed = true; }
        }
        return sel;
    }
    /// Small round icon button (pager arrows, close).
    bool roundButton(std::string const& id, float cx, float cy, float r, std::string const& glyph) {
        float hv = hit(id, cx - r, cy - r, 2 * r, 2 * r);
        circle(cx, cy, r, mix(kChip, 0xFF3A3556, hv));
        text(glyph, cx, cy, 8.0f, kText, 1);
        return clicked(id);
    }
    void row(std::string const& label, float x, float y) { text(label, x, y, 7.5f, kFaint); }
};

// ---------------------------------------------------------------- placement / picking
void Panel::setOpen(bool on) {
    mOpen = on;
    if (!on) mActive.clear();
}

Vec3 Panel::toView(float x, float y) const {
    float const ease = 1.0f - std::pow(1.0f - std::clamp(mOpenT, 0.0f, 1.0f), 3.0f);
    double const s   = (1.0 / kPanelW) * double(mSize) * (0.94 + 0.06 * ease); // panel 1.0 units wide
    double const a   = 24.0 * kPi / 180.0;                                      // tilted back: top farther away
    Vec3 const   C{0.0, -0.235, 1.07};
    Vec3 const   R{1, 0, 0}, U{0, std::cos(a), std::sin(a)};
    Vec3 P = C + R * ((double(x) - kPanelW / 2) * s) + U * ((kPanelH / 2 - double(y)) * s);
    return {P.x / kViewTan, P.y / kViewTan, P.z};
}

std::optional<std::array<float, 2>> Panel::pick(float ndcX, float ndcY, float aspect) const {
    Vec3 const P0 = toView(0, 0), Rx = toView(1, 0) - P0, Dy = toView(0, 1) - P0;
    Vec3 const n  = Rx.cross(Dy);
    Vec3 const d{double(ndcX) * aspect, double(ndcY), 1.0}; // the VS maps view (x, y, z) to ndc (x / aspect / z, y / z)
    double den = d.dot(n);
    if (std::fabs(den) < 1e-12) return std::nullopt;
    double t = P0.dot(n) / den;
    if (t <= 0) return std::nullopt;
    Vec3 h = d * t - P0;
    return std::array<float, 2>{float(h.dot(Rx) / Rx.dot(Rx)), float(h.dot(Dy) / Dy.dot(Dy))};
}

// ---------------------------------------------------------------- frame
std::vector<PanelAction> Panel::frame(PanelInput const& in, Config& cfg, FontSet const& fonts, PanelAssets const& assets, PanelMedia const& media,
                                      std::vector<PresetInfo> const& presets, std::vector<BfGlyphV1>& out) {
    mOpenT = std::clamp(mOpenT + (mOpen ? 1.0f : -1.0f) * in.dt / 0.22f, 0.0f, 1.0f);
    if (mOpenT <= 0.0f) {
        mHot.clear();
        mWasDown = in.down;
        return {};
    }
    Ctx c{*this, in, fonts, assets, out};
    c.font = fonts.get(assets.pixelFont);
    c.k    = 1.0f - std::pow(1.0f - mOpenT, 2.0f);
    if (mOpen && in.mouseValid) c.m = pick(in.mouseNdcX, in.mouseNdcY, in.aspect);
    c.press   = mOpen && in.down && !mWasDown;
    c.release = !in.down && mWasDown;
    mWasDown  = in.down;
    if (!in.down && !mActive.empty()) {
        if (mActive == "seek" && mSeekPreview >= 0) c.actions.push_back({PanelAction::Kind::Seek, mSeekPreview, {}});
        mActive.clear();
        mSeekPreview = -1;
    }

    // cards: a stacked page behind each, then the card with a thin lavender edge
    auto card = [&](float x, float y, float w, float h) {
        c.rect(x - 7, y + 5, w, h, 14, kCardBack);
        c.rect(x - 7, y + 5, w, h, 14, 0x00000000);
        c.rect(x - 1.2f, y - 1.2f, w + 2.4f, h + 2.4f, 13, kEdge);
        c.rect(x, y, w, h, 12, kCard);
    };
    c.rect(-14, -10, kPanelW + 28, kPanelH + 24, 26, 0x40140A30, 18.0f); // soft purple glow around the whole panel
    card(10, 8, 262, 364);
    card(282, 8, 350, 364);

    drawPlayer(c, media);
    drawHeader(c, media, cfg);
    switch (mTab) {
    case Tab::Sky: pageSky(c, cfg); break;
    case Tab::Lyrics: pageLyrics(c, cfg); break;
    case Tab::Look: pageLook(c, cfg, fonts); break;
    case Tab::Presets: pagePresets(c, cfg, presets); break;
    case Tab::Other: pageOther(c, cfg); break;
    }
    drawTabs(c);

    // a press outside the panel closes it (like clicking off a Java screen)
    if (c.press && !c.m) c.actions.push_back({PanelAction::Kind::Close, 0, {}});
    else if (c.press && c.m && ((*c.m)[0] < 0 || (*c.m)[0] > kPanelW || (*c.m)[1] < 0 || (*c.m)[1] > kPanelH))
        c.actions.push_back({PanelAction::Kind::Close, 0, {}});

    mHot = c.hotNow;
    if (c.changed) c.actions.push_back({PanelAction::Kind::SaveConfig, 0, {}});
    return std::move(c.actions);
}

// ---------------------------------------------------------------- left card: the player
void Panel::drawPlayer(Ctx& c, PanelMedia const& media) {
    float const x = 10, w = 262;
    // cover art (square, thin lavender frame)
    float const cx = x + 24, cy = 22, cs = 214;
    c.rect(cx - 2, cy - 2, cs + 4, cs + 4, 3, kEdge);
    if (c.assets.cover >= 0 && media.hasTrack) c.imageAt(c.assets.cover, UiRect{}, cx, cy, cs, cs);
    else {
        c.rect(cx, cy, cs, cs, 2, 0xFF1C1A33);
        c.rect(cx, cy + cs * 0.55f, cs, cs * 0.45f, 2, 0xFF241F45);
        if (c.hasImage("icon.music")) c.image("icon.music", cx + cs / 2 - 22, cy + cs / 2 - 22, 44, 44, 0xFF6E66A0);
        else c.text("~", cx + cs / 2, cy + cs / 2, 30, 0xFF6E66A0, 1);
    }
    // title / artist
    std::string title  = media.hasTrack ? media.title : "Nothing playing";
    std::string artist = media.hasTrack ? media.artist : "Start your music";
    c.text(title, x + w / 2, 260, 12.5f, kText, 1, w - 30);
    c.text(artist, x + w / 2, 279, 9.0f, kDim, 1, w - 30);

    // progress (click / drag to seek)
    float const bx = x + 24, bw = w - 48, by = 296;
    float hv = c.hit("seek", bx - 4, by - 6, bw + 8, 14);
    if (c.clicked("seek") && media.hasTrack && media.durationMs > 0) mActive = "seek";
    int64_t pos = media.positionMs;
    if (mActive == "seek" && c.m && media.durationMs > 0) {
        float t      = std::clamp(((*c.m)[0] - bx) / bw, 0.0f, 1.0f);
        mSeekPreview = int64_t(double(t) * double(media.durationMs));
        pos          = mSeekPreview;
    }
    float t = media.durationMs > 0 ? std::clamp(float(double(pos) / double(media.durationMs)), 0.0f, 1.0f) : 0.0f;
    c.rect(bx, by, bw, 5, 2.5f, kBarBack);
    if (t > 0) c.rect(bx, by, std::max(5.0f, bw * t), 5, 2.5f, kGreen);
    if (hv > 0.05f || mActive == "seek") c.circle(bx + bw * t, by + 2.5f, 4.5f * std::max(hv, mActive == "seek" ? 1.0f : 0.0f), kKnob);
    c.text(fmtTime(pos), bx - 2, by + 15, 7.0f, kDim, 0);
    c.text(fmtTime(media.durationMs), bx + bw + 2, by + 15, 7.0f, kDim, 2);

    // source app
    std::string src = media.sourceApp.empty() ? "No player" : media.sourceApp;
    bool spotify = src == "Spotify", apple = src == "Apple Music";
    uint32_t logo = spotify ? kGreen : apple ? 0xFFFA2D48 : 0xFF6A6680;
    float const sw = c.textW(src, 9.0f) + 22, sx = x + w / 2 - sw / 2, sy = 332;
    std::string const logoImg = spotify ? "logo.spotify" : apple ? "logo.apple_music" : "";
    if (!logoImg.empty() && c.hasImage(logoImg)) c.image(logoImg, sx, sy - 8, 16, 16);
    else c.circle(sx + 8, sy, 8, logo);
    c.text(src, sx + 22, sy, 9.0f, kText);

    // controls: prev, play/pause, next, re-fetch lyrics
    struct B { char const* id; char const* icon; char const* fallback; PanelAction::Kind act; float x; };
    B const bs[] = {{"prev", "icon.prev", "|<", PanelAction::Kind::Prev, x + 64},
                    {"play", media.playing ? "icon.pause" : "icon.play", media.playing ? "||" : ">", PanelAction::Kind::PlayPause, x + 108},
                    {"next", "icon.next", ">|", PanelAction::Kind::Next, x + 152},
                    {"refetch", "icon.refresh", "o", PanelAction::Kind::Refetch, x + 196}};
    for (auto const& b : bs) {
        float hb = c.hit(b.id, b.x - 14, 344, 28, 24);
        if (hb > 0.02f) c.circle(b.x, 356, 12, mix(0x00000000, 0xFF2A2644, hb));
        float const sz = std::string(b.id) == "refetch" ? 9.0f : 11.0f;
        if (c.hasImage(b.icon)) c.image(b.icon, b.x - sz / 2, 356 - sz / 2, sz, sz, kText);
        else c.text(b.fallback, b.x, 356, 9.0f, kText, 1);
        if (c.clicked(b.id)) c.actions.push_back({b.act, 0, {}});
    }
}

// ---------------------------------------------------------------- right card header
void Panel::drawHeader(Ctx& c, PanelMedia const& media, Config&) {
    float const x = 282;
    c.circle(x + 20, 27, 7, 0x60B9A8FF, 5.0f); // glow
    c.circle(x + 20, 27, 3.2f, 0xFFFFFFFF);
    c.text("LyricsFlow", x + 34, 25, 11.0f, kText);
    std::string sub = media.hasTrack ? media.title + "  \xC2\xB7  " + media.artist : "Waiting for music";
    if (c.hasImage("icon.music")) c.image("icon.music", x + 34, 41, 8, 8, kDim);
    c.text(sub, x + 46, 45, 7.5f, kDim, 0, 200);

    // size chip ("Normal") and close
    std::string const sizeName = mSize < 0.95f ? "Compact" : mSize > 1.05f ? "Large" : "Normal";
    float const cw = c.textW(sizeName, 7.5f) + 18, chx = x + 350 - 44 - cw;
    float hv = c.hit("size", chx, 15, cw, 20);
    c.rect(chx, 15, cw, 20, 10, mix(kChip, 0xFF3A3556, hv));
    c.text(sizeName, chx + cw / 2, 25, 7.5f, kText, 1);
    if (c.clicked("size")) mSize = mSize < 0.95f ? 1.0f : mSize > 1.05f ? 0.85f : 1.18f;
    if (c.roundButton("close", x + 350 - 22, 25, 10, "x")) c.actions.push_back({PanelAction::Kind::Close, 0, {}});
}

// ---------------------------------------------------------------- bottom tabs
void Panel::drawTabs(Ctx& c) {
    static char const* const names[] = {"Sky", "Lyrics", "Look", "Presets", "Other"};
    float const x0 = 292, y = 334, w = 330, gap = 5, tw = (w - gap * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        std::string id = std::string("tab.") + names[i];
        float tx = x0 + float(i) * (tw + gap);
        float hv = c.hit(id, tx, y, tw, 26);
        bool  on = int(mTab) == i;
        c.rect(tx, y, tw, 26, 13, on ? kAccentTab : mix(kPill, kPillHot, hv));
        c.text(names[i], tx + tw / 2, y + 13, 8.5f, on ? 0xFFFFFFFF : 0xFFC9C5DA, 1, tw - 10);
        if (c.clicked(id) && !on) { mTab = Tab(i); mPage = 0; }
    }
}

// ---------------------------------------------------------------- pages
namespace {
constexpr float kPx = 296, kPw = 322; // content area of the right card
}

void Panel::pageSky(Ctx& c, Config& cfg) {
    bool sky = cfg.skyPreset != "off";
    if (c.togglePill("sky.on", "Sky", kPx, 58, 156, sky)) cfg.skyPreset = sky ? (cfg.skyPreset == "off" ? "night_lights" : cfg.skyPreset) : "off";
    c.togglePill("sky.stars", "Stars", kPx + 166, 58, 156, cfg.stars);

    auto const& ids   = skyPresetIds();
    int const   pages = int((ids.size() + 5) / 6);
    mPage             = std::clamp(mPage, 0, pages - 1);
    float const tw = 100, th = 50, gx = 11;
    for (int i = 0; i < 6; ++i) {
        size_t idx = size_t(mPage * 6 + i);
        if (idx >= ids.size()) break;
        std::string const& id = ids[idx];
        float tx = kPx + float(i % 3) * (tw + gx), ty = 92 + float(i / 3) * 72;
        float hv  = c.hit("sky." + id, tx, ty, tw, th + 14);
        bool  sel = cfg.skyPreset == id;
        if (sel || hv > 0.02f) c.rect(tx - 2.5f, ty - 2.5f, tw + 5, th + 5, 5, sel ? kAccentTab : mix(0x00000000, 0xFF4A4370, hv));
        if (c.hasImage("sky." + id)) c.image("sky." + id, tx, ty, tw, th);
        else c.rect(tx, ty, tw, th, 3, 0xFF221D3D);
        c.text(skyPresetName(id), tx + 1, ty + th + 9, 7.5f, sel ? kText : 0xFFB7B3C8, 0, tw);
        if (c.clicked("sky." + id)) { cfg.skyPreset = id; c.changed = true; }
    }
    // pager
    c.text(std::to_string(mPage + 1) + "/" + std::to_string(pages), kPx + kPw - 34, 238, 8.0f, kText, 1);
    if (c.roundButton("sky.prev", kPx + kPw - 60, 238, 9, "<")) mPage = (mPage + pages - 1) % pages;
    if (c.roundButton("sky.next", kPx + kPw - 8, 238, 9, ">")) mPage = (mPage + 1) % pages;

    cfg.starDensity   = c.slider("sky.stard", "Star density", kPx, 262, kPw, cfg.starDensity, 0.0f, 1.0f);
    cfg.streakDensity = c.slider("sky.streak", "Streaks", kPx, 286, kPw, cfg.streakDensity, 0.0f, 1.0f);
    cfg.moteDensity   = c.slider("sky.motes", "Motes", kPx, 310, kPw, cfg.moteDensity, 0.0f, 1.0f);
}

void Panel::pageLyrics(Ctx& c, Config& cfg) {
    c.togglePill("ly.show", "Show lyrics", kPx, 58, 156, cfg.showLyrics);
    c.togglePill("ly.fall", "Falling letters", kPx + 166, 58, 156, cfg.fallingLetters);
    c.row("Lyrics style", kPx, 98);
    int st = c.chips("ly.style", {"Sky", "Floating", "Screen", "Wall"},
                     cfg.lyricsStyle == LyricsStyle::Sky ? 0 : cfg.lyricsStyle == LyricsStyle::Floating ? 1 : cfg.lyricsStyle == LyricsStyle::Screen ? 2 : 3,
                     kPx, 106, kPw);
    cfg.lyricsStyle = st == 0 ? LyricsStyle::Sky : st == 1 ? LyricsStyle::Floating : st == 2 ? LyricsStyle::Screen : LyricsStyle::Wall;
    c.row("Words appear", kPx, 142);
    int en          = c.chips("ly.enter", {"Word by word", "Fade", "Rise"}, int(cfg.enterAnim), kPx, 150, kPw);
    cfg.enterAnim   = EnterAnim(en);
    c.row("Lines on screen", kPx, 186);
    int ml          = c.chips("ly.lines", {"1", "2", "3"}, std::clamp(cfg.maxLines, 1, 3) - 1, kPx, 194, kPw);
    cfg.maxLines    = ml + 1;
    cfg.lyricsScale    = c.slider("ly.size", "Size", kPx, 238, kPw, cfg.lyricsScale, 0.4f, 3.0f, fmtF("%.2fx", cfg.lyricsScale));
    cfg.lyricsDistance = c.slider("ly.dist", "Distance", kPx, 262, kPw, cfg.lyricsDistance, 0.4f, 3.0f, fmtF("%.2fx", cfg.lyricsDistance));
    cfg.textOpacity    = c.slider("ly.opac", "Opacity", kPx, 286, kPw, cfg.textOpacity, 0.1f, 1.0f, fmtF("%.0f%%", cfg.textOpacity * 100));
    cfg.animSpeed      = c.slider("ly.anim", "Anim speed", kPx, 310, kPw, cfg.animSpeed, 0.25f, 3.0f, fmtF("%.2fx", cfg.animSpeed));
}

void Panel::pageLook(Ctx& c, Config& cfg, FontSet const& fonts) {
    bool neon = cfg.textStyle == TextStyle::Neon;
    if (c.togglePill("look.neon", "Neon layers", kPx, 58, 156, neon)) cfg.textStyle = neon ? TextStyle::Neon : TextStyle::Clean;
    c.togglePill("look.glow", "Glow", kPx + 166, 58, 156, cfg.glow);

    // font tiles: "Aa" in the font itself + its name
    std::vector<Font const*> fl;
    for (auto const& f : fonts.all()) fl.push_back(&f);
    int const pages = std::max(1, int((fl.size() + 5) / 6));
    mPage           = std::clamp(mPage, 0, pages - 1);
    float const tw = 100, th = 50, gx = 11;
    for (int i = 0; i < 6; ++i) {
        size_t idx = size_t(mPage * 6 + i);
        if (idx >= fl.size()) break;
        Font const* f   = fl[idx];
        float       tx  = kPx + float(i % 3) * (tw + gx), ty = 92 + float(i / 3) * 72;
        float       hv  = c.hit("font." + f->id, tx, ty, tw, th + 14);
        bool        sel = cfg.font == f->id;
        if (sel || hv > 0.02f) c.rect(tx - 2.5f, ty - 2.5f, tw + 5, th + 5, 5, sel ? kAccentTab : mix(0x00000000, 0xFF4A4370, hv));
        c.rect(tx, ty, tw, th, 4, 0xFF1B1830);
        c.text("Aa", tx + tw / 2, ty + th / 2, 22.0f, kText, 1, tw - 8, f);
        c.text(fontLabel(f->id), tx + 1, ty + th + 9, 7.5f, sel ? kText : 0xFFB7B3C8, 0, tw);
        if (c.clicked("font." + f->id)) { cfg.font = f->id; c.changed = true; }
    }
    c.text(std::to_string(mPage + 1) + "/" + std::to_string(pages), kPx + kPw - 34, 238, 8.0f, kText, 1);
    if (c.roundButton("font.prev", kPx + kPw - 60, 238, 9, "<")) mPage = (mPage + pages - 1) % pages;
    if (c.roundButton("font.next", kPx + kPw - 8, 238, 9, ">")) mPage = (mPage + 1) % pages;

    cfg.glowStrength = c.slider("look.glowk", "Glow", kPx, 262, kPw, cfg.glowStrength, 0.0f, 1.0f, fmtF("%.0f%%", cfg.glowStrength * 100));
    cfg.outlineWidth = c.slider("look.outl", "Outline", kPx, 286, kPw, cfg.outlineWidth, 0.0f, 0.35f, fmtF("%.2f", cfg.outlineWidth));
    cfg.italic       = c.slider("look.ital", "Italic", kPx, 310, kPw, cfg.italic, 0.0f, 0.4f, fmtF("%.2f", cfg.italic));
}

void Panel::pagePresets(Ctx& c, Config& cfg, std::vector<PresetInfo> const& presets) {
    c.togglePill("pre.combat", "Combat safe", kPx, 58, 156, cfg.combatSafe);
    c.togglePill("pre.caps", "All caps", kPx + 166, 58, 156, cfg.allCaps);
    std::vector<PresetInfo const*> list;
    for (auto const& pr : presets)
        if (pr.id != "combat_safe") list.push_back(&pr); // Combat Safe is the toggle above, not a look
    int const pages = std::max(1, int((list.size() + 5) / 6));
    mPage           = std::clamp(mPage, 0, pages - 1);
    float const tw = 100, th = 50, gx = 11;
    for (int i = 0; i < 6; ++i) {
        size_t idx = size_t(mPage * 6 + i);
        if (idx >= list.size()) break;
        PresetInfo const& pr = *list[idx];
        float tx = kPx + float(i % 3) * (tw + gx), ty = 92 + float(i / 3) * 72;
        float hv = c.hit("preset." + pr.id, tx, ty, tw, th + 14);
        if (hv > 0.02f) c.rect(tx - 2.5f, ty - 2.5f, tw + 5, th + 5, 5, mix(0x00000000, kAccentTab, hv));
        if (c.hasImage("preset." + pr.id)) c.image("preset." + pr.id, tx, ty, tw, th);
        else c.rect(tx, ty, tw, th, 3, 0xFF221D3D);
        c.text(pr.name, tx + 1, ty + th + 9, 7.5f, 0xFFB7B3C8, 0, tw);
        if (c.clicked("preset." + pr.id)) c.actions.push_back({PanelAction::Kind::ApplyPreset, 0, pr.id});
    }
    c.text(std::to_string(mPage + 1) + "/" + std::to_string(pages), kPx + kPw - 34, 238, 8.0f, kText, 1);
    if (c.roundButton("pre.prev", kPx + kPw - 60, 238, 9, "<")) mPage = (mPage + pages - 1) % pages;
    if (c.roundButton("pre.next", kPx + kPw - 8, 238, 9, ">")) mPage = (mPage + 1) % pages;
    c.text("Click a look to apply it", kPx, 238, 7.5f, kFaint);

    cfg.keepOutDeg  = c.slider("pre.keep", "Clear aim", kPx, 262, kPw, cfg.keepOutDeg, 0.0f, 30.0f, fmtF("%.0f deg", cfg.keepOutDeg));
    cfg.combatAlpha = c.slider("pre.calpha", "Fight dim", kPx, 286, kPw, cfg.combatAlpha, 0.0f, 1.0f, fmtF("%.0f%%", cfg.combatAlpha * 100));
    cfg.letterLifetimeMs = int(c.slider("pre.life", "Letters stay", kPx, 310, kPw, float(cfg.letterLifetimeMs), 1000.0f, 15000.0f,
                                        fmtF("%.1fs", cfg.letterLifetimeMs / 1000.0)));
}

void Panel::pageOther(Ctx& c, Config& cfg) {
    c.togglePill("oth.top", "Top bar", kPx, 58, 156, cfg.showTopBar);
    c.togglePill("oth.only", "Only when playing", kPx + 166, 58, 156, cfg.showOnlyWhenPlaying);
    c.row("Music from", kPx, 98);
    int const cur = cfg.source == Source::Any ? 0 : cfg.source == Source::AppleMusic ? 1 : cfg.source == Source::Spotify ? 2 : 3;
    int src       = c.chips("oth.src", {"Any", "Apple Music", "Spotify", "YouTube"}, cur, kPx, 106, kPw);
    cfg.source    = src == 0 ? Source::Any : src == 1 ? Source::AppleMusic : src == 2 ? Source::Spotify : Source::Youtube;
    c.row("Top bar style", kPx, 142);
    int tb          = c.chips("oth.tbs", {"Karaoke", "Simple"}, int(cfg.topBarStyle), kPx, 150, kPw);
    cfg.topBarStyle = TopBarStyle(tb);
    c.text("LyricsFlow by 374m (MIT) - Bedrock port, personal build", kPx, 194, 7.0f, kFaint, 0, kPw);
    c.text("Keys: O on/off, [ ] sync -/+ 100 ms", kPx, 208, 7.0f, kFaint, 0, kPw);

    cfg.syncOffsetMs = int(std::lround(c.slider("oth.sync", "Sync offset", kPx, 238, kPw, float(cfg.syncOffsetMs), -2000.0f, 2000.0f,
                                                 fmtF("%+.0f ms", double(cfg.syncOffsetMs))) / 10.0f) * 10);
    cfg.topBarScale  = c.slider("oth.tbscale", "Top bar size", kPx, 262, kPw, cfg.topBarScale, 0.5f, 2.0f, fmtF("%.2fx", cfg.topBarScale));
    cfg.gravity      = c.slider("oth.grav", "Letter gravity", kPx, 286, kPw, cfg.gravity, 4.0f, 40.0f, fmtF("%.0f", cfg.gravity));
    cfg.bounce       = c.slider("oth.bounce", "Letter bounce", kPx, 310, kPw, cfg.bounce, 0.0f, 0.9f, fmtF("%.2f", cfg.bounce));
}

} // namespace bf::lyrics
