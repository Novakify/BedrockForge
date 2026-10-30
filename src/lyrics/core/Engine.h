// LyricsFlow port - one frame of lyrics: Scene (what/where) -> glyph quads (+ falling letters) -> the shared frame
// RF copies via BF_CopyLyricFrame. Original code for Bedrock Forge. Pure C++ (no game types), so a whole frame can be
// built in unit tests from a fake world, fake camera and parsed lyrics.
#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Physics.h"
#include "lyrics/core/Scene.h"
#include "lyrics/core/TopBar.h"

namespace bf::lyrics {

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
    TopBar&                       topBar() { return mTopBar; }
    void                          reset();

private:
    Config                 mCfg;
    FontSet                mFonts;
    Scene                  mScene;
    LetterPhysics          mPhysics;
    TopBar                 mTopBar;
    std::vector<BfGlyphV1> mGlyphs;
    Vec3                   mOrigin;
    CameraState            mCam;
    int64_t                mSongSeq = -1;
};

/// The shared frame store behind BF_CopyLyricFrame (thread-safe: BF writes on its frame, RF reads at its draw point).
class FrameStore {
public:
    static FrameStore& get();
    void     publish(BfLyricFrameV1 const& hdr, std::vector<BfGlyphV1> const& glyphs);
    void     setAtlases(std::vector<std::string> const& files);
    uint32_t copy(BfLyricFrameV1* hdr, BfGlyphV1* dst, uint32_t cap);

private:
    std::mutex             mMutex;
    BfLyricFrameV1         mHdr{};
    std::vector<BfGlyphV1> mGlyphs;
    bool                   mInit = false;
};

} // namespace bf::lyrics
