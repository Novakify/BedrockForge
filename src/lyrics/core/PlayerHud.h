// LyricsForge - [BF] corner music player (the idea of LyricsFlow 2.2's Extra tab "Music player"; original code for
// Bedrock Forge): cover, song + artist, time bar, equalizer, and optionally the line being sung plus the next two.
// Screen-mode quads (BF_GLYPH_SCREEN). Rounded (LyricsForge look) or square (Minecraft look).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Scene.h"

namespace bf::lyrics {

class PlayerHud {
public:
    struct Extra {
        int64_t  durationMs = 0;
        int      coverAtlas = -1;   // atlas index of the cover art, -1 = none
        uint32_t coverAccent = 0;   // 0xRRGGBB from the cover, 0 = none
    };
    void build(SceneInput const& in, Extra const& ex, Config const& c, FontSet const& fonts, std::vector<BfGlyphV1>& out);
    float visible() const { return mVis; }

private:
    float mVis = 0, mBars[5]{}, mPos = 0;
    struct Fit { std::string font, in, out; float maxEm = 0; };
    std::vector<Fit> mFits; // the ellipsized texts of the last frames (title, artist, lines): no re-measuring per frame
    std::string const& fitCached(FontSet const& f, std::string const& font, std::string const& s, float maxEm);
    int64_t mSeq = -1;
};

} // namespace bf::lyrics
