// LyricsFlow port - the in-world settings panel: a tilted 3D card pair in front of the camera (player card with cover
// art + controls on the left, settings on the right, tab pills at the bottom). Look follows the LyricsFlow teaser
// panel (374m), content is LyricsFlow v2.0's real options (Pages.java) plus the port's [BF] extras.
// Original code for Bedrock Forge. Pure C++: builds BF_GLYPH_VIEW glyph quads and handles the mouse, so the whole
// panel can be unit-tested and rendered offline (tools/LyricsPreview).
//
// Space: the panel is laid out in "design px" (kPanelW x kPanelH, y down). It sits on a plane in BF's virtual view
// space (x right, y up, z forward); glyphs are emitted with x, y divided by kViewTan so RF's vertex shader only needs
// (x / aspect, y, 0, z). Mouse picking inverts exactly that mapping.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {

inline constexpr float kPanelW  = 640.0f;
inline constexpr float kPanelH  = 380.0f;
inline constexpr float kViewTan = 0.70f; // tan(fovY / 2) of the virtual camera the panel is drawn with

/// A rectangle in the UI image atlas (icons, sky thumbnails, preset thumbnails), uv 0..1, v0 = top.
struct UiRect { float u0 = 0, v0 = 0, u1 = 1, v1 = 1; };

struct PanelAssets {
    int                           uiAtlas = -1;  // atlas index of the UI image atlas (-1 = none: icons drawn as shapes)
    std::map<std::string, UiRect> ui;            // "icon.play", "sky.inferno", "preset.ground_drop", ...
    int                           cover = -1;    // atlas index of the current cover art (-1 = none)
    std::string                   pixelFont = "minecraft"; // UI text font (the teaser uses the Minecraft font)
};

struct PresetInfo {
    std::string id, name;
};

struct PanelMedia {
    std::string title, artist, sourceApp; // sourceApp: "Spotify", "Apple Music", "Browser", ...
    int64_t     positionMs = 0, durationMs = 0;
    bool        playing = false, hasTrack = false;
    std::string lyricsState; // "synced", "searching", "not found", ""
};

/// What the panel asks the game side to do (BF glue performs these on the media thread / config).
struct PanelAction {
    enum class Kind { PlayPause, Next, Prev, Seek, Refetch, ApplyPreset, Close, SaveConfig } kind;
    int64_t     ms = 0;   // Seek target
    std::string id;       // ApplyPreset
};

struct PanelInput {
    bool  mouseValid = false;
    float mouseNdcX = 0, mouseNdcY = 0; // -1..1, y up (screen centre = 0,0)
    bool  down = false;                 // left button held
    float aspect = 16.0f / 9.0f;        // back buffer width / height
    float dt = 1.0f / 60.0f;
    double now = 0;                     // seconds, for small animations
};

class Panel {
public:
    enum class Tab { Sky, Lyrics, Look, Presets, Other };

    bool open() const { return mOpen; }
    void setOpen(bool on);
    bool visible() const { return mOpen || mOpenT > 0.001f; }

    /// One frame: animate, handle the mouse, append glyphs (BF_GLYPH_VIEW) and return actions. Changes to `cfg` are
    /// made directly (sliders, toggles, chips); a SaveConfig action is emitted when something changed.
    std::vector<PanelAction> frame(PanelInput const& in, Config& cfg, FontSet const& fonts, PanelAssets const& assets, PanelMedia const& media,
                                   std::vector<PresetInfo> const& presets, std::vector<BfGlyphV1>& out);

    /// Map a mouse position (NDC) onto the panel: design px, or nullopt when the ray misses the panel plane.
    std::optional<std::array<float, 2>> pick(float ndcX, float ndcY, float aspect) const;
    /// Design px -> virtual view space (already divided by kViewTan; what BF_GLYPH_VIEW glyphs carry).
    Vec3 toView(float x, float y) const;

    Tab  tab() const { return mTab; }
    void setTab(Tab t) { mTab = t; }
    int  page() const { return mPage; }
    /// Hover target id under the mouse last frame (tests / debugging).
    std::string const& hovered() const { return mHot; }

    static std::vector<std::string> const& skyPresetIds();
    static std::string                     skyPresetName(std::string const& id);

private:
    struct Ctx;
    void drawPlayer(Ctx& c, PanelMedia const& media);
    void drawHeader(Ctx& c, PanelMedia const& media, Config& cfg);
    void drawTabs(Ctx& c);
    void pageSky(Ctx& c, Config& cfg);
    void pageLyrics(Ctx& c, Config& cfg);
    void pageLook(Ctx& c, Config& cfg, FontSet const& fonts);
    void pagePresets(Ctx& c, Config& cfg, std::vector<PresetInfo> const& presets);
    void pageOther(Ctx& c, Config& cfg);

    bool        mOpen = false;
    float       mOpenT = 0;     // 0..1 open animation
    Tab         mTab = Tab::Sky;
    int         mPage = 0;      // grid page on the current tab
    std::string mHot;           // hovered widget id
    std::string mActive;        // widget being dragged (slider / seek bar)
    bool        mWasDown = false;
    std::map<std::string, float> mHover; // smoothed hover 0..1 per widget id
    int64_t     mSeekPreview = -1;
    float       mSize = 1.0f;   // Compact 0.85 / Normal 1 / Large 1.18 (the "Normal" chip)
};

} // namespace bf::lyrics
