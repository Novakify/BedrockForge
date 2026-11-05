// LyricsFlow port - the live sky per frame: clock + layers + parameter row -> the shared store RF reads with
// BF_CopySkyFrame (BfLyrics.h). Original glue for Bedrock Forge around the 2.1 ports in Sky / SkyFx.
#pragma once
#include <cmath>
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
    float   wave = -1;                       // [BF] chorus light wave progress 0..1 (< 0 none)
    uint32_t waveColor = 0xFFFFFF;
    int     meteors = 0;                     // [BF] chorus meteor burst starting this frame
    float   dim = 0;                         // [BF] 0..1 darker (song outro, bridge, "night" moments)
    Weather burst = Weather::None;           // [BF] Moments
    float   burstAmt = 0, lightning = 0;
    float   kick = 0;   // [LyricsForge] the drop / chorus kick alone (0..1, eases out): floods the world with the sky's colour
    float   pulse = 0;  // [LyricsForge] music energy + a kick on drops -> the realistic skies (wind, light, colour)
    std::vector<float> const* eq = nullptr;  // [BF] horizon equaliser (Pulse): bar elevations in degrees
    float   eqAlpha = 0;
    uint32_t eqColor = 0xFFFFFF;
};

/// [LyricsForge] the light the sky casts on the world (RF post pass, BfDirectorV1.world*). Neutral = no change.
struct WorldLight {
    float tint[3] = {1, 1, 1};
    float amount = 0, exposure = 1, sat = 1;
    bool  neutral() const { return amount < 0.005f && std::fabs(exposure - 1) < 0.005f && std::fabs(sat - 1) < 0.005f; }
};
/// The target for one sky: its colour (horizon-weighted, hue only) on the world, pushed towards its accent on a drop;
/// exposure follows how bright the sky is and how bright it is right now (breathing, dimming, lightning).
WorldLight worldLightFor(Preset const& p, float visible, float brightness, float energy, float kick, float lightning);

class System {
public:
    /// One frame; publishes to the store. `on` = module on (off publishes an inactive frame).
    void step(bool on, Config const& cfg, AutoPerf const& perf, SystemIn const& in);
    Frame const&             frame() const { return mFrame; }
    /// [BF] how bright the sky behind the lyrics is right now, 0..1 (contrast guard)
    float                    luma() const { return mLuma; }
    WorldLight const&        world() const { return mWorld; }
    std::vector<Quad> const& quads() const { return mQuads; }

private:
    Clock             mClock;
    Fx                mFx;
    Frame             mFrame{};
    std::vector<Quad> mQuads;
    uint32_t          mSeq = 0;
    float             mLuma = 0;
    WorldLight        mWorld;
    float             mDomeYaw = 0;  // [BF] the dome's turn for the current preset
    std::string       mDomeFor;
    Params            mCurQ{}, mPrevQ{}; // [LyricsForge] the sky shown last frame / the one being swept away
    bool              mHaveQ = false;
    float             mSweep = 1;        // 0..1 how far the new sky has swept in (1 = done)
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
