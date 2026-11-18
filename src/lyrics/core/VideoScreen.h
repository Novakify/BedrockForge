// [LyricsForge] Video screen - where the screen stands and what it is made of. Original code for Bedrock Forge.
// Pure C++ (no game / Windows types): turns the settings + camera into glyph quads. The picture itself is a BF_GLYPH_LIVE
// image quad that Renderer Foundation fills with the captured window (16:9, 1920x1080, letterboxed, Anime4K optional).
//
// Places (Config::screenPlace):
//   0 Sky    - hangs in the sky like the cover card / sky lyrics: always ~90 blocks away in one direction, moves with you
//   1 Pinned - stands at a fixed spot in the world (Put it here), like a cinema screen
//   2 Beside - a small screen floating beside you (right of the crosshair), lagging softly behind your turns
#pragma once
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

struct VideoScreenIn {
    Vec3   eye;              // camera position (world)
    float  yaw = 0, pitch = 0; // camera (Minecraft degrees)
    Vec3   origin;           // frame origin the glyph positions are relative to
    float  dt = 1.0f / 60;   // seconds
    float  fight = 0;        // 0 calm .. 1 in a fight (eased)
    bool   live = false;     // RF reports a picture
};

class VideoScreen {
public:
    /// Append the screen's quads (aura, neon rim, bezel, picture). Nothing when the screen is off.
    void build(VideoScreenIn const& in, Config const& cfg, std::vector<BfGlyphV1>& out);
    /// Put it where you look now (sky: your direction; pinned: ~8 blocks ahead; beside: no-op). Changes cfg.
    static void placeHere(Config& cfg, Vec3 eye, float yaw, float pitch);
    /// Centre + size of the last build (world), for tests / the panel.
    Vec3  centre() const { return mCentre; }
    float width() const { return mWidth; }
    float alpha() const { return mAlpha; }

private:
    float mSideYaw = 0, mSidePitch = 0; // Beside: eased direction
    bool  mSideInit = false;
    float mShow = 0;                    // fade in when switched on
    Vec3  mCentre;
    float mWidth = 0, mAlpha = 0;
};

} // namespace bf::lyrics
