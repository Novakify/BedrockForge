// LyricsFlow port - the top "now playing" / karaoke bar (dev.lyricsflow.render.TopBar, LyricsFlow v2.0 by 374m,
// MIT; ported for Fernando's personal build). It is in 3 of the 4 reference videos (B, C, D).
// Ported: phases (title 3.2 s -> lyrics, paused, resumed), slide-in, width easing, rounded pill + shadow + outline,
// vinyl / pause icon, title·artist line, karaoke line with the highlight pill gliding to the sung word, sung/upcoming
// word colours, word-follow scrolling, 4 equalizer bars. Not yet: cover art, Spotify/YouTube logos, clock/ping/fps
// chips, the beat glow. Output: screen-mode glyph quads (BF_GLYPH_SCREEN, + BF_GLYPH_SOLID for shapes).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Scene.h"

namespace bf::lyrics {

class TopBar {
public:
    /// Append this frame's bar. accent = Themes.accent(): accentColor or lerp(theme.a, #8B5CF6, 0.45).
    void build(SceneInput const& in, Config const& c, FontSet const& fonts, float aspect, std::vector<BfGlyphV1>& out);
    void reset() { *this = TopBar{}; }

    enum class Phase { Title, Lyrics, Paused, Resumed };
    Phase phase() const { return mPhase; }
    float width() const { return mWidth; }
    float highlightX() const { return mHlX; }
    int   lastWord() const { return mWord; }

private:
    Phase       mPhase = Phase::Title;
    float       mPhaseAge = 0, mVis = 0, mWidth = 150, mSwap = 1, mScroll = 0, mHlX = 0, mHlW = 0, mSpin = 0;
    float       mBars[4]{};
    bool        mHlInit = false;
    int64_t     mSeq = -1;
    bool        mPlaying = false;
    float       mPausedFor = 0;
    std::string mKey;
    int         mWord = -1;
};

/// Rounded rectangle quad in screen units (x right, y down, 0..aspect / 0..1). radius 1 = pill.
BfGlyphV1 solidRect(float x, float y, float w, float h, uint32_t argb, float alpha, float radius = 1.0f, float softPx = 0.0f);

} // namespace bf::lyrics
