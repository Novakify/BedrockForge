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
    double  rate  = 1; // [LyricsForge] sky drift rate (Clock::In::rate)
    double  yaw = 0, pitch = 0;              // camera, Minecraft degrees
    double  camX = 0, camY = 0, camZ = 0;    // camera world position
    float   night = 1;                       // nightOf(dayTime); 1 when unknown
    bool    skyOpen = true;                  // sky visible above the player (underground fade)
    bool    overworld = true;                // Core.skyWorld: other dimensions keep their own sky
    float   beat = 0;
    bool    playing = false, lineActive = true;
    int64_t songSeq = 0;                     // [LyricsForge] changes with every new track (chapter transitions)
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
    float   spec[8] = {}; // [LyricsForge] 8 spectrum bands 0..1, bass -> treble (Event Horizon's visualizer ring)
    float   warp = 0;      // [LyricsForge] space skies' warp jump envelope 0..1 (final hit / chapter)
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
    /// [LyricsForge] the colour the world's fog should take (the sky's horizon, as bright as the sky is right now) and
    /// how much it should replace the game's own fog (0 = sky off / underground / another dimension)
    float const*             fog() const { return mFog; }
    float                    fogWeight() const { return mFogW; }
    /// [LyricsForge] Awaken: 0 = the vanilla world (no music), rising to 1 over the ~5 s startup when a song starts
    float                    awaken() const { return mAwakeLevel; }
    bool                     inOutro() const { return mOutroT >= 0; }
    bool                     inChapter() const { return mChapterT >= 0; }
    static constexpr float   kAwakeSec = 5.0f;
    static constexpr float   kOutroSec = 4.0f;    // [LyricsForge] the music stops: the world exhales back to vanilla
    static constexpr float   kChapterSec = 3.5f;  // [LyricsForge] the next song starts with no gap: a new chapter
    std::vector<Quad> const& quads() const { return mQuads; }

private:
    Clock             mClock;
    Fx                mFx;
    Frame             mFrame{};
    std::vector<Quad> mQuads;
    uint32_t          mSeq = 0;
    float             mLuma = 0;
    WorldLight        mWorld;
    float             mFog[3] = {0.67f, 0.82f, 1.0f}, mFogW = 0;
    bool              mAwake = false;     // [LyricsForge] Awaken: music has been playing (cfg.awaken)
    float             mAwakeT = -1;       //   seconds into the startup (-1 = none)
    float             mSilent = 0;        //   seconds without music
    float             mAwakeLevel = 0;    //   0..1 progress the effects follow
    bool              mFromNothing = false, mAwakeMeteors = false; // the startup sweep rises over the vanilla sky
    float             mKickSmooth = 0, mBeatSmooth = 0, mPulseSmooth = 0;
    float             mSpec[8] = {}; // [LyricsForge] eased spectrum (fast up, slow down) for texel 15 // [LyricsForge] eased kick for the dome
    float             mOutroT = -1, mOutroFrom = 1; // [LyricsForge] outro: seconds in (-1 none), the level it started from
    float             mChapterT = -1;              // [LyricsForge] chapter: seconds in (-1 none)
    bool              mOutroMeteors = false, mChapterMeteors = false, mChapterSweep = false;
    int64_t           mSongSeq = 0;
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
