// LyricsForge - [BF] music-reactive atmosphere: glowing motes floating around you that sparkle with the treble,
// swell with the song's energy and flare on drops. Original code for Bedrock Forge. Pure logic: BfGlyphV1 SOLID quads.
#pragma once
#include <cstdint>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

struct MusicState;

class Atmos {
public:
    struct In {
        MusicState const* music = nullptr;
        float             dt = 1.0f / 60.0f;
        Vec3              camPos;
        float             camYaw = 0, camPitch = 0;
        uint32_t          color = 0xFFB9A2FF; // ARGB tint
        bool              enabled = true;
        double            now = 0;
        float             density = 1.0f;     // AutoPerf: 0.25 .. 1
    };
    void step(In const& in);
    void emit(In const& in, Vec3 const& origin, std::vector<BfGlyphV1>& out) const;
    size_t size() const { return mMotes.size(); }
    float  level() const { return mOn; }

    static constexpr int   kCount  = 72;
    static constexpr float kRadius = 13.0f; // blocks

private:
    struct Mote { Vec3 pos, vel; float size = 0.05f, phase = 0, seed = 0; };
    std::vector<Mote> mMotes;
    float             mOn = 0;
    uint32_t          mRng = 0x9E3779B9u;
    float rnd();
    void  respawn(Mote& m, Vec3 const& cam, bool anywhere);
};

} // namespace bf::lyrics
