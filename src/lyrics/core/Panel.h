// LyricsFlow port - the world panel (LyricsFlow 2.1 app.cosmetic.WorldPanel, 374m, MIT; ported for Fernando's personal
// build). A 380 x 222 px card (0.0105 blocks per px) that stands 3.2 blocks in front of the player, facing them; you
// aim at it with the crosshair and click. Opening: a record card flies up from in front of you, settles at the panel's
// left, then the panel unfolds sideways out of it and the content fades in. Pages: Sky / Lyrics / Look / Cosmetics /
// More. Pure C++: emits world-space glyph quads (positions relative to the frame origin) and handles the crosshair,
// so it is unit-tested and rendered offline (tools/LyricsPreview) like the rest of the core.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

inline constexpr float kPanelW = 380.0f;  // WorldPanel.W
inline constexpr float kPanelH = 222.0f;  // WorldPanel.H
inline constexpr float kPanelK = 0.0105f; // blocks per px (WorldPanel.K)

/// A rectangle in the UI image atlas (icons, sky thumbnails, logos), uv 0..1, v0 = top.
struct UiRect { float u0 = 0, v0 = 0, u1 = 1, v1 = 1; };

struct PanelAssets {
    int                           uiAtlas = -1;  // atlas index of the UI image atlas (-1 = none)
    std::map<std::string, UiRect> ui;            // "sky.inferno", "logo.spotify", ...
    int                           cover = -1;    // atlas index of the current cover art (-1 = none)
    std::string                   pixelFont = "minecraft"; // panel text (2.1 draws with the Minecraft font)
};

struct PanelMedia {
    std::string title, artist, sourceApp; // sourceApp: "Spotify", "Apple Music", "Browser", ...
    int64_t     positionMs = 0, durationMs = 0;
    bool        playing = false, hasTrack = false;
    int         repeat = 0; // 0 off, 1 one, 2 all (SMTC AutoRepeatMode: None / Track / List)
};

/// What the panel asks the game side to do (BF glue performs these on the media thread / config).
struct PanelAction {
    enum class Kind { PlayPause, Next, Prev, Seek, Repeat, Close, SaveConfig, OpenFullMenu } kind;
    int64_t ms = 0; // Seek target
};

struct PanelInput {
    Vec3    eye;               // camera position (world)
    Vec3    look{0, 0, 1};     // camera forward (unit, world) = the crosshair ray
    Vec3    up{0, 1, 0};       // camera up (unit, world)
    bool    click = false;     // attack button went down this frame
    bool    down  = false;     // attack button held (slider drag, [BF])
    int64_t nowMs = 0;         // wall clock in ms (animations)
    float   dt    = 1.0f / 60; // seconds
    // performance status for the FPS page
    int     perfLevel = 0, fps = 0;
    double  modMs = 0;
};

class Panel {
public:
    enum class Tab { Sky, Lyrics, Look, Fx, Fps, More }; // 2.1's Cosmetics slot is the FPS page; FX = [BF] effects

    /// Open in front of the camera (WorldPanel constructor). eye/look = camera at that moment.
    void open(Vec3 eye, Vec3 look, int64_t nowMs);
    void close(int64_t nowMs);                // starts the closing animation
    bool isOpen() const { return mBorn >= 0 && mClosing < 0; }
    bool visible(int64_t nowMs) const;        // drawn (open, or still animating closed)
    bool hovered() const { return mHovered; } // crosshair on the panel: the game should not get the click

    /// One frame: animate, aim, handle the click, append glyphs (world space, relative to `origin`) and return actions.
    /// Toggles / sliders change `cfg` directly; SaveConfig is emitted when something changed.
    std::vector<PanelAction> frame(PanelInput const& in, Vec3 origin, Config& cfg, FontSet const& fonts, PanelAssets const& assets,
                                   PanelMedia const& media, std::vector<BfGlyphV1>& out);

    /// Panel px -> world (the settled panel, no animation).
    Vec3 toWorld(float x, float y) const;
    /// Crosshair ray -> panel px, or nullopt (WorldPanel.aim: in front, within 14 blocks, inside x 14..380, y 0..222).
    std::optional<std::array<float, 2>> aimAt(Vec3 eye, Vec3 look) const;

    Tab  tab() const { return Tab(mPage); }
    void setTab(Tab t) { mPage = int(t); }
    std::string const& hot() const { return mHot; }

    struct SkyPresetInfo {
        char const* id;           // lower-case enum name, stored in Config::skyPreset
        char const* name;         // en_us lyricsflow.sky.<id>
        uint32_t    top, horizon; // RGB
    };
    /// SkyPreset.order(): the 20 skies of the grid, in LyricsFlow 2.1's order.
    static std::vector<SkyPresetInfo> const& skyOrder();

private:
    struct Ctx;
    void record(Ctx& c, PanelMedia const& media);
    void body(Ctx& c, Config& cfg, PanelMedia const& media);
    void skies(Ctx& c, Config& cfg);
    void lyrics(Ctx& c, Config& cfg);
    void offset(Ctx& c, Config& cfg, float y);
    void look(Ctx& c, Config& cfg, FontSet const& fonts);
    void fx(Ctx& c, Config& cfg);
    void fps(Ctx& c, Config& cfg);
    void more(Ctx& c, Config& cfg);

    Vec3    mPos;              // panel centre (px 190, 111), world
    float   mRx = 1, mRz = 0;  // panel +x (the player's right)
    float   mNx = 0, mNz = -1; // panel normal (toward the player)
    int64_t mBorn = -1, mClosing = -1, mPageTime = 0;
    int     mPage = 0, mSkyPage = 0, mFontPage = 0;
    bool    mHovered = false;
    float   mHx = 0, mHy = 0;
    float   mSpin = 0;
    std::string mHot, mActive;
    std::map<std::string, float> mHov;
};

} // namespace bf::lyrics
