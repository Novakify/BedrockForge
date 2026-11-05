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
    float fovScale = 1;   // multiply the camera FOV (fovBreath)
    float showcase = 0;   // 0..1 Showcase blend (camera sway, focus)
    float orbitYaw = 0, orbitPitch = 14, orbitRadius = 4; // camera sway pose, degrees / blocks (MC yaw convention)
    float focus = 0;      // 0..1 soft focus at the screen edges
};

/// [BF] Director cuts (Showcase): camera shots relative to where you look. yaw offset (180 = in front of you,
/// looking back), pitch (+ = from above), distance. 0 = the slow sway behind you.
struct Shot { float yaw, pitch, radius; char const* name; };
inline constexpr Shot kShots[] = {
    {0, 13, 3.6f, "sway"},     {160, -6, 2.4f, "hero low"}, {-160, -5, 2.6f, "hero low R"},
    {35, 28, 5.6f, "wide high"}, {92, 7, 3.1f, "side"},     {-95, 9, 3.3f, "side R"},
    {0, 22, 5.2f, "calm wide"},
};

class Cinema {
public:
    void setPulse(float heartbeat) { mHeart = heartbeat; } // [BF] Moments: lub-dub vignette
    /// [LyricsForge] PvP calm, 0..1 (1 = in a fight right now): the look steps aside while you fight
    void setCombat(float calm) { mCombat = calm; }
    float combat() const { return mCombat; }
    int  shot() const { return mShot; }       // current Showcase shot (kShots index)
    int  cuts() const { return mCuts; }
    /// accent: the song cover colour (0xRRGGBB, 0 = none): each song tints the scene grade a little
    void             step(MusicState const* m, Config const& cfg, float dt, float camYaw = 0, uint32_t accent = 0);
    CinemaOut const& out() const { return mOut; }

    static constexpr float kFlashCap  = 0.16f; // comfort: an impact frame never adds more than this much light
    static constexpr float kDropFlashCap = 0.24f; // [BF] a known drop lands harder (still one soft frame, 20 s apart)
    static constexpr float kFlashGap  = 0.9f;  // s: at most one flash this often (no strobing)
    static constexpr float kLetterMax = 0.075f; // thin: the hotbar is still drawn under it on the HUD

private:
    SceneGrade mCur, mFrom, mTo;
    float      mBlend = 1.0f;     // 0..1 crossfade progress mFrom -> mTo
    bool       mHave = false;
    char       mScene[48] = {};
    float      mAcc[3] = {1, 1, 1};
    float      mHeart = 0;
    float      mCombat = 0;
    int        mShot = 0, mNextShot = 0, mCuts = 0, mCutSeq = 0;
    float      mCutT = 99, mCutWait = -1, mCutPending = -1, mCutJitter = 0, mFlash = 0, mFlashCap = kFlashCap, mHold = 0, mSinceFlash = 99, mKick = 0, mBox = 0, mVig = 0, mOn = 0, mFov = 1, mFovPunch = 0, mShow = 0, mShowAge = 0, mShowYaw = 0, mWaitBeat = 0;
    bool       mPendingFade = false;
    CinemaOut  mOut;
};

} // namespace bf::lyrics
