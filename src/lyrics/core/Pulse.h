// LyricsForge - [BF] the beat takes the stage: when nobody is singing (instrumentals, ambient songs, long beat
// breaks, songs with no lyrics) the world itself performs the music. Original code for Bedrock Forge.
//
//  - every kick sends a ring of light rolling out across the ground from your feet (the world is the speaker)
//  - an equaliser of light stands all around the horizon: low, slow swells for the bass, fast ripples for the
//    treble, flaring on hits
// Driven by MusicState::instrumental (0..1, eased) so it fades in when the voice leaves and out when it comes
// back. SOLID BfGlyphV1 quads, positions relative to the eye (= Engine origin).
#pragma once
#include <cstdint>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

struct MusicState;

class Pulse {
public:
    struct In {
        MusicState const* music = nullptr;
        float    dt = 1.0f / 60, camYaw = 0;
        double   now = 0;
        uint32_t color = 0xFFB9A2FF; // ARGB
        bool     enabled = true;
        float    density = 1.0f;     // AutoPerf: 0.25 .. 1
        bool     bars = true;        // the horizon equaliser (off for pastel / storm skies)
    };
    void step(In const& in);
    void emit(In const& in, std::vector<BfGlyphV1>& out) const;
    int  rings() const { return int(mRings.size()); }
    float level() const { return mOn; }

    static constexpr float kRingLife = 1.25f, kRingMax = 15.0f, kHorizon = 70.0f, kEye = 1.58f;

private:
    struct Ring { float age = 0, power = 1; };
    std::vector<Ring> mRings;
    float mOn = 0, mPrevBeat = 0, mSince = 9, mFlare = 0;
};

} // namespace bf::lyrics
