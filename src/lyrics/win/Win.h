// LyricsFlow port - Windows/Bedrock side. Original code for Bedrock Forge.
//   Http_win.cpp     WinHTTP transport for the lyric search
//   Smtc_win.cpp     Windows media session (SMTC) via C++/WinRT: title/artist/position/state + play/pause/skip/seek
//   Runtime_win.cpp  the BF module: media thread, lyric fetch thread, per-frame Engine update, keybinds, config
//   World_win.cpp    IWorld over LeviLamina's BlockSource / players (placement raycasts, letter collisions)
#pragma once
#ifdef _WIN32
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <map>

#include "lyrics/core/Fetcher.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Media.h"
#include "lyrics/core/Panel.h"

namespace bf::lyrics::win {

fetch::HttpResponse httpGet(std::string const& url, fetch::Headers const& headers, int64_t deadlineMs);

/// GlobalSystemMediaTransportControls. All calls block; use from a worker thread (it joins the MTA itself).
class Smtc {
public:
    Smtc();
    ~Smtc();
    /// Current session for the source filter (playing sessions first, like LyricsFlow); nullopt = nothing playing.
    std::optional<MediaSample> poll(Source src);
    void        playPause();
    void        next();
    void        previous();
    void        seek(int64_t ms);
    void        cycleRepeat(); // off -> list -> track -> off
    /// Cover art of the current session (PNG / JPEG bytes as the app provides them), empty if none.
    std::vector<uint8_t> thumbnail();
    std::string status() const; // "playing" / "paused" / "nothing" / "open_spotify" / "unsupported"
    bool        available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

/// Called from Core::enable(): registers the "lyricsflow" module (off by default), keybinds and threads.
/// modDir = mods/BedrockForgeCore (fonts under resources/lyricsflow/fonts), configDir = its config folder.
void install(std::filesystem::path const& modDir, std::filesystem::path const& configDir);
void uninstall();
/// Local player hit something or got hurt (Combat Safe dims lyrics for combatHoldMs). Any thread.
void noteCombat();

/// [BF] What Bedrock Forge's glass client menu draws with: the Outfit MSDF fonts (always kept in an atlas slot) and
/// the white icon atlas (resources/client). Fonts are null until their atlas is slotted. Any thread.
struct ClientUiAssets {
    Font const*                   regular = nullptr;
    Font const*                   bold    = nullptr;
    int                           icons   = -1; // atlas index, -1 = missing
    std::map<std::string, UiRect> iconRects;
};
ClientUiAssets clientUiAssets();
/// Open the LyricsFlow world panel on the next HUD frame (the menu closes first, the panel needs the HUD view).
void requestPanel();
/// LyricsFlow's own on/off (the O key): lyrics drawn or not, independent of the module.
bool lyricsShown();
void setLyricsShown(bool on);

} // namespace bf::lyrics::win
#endif
