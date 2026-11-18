// LyricsForge - [BF] musical portals: on a chorus or drop a ring of light opens in the sky in front of you, swirls and
// closes again (the vision doc's "eclipse ring as a chorus event"). Original code for Bedrock Forge. SOLID quads.
#pragma once
#include <cstdint>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

struct MusicState;

class Portal {
public:
    struct In {
        MusicState const* music = nullptr;
        float    dt = 1.0f / 60, camYaw = 0, camPitch = 0;
        double   now = 0;
        uint32_t color = 0xFFB9A2FF;
        bool     enabled = true;
    };
    void step(In const& in);
    void emit(In const& in, std::vector<BfGlyphV1>& out) const;
    bool open() const { return mAge >= 0; }

    static constexpr float kLife = 3.6f, kRadius = 140.0f, kGap = 14.0f;

private:
    float mAge = -1, mSince = 99, mYaw = 0, mPitch = 0, mPower = 1;
};

} // namespace bf::lyrics
