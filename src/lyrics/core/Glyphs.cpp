// LyricsFlow port - font atlases + glyph quads. Original code for Bedrock Forge; the neon pass layout follows
// LyricsFlow v2.0's WorldLyrics.neon3d / Gfx.neon (374m, MIT).
#include "lyrics/core/Glyphs.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Json.h"
#include "lyrics/core/Text.h"

namespace bf::lyrics {

// ---------------------------------------------------------------- fonts
GlyphMetrics const* Font::find(uint32_t cp) const {
    auto it = glyphs.find(cp);
    return it == glyphs.end() ? nullptr : &it->second;
}
float Font::width(std::string_view s) const {
    float w = 0;
    for (uint32_t cp : text::codepoints(s)) {
        GlyphMetrics const* g = find(cp);
        if (!g) g = find('?');
        w += g ? g->advance : 0.5f;
    }
    return w;
}

bool FontSet::loadFromJson(std::string const& id, std::string_view jsonText, std::string const& atlasPath) {
    auto doc = json::parse(jsonText);
    if (!doc || !doc->isObject()) return false;
    auto const& at = (*doc)["atlas"];
    auto const& m  = (*doc)["metrics"];
    Font        f;
    f.id         = id;
    f.atlasPath  = atlasPath;
    f.atlasIndex = uint32_t(mFonts.size());
    std::string type = at["type"].asString();
    if (type == "msdf" || type == "mtsdf") f.flags |= BF_GLYPH_MSDF;
    if (type == "bitmap" || type == "hardmask") f.flags |= BF_GLYPH_BITMAP;
    f.pxRange    = float(at["distanceRange"].asNumber(4.0));
    double W = at["width"].asNumber(1.0), H = at["height"].asNumber(1.0);
    bool   top   = at["yOrigin"].asString() == "top";
    double em    = m["emSize"].asNumber(1.0);
    if (em <= 0) em = 1.0;
    f.lineHeight = float(m["lineHeight"].asNumber(1.2) / em);
    f.ascender   = float(m["ascender"].asNumber(0.8) / em);
    f.descender  = float(m["descender"].asNumber(-0.2) / em);
    f.capHeight  = float(m["capHeight"].asNumber(f.ascender * 0.86) / em);
    f.xHeight    = float(m["xHeight"].asNumber(f.capHeight * 0.68) / em);
    for (auto const& g : (*doc)["glyphs"].items()) {
        GlyphMetrics gm;
        gm.advance = float(g["advance"].asNumber(0.5) / em);
        if (g.has("planeBounds") && g.has("atlasBounds")) {
            auto const& p = g["planeBounds"];
            auto const& a = g["atlasBounds"];
            gm.pl = float(p["left"].asNumber() / em);
            gm.pb = float(p["bottom"].asNumber() / em);
            gm.pr = float(p["right"].asNumber() / em);
            gm.pt = float(p["top"].asNumber() / em);
            gm.u0 = float(a["left"].asNumber() / W);
            gm.u1 = float(a["right"].asNumber() / W);
            if (top) { gm.v0 = float(a["top"].asNumber() / H); gm.v1 = float(a["bottom"].asNumber() / H); }
            else { gm.v0 = float(1.0 - a["top"].asNumber() / H); gm.v1 = float(1.0 - a["bottom"].asNumber() / H); }
            gm.empty = false;
        }
        f.glyphs[uint32_t(g["unicode"].asInt())] = gm;
    }
    if (f.glyphs.empty()) return false;
    mFonts.push_back(std::move(f));
    return true;
}
Font const* FontSet::get(std::string_view id) const {
    for (auto const& f : mFonts)
        if (f.id == id && f.atlasIndex != kNoAtlas) return &f;
    for (auto const& f : mFonts)
        if (f.atlasIndex != kNoAtlas) return &f;
    return mFonts.empty() ? nullptr : &mFonts.front();
}
void FontSet::setAtlasIndex(std::string_view id, uint32_t index) {
    for (auto& f : mFonts)
        if (f.id == id) f.atlasIndex = index;
}
bool FontSet::has(std::string_view id) const {
    for (auto const& f : mFonts)
        if (f.id == id) return true;
    return false;
}
float FontSet::width(std::string_view s, std::string_view font) const {
    Font const* f = get(font);
    return f ? f->width(s) : 0.5f * float(text::length(s));
}

// ---------------------------------------------------------------- colours
uint32_t toRgba(uint32_t argb, float alphaMul) {
    uint32_t a = uint32_t(std::clamp(float(argb >> 24) * alphaMul, 0.0f, 255.0f) + 0.5f);
    uint32_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}
uint32_t hueRotate(uint32_t c, float turns) {
    if (turns == 0.0f) return c;
    float r = float((c >> 16) & 0xFF) / 255.0f, g = float((c >> 8) & 0xFF) / 255.0f, b = float(c & 0xFF) / 255.0f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn, h = 0;
    if (d > 1e-6f) {
        if (mx == r) h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
    float s = mx > 0 ? d / mx : 0, v = mx;
    h       = h + turns;
    h -= std::floor(h);
    float hf = h * 6.0f;
    int   i  = int(hf) % 6;
    float f = hf - std::floor(hf), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float rr, gg, bb;
    switch (i) {
    case 0: rr = v; gg = t; bb = p; break;
    case 1: rr = q; gg = v; bb = p; break;
    case 2: rr = p; gg = v; bb = t; break;
    case 3: rr = p; gg = q; bb = v; break;
    case 4: rr = t; gg = p; bb = v; break;
    default: rr = v; gg = p; bb = q; break;
    }
    auto to8 = [](float x) { return uint32_t(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (c & 0xFF000000u) | (to8(rr) << 16) | (to8(gg) << 8) | to8(bb);
}

GlyphStyle makeStyle(Config const& c, float timeSeconds) {
    GlyphStyle st;
    st.theme       = currentTheme(c);
    st.timeSeconds = timeSeconds;
    bool clean     = c.textStyle == TextStyle::Clean;
    if (c.shadowColor != 0) st.shadowSoft = st.shadowStrong = uint32_t(c.shadowColor) | 0xFF000000u;
    else {
        st.shadowSoft   = clean ? 0xFF15132Au : colDarken(st.theme.a, 0.75f);
        st.shadowStrong = 0xFF0A0612u;
    }
    return st;
}

// ---------------------------------------------------------------- quads
namespace {
struct Pass {
    float    ox, oy; // em, y up
    uint32_t argb;
    float    alpha;
    bool     main;
};
} // namespace

void buildWordGlyphs(DrawWord const& w, Font const& font, Config const& c, GlyphStyle const& st, Vec3 origin, std::vector<BfGlyphV1>& out,
                     bool mainPassOnly) {
    if (w.alpha <= 0.01f || w.text.empty()) return;
    Theme const& th    = st.theme;
    bool const   clean = c.textStyle == TextStyle::Clean;
    float const  o     = 0.055f; // Gfx.neon offset unit (h * 0.055)
    float const  hue   = (c.fxRainbow && c.hueShiftSpeed == 0.0f ? 0.12f : c.hueShiftSpeed) * st.timeSeconds; // [BF] Rainbow

    std::vector<Pass> passes;
    if (w.plain) mainPassOnly = true;
    if (!mainPassOnly) {
        if (c.textShadow == 1) passes.push_back({o * 0.6f, -o * 1.0f, st.shadowSoft, w.alpha, false});
        else if (c.textShadow == 2) {
            passes.push_back({o * 1.3f, -o * 2.0f, st.shadowStrong, w.alpha, false});
            passes.push_back({o * 0.7f, -o * 1.1f, st.shadowStrong, w.alpha, false});
        }
    }
    if (clean) passes.push_back({0, 0, colLerp(th.main, 0xFFFFFFFFu, 0.2f), w.alpha, true});
    else {
        float tcs = c.twoColorStrength;
        if (!mainPassOnly && c.twoColor && tcs > 0.01f) {
            uint32_t ca = c.twoColorColor != 0 ? uint32_t(c.twoColorColor) | 0xFF000000u : th.a;
            uint32_t cb = c.twoColorColor != 0 ? uint32_t(c.twoColorColor) | 0xFF000000u : th.b;
            float    oo = o * (0.35f + 0.65f * tcs);
            float    ta = w.alpha * std::min(1.0f, 0.3f + 0.7f * tcs);
            passes.push_back({-oo, -oo * 0.5f, hueRotate(ca, hue), ta, false});
            passes.push_back({oo, oo * 0.5f, hueRotate(cb, hue), ta, false});
        }
        passes.push_back({0, 0, hueRotate(th.main, hue), w.alpha, true});
    }
    if (w.argb) passes.back().argb = w.argb;

    // layout in em: centred on x, visual middle (between x-height and cap-height) on y = 0
    auto const  cps      = text::codepoints(w.text);
    float const total    = font.width(w.text);
    float const baseline = -(font.capHeight + font.xHeight) * 0.25f;
    uint32_t    flags    = font.flags | (w.screen ? BF_GLYPH_SCREEN : 0u) | (c.gradient ? BF_GLYPH_GRADIENT : 0u);
    uint32_t const outlineArgb = c.outlineColor != 0 ? uint32_t(c.outlineColor) : colDarken(th.a, 0.75f);
    uint32_t const glowArgb    = c.glowColor != 0 ? uint32_t(c.glowColor) : th.a;
    uint32_t const grad2Argb   = c.gradientColor != 0 ? uint32_t(c.gradientColor) : th.b;
    float const    glowK       = (c.glow && c.glowStrength > 0) ? std::min(1.0f, c.glowStrength * std::clamp(w.glow, 0.0f, 1.5f)) : 0.0f;
    float const    glowW       = c.glowWidth * (0.6f + 0.4f * std::min(1.0f, w.glow));

    double const sinR = std::sin(double(w.rotZ)), cosR = std::cos(double(w.rotZ));
    double const tilt = w.screen ? 2.0 * std::atan2(w.orient.z, w.orient.w) : 0.0;
    double const sinT = std::sin(tilt), cosT = std::cos(tilt);

    // local em point (lx, ly, z-layer) -> output position
    auto place = [&](double lx, double ly, double lz) -> Vec3 {
        double s = double(w.scale);
        if (w.screen) { // y-down screen units
            double gx = lx * s, gy = -ly * s;
            double rx = gx * cosR - gy * sinR, ry = gx * sinR + gy * cosR;
            rx += w.x;
            ry += w.y;
            double tx = rx * cosT - ry * sinT, ty = rx * sinT + ry * cosT;
            return Vec3{w.origin.x + tx, w.origin.y + ty, 0};
        }
        double gx = lx * s, gy = ly * s;
        Vec3   local{gx * cosR - gy * sinR + w.x, gx * sinR + gy * cosR + w.y, lz};
        return w.origin + w.orient.rotate(local) - origin;
    };
    auto vecOut = [&](double lx, double ly) -> Vec3 { // direction (no translation)
        Vec3 a = place(lx, ly, 0), b = place(0, 0, 0);
        return a - b;
    };

    int layer = 0;
    for (auto const& ps : passes) {
        float zEm = w.wall ? 0.006f * float(layer) / std::max(1e-4f, w.scale) : w.layerStep * float(layer) / 16.0f;
        ++layer;
        if ((ps.argb >> 24) < 26) continue; // Java skips nearly transparent passes
        float pen = -total / 2 + ps.ox;
        int   letter = 0;
        for (uint32_t cp : cps) {
            GlyphMetrics const* g = font.find(cp);
            if (!g) g = font.find('?');
            float adv = g ? g->advance : 0.5f;
            if (g && !g->empty) {
                double   cx = double(pen) + (g->pl + g->pr) * 0.5, cy = double(baseline + ps.oy) + (g->pb + g->pt) * 0.5;
                if (c.fxWave && !w.plain) // [BF] Wave: letters ripple along the word
                    cy += 0.11 * std::sin(double(st.timeSeconds) * 4.2 - double(letter) * 0.75 + double(w.key % 7));
                double   hx = (g->pr - g->pl) * 0.5, hy = (g->pt - g->pb) * 0.5;
                double const sk = w.plain ? 0.0 : double(c.italic); // [BF] slant (not the top bar): quad -> parallelogram, x += sk * y
                Vec3     p  = place(cx + sk * (cy - double(baseline)), cy, double(zEm));
                Vec3     r  = vecOut(hx, 0), u = vecOut(sk * hy, hy);
                BfGlyphV1 q{};
                q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
                q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
                q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
                q.uv[0] = g->u0; q.uv[1] = g->v0; q.uv[2] = g->u1; q.uv[3] = g->v1;
                uint32_t fillArgb = ps.argb;
                if (ps.main && c.perLetterColor && !clean && !w.argb) {
                    uint32_t const cyc[3] = {th.main, th.a, th.b};
                    fillArgb              = hueRotate(cyc[letter % 3], hue);
                }
                q.fill    = toRgba(fillArgb);
                q.fill2   = toRgba(ps.main ? hueRotate(grad2Argb, hue) : fillArgb);
                q.alpha   = ps.alpha;
                q.atlas   = font.atlasIndex;
                q.flags   = flags;
                if (!ps.main) q.flags &= ~uint32_t(BF_GLYPH_GRADIENT);
                q.pxRange = font.pxRange;
                if (w.plain) q.flags &= ~uint32_t(BF_GLYPH_GRADIENT);
                if (ps.main && !w.plain) {
                    q.outline  = c.outlineWidth > 0 ? toRgba(outlineArgb) : 0u;
                    q.outlineW = c.outlineWidth;
                    q.glow     = glowK > 0 ? toRgba(hueRotate(glowArgb, hue)) : 0u;
                    q.glowW    = glowW;
                    q.glowK    = glowK;
                }
                out.push_back(q);
            }
            pen += adv;
            ++letter;
        }
    }
}

} // namespace bf::lyrics
