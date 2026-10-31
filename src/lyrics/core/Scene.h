// LyricsFlow port - which lyric words are on screen, where they sit in the world and how they animate.
// A port of dev.lyricsflow.render.WorldLyrics (LyricsFlow v2.0 by 374m, MIT; ported for Fernando's personal build):
// line/word scheduling, the SKY / FLOATING / WALL / SCREEN placement searches (raycast clearance, cloud and crowd
// avoidance, HUD-target avoidance, wall fitting, keep-out-of-blocks), row layout, enter/exit/beat animation.
// Output is a list of DrawWord (one transform per word); Glyphs.cpp turns those into glyph quads.
//
// Differences from Java, on purpose: no "ghost" second pass (Bedrock draws our text without a depth test anyway),
// and the [BF] combat-safe keep-out zone.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "lyrics/core/Config.h"
#include "lyrics/core/Lyrics.h"
#include "lyrics/core/World.h"

namespace bf::lyrics {

/// Width of `text` in `font`, in units of the font's line height (Gfx.textWidth(s, font, 1.0)).
using TextWidthFn = std::function<float(std::string_view text, std::string_view font)>;

struct Placed {
    Line    line;
    int     w0 = 0, w1 = 0;
    int64_t seed = 0;
    float   yaw = 0, pitch = 0;
    double  dist = 6.0;
    Vec3    a;                 // anchor (world)
    float   turn = 0;
    float   sx = 0, sy = 0;    // SCREEN position (0..1)
    float   tilt = 0;
    std::optional<Quat> surf;  // WALL: surface orientation
    std::optional<Vec3> surfN;
    float   fit = 1.0f;
    float   pairYaw = 0, pairPitch = 0;
    int     rowsN = 1;
    float   coverFade = 1.0f;
    float   angW = 6.0f, angH = 2.0f;
    float   sizeDist = 6.0f;
    std::vector<float> wx0, wy0, wsize, wrot, widths;
    int64_t exitAt = -1;
    float   occl = 1.0f, occlVis = 1.0f, viewVis = 0.0f;
    bool    lookedAway = false;
    int     occlTick = 0;
    std::vector<bool> dropped; // [BF] falling letters already spawned for word j

    int     n() const { return w1 - w0; }
    int64_t lastEnd() const { return line.words[size_t(w1 - 1)].end; }
    bool    sameLine(Line const& l) const { return line.index == l.index && line.start == l.start; }
};

enum class LyricsState { None, Searching, Found, NotFound };

struct SceneInput {
    int64_t            t = -1;       // song position (ms) incl. offsets, -1 = no track
    bool               playing = false;
    Lyrics const*      lyrics = nullptr;
    LyricsState        lyricsState = LyricsState::None;
    int64_t            songSeq = 0;  // increments on every new song
    float              dt = 1.0f / 60.0f;
    double             nowSeconds = 0; // wall clock for bobbing
    float              focusA = 1.0f;  // Hud.focusA (dims while a menu is open)
    float              beat = 0.0f;    // Hud.beat (beatGlow)
    float              combat = 0.0f;  // [BF] 0..1, raised by hits (combat-safe)
    std::string        title, artist;  // now playing (top bar)
    float              guiW = 427.0f, guiH = 240.0f; // UI size in GUI units (Bedrock ScreenView size)
};

/// One word ready to draw. World mode: origin + orient (+X right, +Y up, +Z toward the viewer side the Java mod
/// uses), word centre at local (x, y), rotated by rotZ (radians), uniform scale = world height of the font line.
/// Screen mode: origin.xy in "screen-height units" (x 0..aspect, y 0..1 down), scale in the same units.
struct DrawWord {
    std::string text;
    std::string font;
    bool        screen = false;
    bool        wall   = false;
    Vec3        origin;
    Quat        orient;
    float       x = 0, y = 0, rotZ = 0;
    float       scale = 1.0f;
    float       alpha = 1.0f;
    float       glow  = 0.0f;   // glowBase + sung + beat (Java computes it; we feed the SDF glow with it)
    bool        sung  = false;
    float       layerStep = 0.06f; // z step between neon passes, font units (Java layerStep)
    bool        dropNow = false;    // [BF] falling letters: hand this word's glyphs to the physics this frame
    ExitAnim    dropFx  = ExitAnim::Fall; // [BF] ... or to LetterFx (Shatter / Dissolve / Explode / Burn / Glitch)
    int64_t     key = 0;            // stable id of (line, word) for the physics hand-off
    uint32_t    argb  = 0;          // [BF] fixed colour (0 = theme) - top bar text
    bool        plain = false;      // [BF] main pass only (no shadow / offset passes / glow)
};

class Scene {
public:
    void clear();
    void tick(SceneInput const& in, Config const& cfg, CameraState const& cam, IWorld* world, TextWidthFn const& width);
    /// Words to draw this frame (call after tick).
    std::vector<DrawWord> draw(SceneInput const& in, Config const& cfg, CameraState const& cam, IWorld* world);

    std::vector<Placed> const& visible() const { return mVisible; }
    float                      pauseFade() const { return mPauseFade; }

private:
    void   place(Placed& p, Config const& c, CameraState const& cam, IWorld* w, TextWidthFn const& width);
    void   wallPlace(Placed& p, int64_t s, CameraState const& cam, IWorld* w);
    void   fitToSurface(Placed& p, Config const& c, IWorld* w);
    void   keepOutOfBlocks(Placed& p, Config const& c, CameraState const& cam, IWorld* w);
    bool   wordsInBlocks(Placed const& p, Quat const& q, float em, IWorld* w);
    void   update(Placed& p, Config const& c, CameraState const& cam, IWorld* w, float dt);
    Vec3   anchor(Placed const& p, Config const& c, CameraState const& cam) const;
    Quat   facing(Placed const& p, Config const& c, CameraState const& cam) const;
    float  em(Placed const& p, Config const& c) const;
    double clearance(float yaw, float pitch, double dist, CameraState const& cam, IWorld* w) const;
    int    blocked(float yaw, float pitch, double dist, float halfWidthDeg, CameraState const& cam, IWorld* w) const;
    float  crowd(Vec3 at) const;
    bool   overPlayer(Placed const& p, Config const& c, CameraState const& cam, IWorld* w) const;
    Placed* find(Line const& l, int w0);
    Placed* findIndex(int index);
    Line const& noteLine(int64_t t);

    std::vector<Placed> mVisible;
    std::set<int64_t>   mShown;
    int64_t             mLastT = -1;
    int64_t             mSongSeq = -1;
    float               mPauseFade = 1.0f;
    float               mBaseYaw = 0.0f;
    Line                mNoteLine;
    bool                mHaveNote = false;
    int64_t             mNoteSlot = -1, mNoteSong = -1;
};

/// Per-word enter/exit/beat animation: {alpha, dx, dy, scale, rotation (radians, [BF])}; nullopt = not visible
/// (WorldLyrics.wordAnim).
std::optional<std::array<float, 5>> wordAnim(Placed const& p, int j, Word const& w, int64_t t, Config const& c, float speed);

} // namespace bf::lyrics
