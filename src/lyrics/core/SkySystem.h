// LyricsFlow port - the live sky per frame: clock + layers + parameter row -> the shared store RF reads with
// BF_CopySkyFrame (BfLyrics.h). Original glue for Bedrock Forge around the 2.1 ports in Sky / SkyFx.
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/SkyFx.h"

namespace bf::lyrics::sky {

struct SystemIn {
    int64_t nowMs = 0;
    double  dtSec = 0;
    double  yaw = 0, pitch = 0;              // camera, Minecraft degrees
    double  camX = 0, camY = 0, camZ = 0;    // camera world position
    float   night = 1;                       // nightOf(dayTime); 1 when unknown
    bool    skyOpen = true;                  // sky visible above the player (underground fade)
    bool    overworld = true;                // Core.skyWorld: other dimensions keep their own sky
    float   beat = 0;
    bool    playing = false, lineActive = true;
    std::string pictureFile;                 // full path of the chosen picture, "" = none / not loaded
    std::string spritesFile;                 // resources/lyricsflow/sky/sprites.png
};

class System {
public:
    /// One frame; publishes to the store. `on` = module on (off publishes an inactive frame).
    void step(bool on, Config const& cfg, AutoPerf const& perf, SystemIn const& in);
    Frame const&             frame() const { return mFrame; }
    std::vector<Quad> const& quads() const { return mQuads; }

private:
    Clock             mClock;
    Fx                mFx;
    Frame             mFrame{};
    std::vector<Quad> mQuads;
    uint32_t          mSeq = 0;
};

/// Pack quads into the shared vertex layout (4 vertices per quad).
void toVerts(std::vector<Quad> const& quads, std::vector<BfSkyVertV1>& out);

class SkyStore {
public:
    static SkyStore& get();
    void     publish(BfSkyFrameV1 const& hdr, std::vector<BfSkyVertV1>&& verts);
    uint32_t copy(BfSkyFrameV1* hdr, BfSkyVertV1* dst, uint32_t cap);

private:
    std::mutex               mMutex;
    BfSkyFrameV1             mHdr{};
    std::vector<BfSkyVertV1> mVerts;
};

} // namespace bf::lyrics::sky
