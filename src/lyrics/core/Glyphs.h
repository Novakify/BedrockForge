// LyricsFlow port - font atlases and DrawWord -> glyph quads. Original code for Bedrock Forge.
// Fonts are prebuilt SDF/MSDF atlases (tools/LyricsFonts) described by an msdf-atlas-gen style JSON (atlas, metrics,
// glyphs with planeBounds/atlasBounds in em units). The quad builder reproduces LyricsFlow's NEON look
// (WorldLyrics.neon3d / Gfx.neon: shadow passes, two offset colour passes, main pass, stacked along z) and adds
// what the SDF shader can do on top (outline, glow, gradient, per-letter / animated colour).
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Scene.h"

namespace bf::lyrics {

struct GlyphMetrics {
    float advance = 0.5f;
    float pl = 0, pb = 0, pr = 0, pt = 0; // plane bounds, em units, y up, relative to pen/baseline
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas uv (v0 = top)
    bool  empty = true;                   // space etc.
};

struct Font {
    std::string id;
    std::string atlasPath;  // PNG
    uint32_t    atlasIndex = 0;
    uint32_t    flags      = 0; // BF_GLYPH_BITMAP / BF_GLYPH_MSDF
    float       pxRange    = 4.0f;
    float       ascender = 0.8f, descender = -0.2f, capHeight = 0.7f, xHeight = 0.5f, lineHeight = 1.2f;
    std::unordered_map<uint32_t, GlyphMetrics> glyphs;

    GlyphMetrics const* find(uint32_t cp) const;
    /// Width of text in em (sum of advances). Missing glyphs fall back to '?' then 0.5 em.
    float               width(std::string_view text) const;
};

class FontSet {
public:
    /// Load <dir>/<id>.json (+ .png). Returns false if the JSON can't be read.
    bool        loadFromJson(std::string const& id, std::string_view jsonText, std::string const& atlasPath);
    Font const* get(std::string_view id) const; // falls back to the first font that has an atlas slot
    /// [BF] RF has a fixed number of texture slots: only fonts in use get one (Runtime assigns them, LRU).
    static constexpr uint32_t kNoAtlas = 0xFFFFFFFFu;
    void        setAtlasIndex(std::string_view id, uint32_t index);
    bool        has(std::string_view id) const;
    size_t      size() const { return mFonts.size(); }
    std::vector<Font> const& all() const { return mFonts; }
    /// TextWidthFn for the Scene: width in units of the line height LyricsFlow uses (em).
    float       width(std::string_view text, std::string_view font) const;

private:
    std::vector<Font> mFonts;
};

/// RGBA8 (R low byte) from LyricsFlow ARGB.
uint32_t toRgba(uint32_t argb, float alphaMul = 1.0f);
uint32_t hueRotate(uint32_t argb, float turns);

struct GlyphStyle {
    Theme    theme;
    uint32_t shadowSoft = 0, shadowStrong = 0;
    float    timeSeconds = 0;
};
GlyphStyle makeStyle(Config const& c, float timeSeconds);

/// Append the quads of one word. Positions are relative to `origin`. passes = neon/shadow layering.
void buildWordGlyphs(DrawWord const& w, Font const& font, Config const& c, GlyphStyle const& st, Vec3 origin, std::vector<BfGlyphV1>& out,
                     bool mainPassOnly = false);

} // namespace bf::lyrics
