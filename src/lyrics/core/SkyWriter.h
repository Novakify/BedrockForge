// LyricsForge - [BF] "The sky writes the song" (original code for Bedrock Forge).
//
// Every time a hook / chorus line finishes, its letters rise into the sky and become a constellation: one star per
// letter, thin light lines joining the letters of each word, keeping the word's shape. The constellations stay for the
// whole song (gently twinkling with the treble), so by the last chorus the sky holds the song's story. In the song's
// final seconds they all light up at once; when the song ends they dissolve. Merges the vision doc's Constellation
// Lyrics (3.2) and Visual Song Memories (33). Sky-anchored: drawn at a fixed direction far away, so it stays put in
// the sky while you walk.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Conductor.h"
#include "lyrics/core/Lyrics.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

class SkyWriter {
public:
    struct Star { Vec3 dir; float delay = 0; int word = 0; float mag = 1; };
    struct Constellation {
        std::vector<Star> stars;
        std::vector<std::pair<int, int>> links;
        float age = 0;        // s since it was written
        float fade = 1;       // 1 alive, falls to 0 while dissolving
        std::string text;
    };
    struct In {
        int64_t           t = -1;
        int64_t           songSeq = -1;
        bool              playing = false;
        Lyrics const*     lyrics = nullptr;
        MusicState const* music = nullptr;
        float             dt = 1.0f / 60.0f;
        float             camYaw = 0, camPitch = 0;
        Vec3              camPos;
        uint32_t          color = 0xFFB9A2FF; // star tint (ARGB)
        bool              enabled = true;
        double            now = 0;            // s, twinkle clock
        float             skyLuma = 0;        // bright sky behind: dark-edged stars so they still read
    };

    void step(In const& in);
    /// [LyricsForge] constellation outro: the song's final line rises into the sky ahead of you as stars (any sky), holds
    /// kFinaleHold seconds - through the next song's chapter if one starts - then dissolves with the outro
    void finale(Line const& l, In const& in);
    bool finaleShowing() const { return !mEnd.empty(); }
    /// star + link quads (world positions relative to `origin`)
    void emit(In const& in, Vec3 const& origin, std::vector<BfGlyphV1>& out) const;
    std::vector<Constellation> const& constellations() const { return mCons; }
    void clear() { mCons.clear(); mDying.clear(); mEnd.clear(); mSeq = -2; mLastEnd = -1; mLastLine = -1; } // [BF] lyrics swapped: start over

    static constexpr int   kMaxPerSong = 7;
    static constexpr float kRadius     = 160.0f; // blocks: far, but inside the depth range
    static constexpr float kFinaleHold = 4.5f;

private:
    void write(Line const& l, In const& in);
    bool make(Line const& l, float az, float elev, uint32_t seedK, bool letters, Constellation& c) const;
    std::vector<Constellation> mCons, mDying, mEnd; // mEnd: the finale (outlives the song change, not tied to the sky)
    int64_t mSeq = -2;
    int     mLastLine = -1;
    float   mSinceWrite = 1e9f;
    float   mFinale = 0;
    int64_t mLastEnd = -1;
    mutable std::vector<Vec3>  mPos;   // [BF] emit scratch (no per-frame allocations)
    mutable std::vector<float> mShown;
};

/// true when this line's text (normalised) occurs at least twice in the lyrics - a hook
bool isHookLine(Lyrics const& l, Line const& line);

} // namespace bf::lyrics
