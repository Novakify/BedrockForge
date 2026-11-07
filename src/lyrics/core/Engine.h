// LyricsFlow port - one frame of lyrics: Scene (what/where) -> glyph quads (+ falling letters) -> the shared frame
// RF copies via BF_CopyLyricFrame. Original code for Bedrock Forge. Pure C++ (no game types), so a whole frame can be
// built in unit tests from a fake world, fake camera and parsed lyrics.
#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Perf.h"
#include "lyrics/core/Physics.h"
#include "lyrics/core/LetterFx.h"
#include "lyrics/core/Scene.h"
#include "lyrics/core/SkyWriter.h"
#include "lyrics/core/Atmos.h"
#include "lyrics/core/Ribbon.h"
#include "lyrics/core/PlayerHud.h"
#include "lyrics/core/Portal.h"
#include "lyrics/core/Pulse.h"
#include "lyrics/core/TopBar.h"

namespace bf::lyrics {

/// [LyricsForge] the colour a lyric word names ("Pink!", "flamingos" -> pink), 0xRRGGBB, 0 = not a colour word
uint32_t colorWord(std::string_view text);

/// [LyricsForge] what the Glass Armor reacts to from the lyrics (BfDirectorV1 glass block)
struct GlassCue {
    uint32_t wordSeq   = 0; // bumps on every newly sung word
    uint32_t wordColor = 0; // 0xRRGGBB the newest word names, 0 = not a colour word
    bool     finale    = false; // the constellation outro is showing
};

class Engine {
public:
    Config&  config() { return mCfg; }
    FontSet& fonts() { return mFonts; }

    /// Advance one frame and rebuild the glyph list. world may be null (no placement raycasts / physics collisions).
    void frame(SceneInput const& in, CameraState const& cam, IWorld* world);
    /// Publish the last frame() to the shared store (what BF_CopyLyricFrame returns).
    void publish(bool active, float fovY, std::vector<BfGlyphV1> const* extra = nullptr);

    std::vector<BfGlyphV1> const& glyphs() const { return mGlyphs; }
    Vec3                          origin() const { return mOrigin; }
    Scene&                        scene() { return mScene; }
    LetterPhysics&                physics() { return mPhysics; }
    LetterFx const&               fx() const { return mFx; }
    TopBar&                       topBar() { return mTopBar; }
    AutoPerf&                     perf() { return mPerf; }
    Pulse const&                  pulse() const { return mPulse; } // [BF] the sky draws its equaliser
    void                          reset();
    GlassCue const&               glassCue() const { return mGlass; }
    /// [BF] An atlas slot is being reused for another font: drop falling letters / fragments still drawn from it.
    void forgetAtlas(uint32_t atlas) { mPhysics.dropAtlas(atlas); mFx.dropAtlas(atlas); }

private:
    static constexpr float kTitleCardSec = 4.2f;
    uint32_t               mAccent = 0; // [BF] eased album-art colour
    float                  mAccentF[3] = {-1, -1, -1}; // ... eased in float (integer easing stalled short of the target)
    float                  mShot = 1;   // [BF] shot language: eased framing (distance factor)
    void direct(std::vector<DrawWord>& words, MusicState const& m, Config const& cfg);
    void titleCard(std::vector<DrawWord>& words, SceneInput const& in, Config const& cfg, CameraState const& cam);
    void endCredits(std::vector<DrawWord>& words, SceneInput const& in, Config const& cfg, CameraState const& cam);

    Config                 mCfg;
    FontSet                mFonts;
    Scene                  mScene;
    Atmos                  mAtmos;
    Bursts                 mBursts;
    std::vector<BfGlyphV1> mBack;        // [BF] reused: glyphs drawn behind the words
    Ribbon                 mRibbon;
    PlayerHud              mPlayer;
    Pulse                  mPulse;       // [BF] the beat takes the stage when nobody sings
    Portal                 mPortal;      // [BF] a ring of light opens in the sky on choruses / drops      // [BF] corner music player      // [BF] light running under the line with the singing      // [BF] material-aware impact bits of falling letters       // [BF] music-reactive motes around the player
    SkyWriter              mSkyWriter;   // [BF] the sky writes the song (hook lines -> constellations)
    // [LyricsForge] constellation outro: the song's final line, and what the last playing frame looked like
    Line                   mFinalLine;
    int64_t                mFinalFor = -2, mFinaled = -2, mPrevSeq = -1, mLastT = -1, mLastDur = 0;
    bool                   mWasPlaying = false;
    LetterPhysics          mPhysics;
    LetterFx                      mFx; // [BF] break-apart exits
    TopBar                 mTopBar;
    AutoPerf               mPerf;
    std::vector<BfGlyphV1> mGlyphs;
    Vec3                   mOrigin;
    CameraState            mCam;
    int64_t                mSongSeq = -1;
    GlassCue               mGlass;
    int64_t                mGlassKeys[16] = {}; // recently sung word keys (one sweep per word)
    int                    mGlassKeyAt = 0;
};

/// The shared frame store behind BF_CopyLyricFrame (thread-safe: BF writes on its frame, RF reads at its draw point).
class FrameStore {
public:
    static FrameStore& get();
    void     publish(BfLyricFrameV1 const& hdr, std::vector<BfGlyphV1> const& glyphs);
    void     setAtlases(std::vector<std::string> const& files);
    /// [BF] Screen-space glyphs drawn on top of the lyrics (Bedrock Forge's glass client menu). Empty = none. While
    /// set, the frame is active and its counter moves on every call, even with the lyricsflow module off.
    void     setOverlay(std::vector<BfGlyphV1> glyphs);
    uint32_t copy(BfLyricFrameV1* hdr, BfGlyphV1* dst, uint32_t cap);

private:
    std::mutex             mMutex;
    BfLyricFrameV1         mHdr{};
    std::vector<BfGlyphV1> mGlyphs;
    std::vector<BfGlyphV1> mOverlay;
    uint32_t               mOverlaySeq = 0;
    bool                   mInit = false;
};

} // namespace bf::lyrics
