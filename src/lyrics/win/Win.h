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

#include "lyrics/core/Fetcher.h"
#include "lyrics/core/Media.h"

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

} // namespace bf::lyrics::win
#endif
