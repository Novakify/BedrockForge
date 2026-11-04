// LyricsForge - [BF] Cinema: the Director's full-screen look (colour grade, vignette, impact frames, letterbox).
// Original code for Bedrock Forge. Pure logic; the runtime copies the result into BfDirectorV1 for RF.
//
// Each scene has a grade (tint, saturation, contrast, vignette, flash colour). Switching scenes crossfades the grade
// over ~1.6 s. The Conductor drives the motion on top: drops and chorus hits flash (soft, capped, never strobing),
// kick bloom and chromatic aberration for a beat; builds pull the letterbox in and deepen the vignette.
#pragma once
#include <cstdint>
#include <string_view>

namespace bf::lyrics {

struct MusicState;
struct Config;

struct SceneGrade {
    float tint[3] = {1, 1, 1};
    float amount = 0.0f, saturation = 1.0f, contrast = 1.0f, vignette = 0.12f;
    float flash[3] = {1, 1, 1};
};

/// grade of a built-in scene ("" / unknown = neutral)
SceneGrade gradeForScene(std::string_view sceneId);

struct CinemaOut {
    bool  active = false;
    float tint[3] = {1, 1, 1};
    float amount = 0, saturation = 1, contrast = 1, vignette = 0, flash = 0;
    float flashColor[3] = {1, 1, 1};
    float letterbox = 0, bloomBoost = 0, caBoost = 0;
};

class Cinema {
public:
    void             step(MusicState const* m, Config const& cfg, float dt);
    CinemaOut const& out() const { return mOut; }

    static constexpr float kFlashCap  = 0.16f; // comfort: an impact frame never adds more than this much light
    static constexpr float kFlashGap  = 0.9f;  // s: at most one flash this often (no strobing)
    static constexpr float kLetterMax = 0.11f;

private:
    SceneGrade mCur, mFrom, mTo;
    float      mBlend = 1.0f;     // 0..1 crossfade progress mFrom -> mTo
    bool       mHave = false;
    char       mScene[48] = {};
    float      mFlash = 0, mSinceFlash = 99, mKick = 0, mBox = 0, mVig = 0, mOn = 0;
    CinemaOut  mOut;
};

} // namespace bf::lyrics
