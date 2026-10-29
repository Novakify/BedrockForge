// LyricsFlow port - Windows media session via C++/WinRT. Behaviour follows LyricsFlow v2.0's SmtcBackend (374m, MIT:
// session choice, source filter, timeline extrapolation, YouTube title split); the code is original (LyricsFlow calls
// the same WinRT interfaces through JNA vtables, C++/WinRT does it natively).
// No LeviLamina headers in this file on purpose (keeps the WinRT projection away from the game headers).
#ifdef _WIN32
#include "lyrics/win/Win.h"

#include <algorithm>
#include <mutex>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include "lyrics/core/Text.h"

#pragma comment(lib, "windowsapp.lib")

namespace bf::lyrics::win {

using namespace winrt::Windows::Media::Control;
using Manager = GlobalSystemMediaTransportControlsSessionManager;
using Session = GlobalSystemMediaTransportControlsSession;

struct Smtc::Impl {
    std::mutex  mutex;
    Manager     mgr{nullptr};
    Session     current{nullptr};
    bool        failed = false;
    std::string status = "nothing";

    bool ensure() {
        if (mgr) return true;
        if (failed) return false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) { /* already initialised on this thread */ }
        try {
            mgr = Manager::RequestAsync().get();
        } catch (...) {
            failed = true;
            status = "unsupported";
        }
        return bool(mgr);
    }
};

Smtc::Smtc() : mImpl(std::make_unique<Impl>()) {}
Smtc::~Smtc() = default;
bool        Smtc::available() const { return !mImpl->failed; }
std::string Smtc::status() const { return mImpl->status; }

std::optional<MediaSample> Smtc::poll(Source src) {
    std::lock_guard l(mImpl->mutex);
    if (!mImpl->ensure()) return std::nullopt;
    try {
        Session chosen{nullptr}, firstMatch{nullptr};
        for (auto const& s : mImpl->mgr.GetSessions()) {
            std::string app = winrt::to_string(s.SourceAppUserModelId());
            if (!sourceMatches(src, app)) continue;
            if (!firstMatch) firstMatch = s;
            if (s.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) { chosen = s; break; }
        }
        if (!chosen) chosen = firstMatch;
        mImpl->current = chosen;
        if (!chosen) {
            mImpl->status = src == Source::Spotify ? "open_spotify" : "nothing";
            return std::nullopt;
        }
        auto        props = chosen.TryGetMediaPropertiesAsync().get();
        MediaSample m;
        m.title  = winrt::to_string(props.Title());
        m.artist = winrt::to_string(props.Artist());
        if (m.artist.empty()) m.artist = winrt::to_string(props.AlbumArtist());
        m.album     = winrt::to_string(props.AlbumTitle());
        std::string app = winrt::to_string(chosen.SourceAppUserModelId());
        bool spotify    = text::contains(text::toLower(app), "spotify");
        m.sourceApp     = spotify ? "Spotify" : (sourceMatches(Source::Youtube, app) ? "Browser" : app);
        if (!spotify) { // browser tabs: "Artist - Song (Official Video) - YouTube" + channel name
            auto split = text::splitYoutube(m.title, m.artist);
            m.artist   = split[0];
            m.title    = split[1];
        }
        m.playing = chosen.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        auto    tl    = chosen.GetTimelineProperties();
        int64_t start = tl.StartTime().count(), end = tl.EndTime().count(), pos = tl.Position().count(); // 100 ns ticks
        if (end > 0) m.durationMs = (end - std::max<int64_t>(0, start)) / 10000;
        m.positionMs    = pos / 10000;
        m.rawPositionMs = m.positionMs;
        m.updatedFt     = tl.LastUpdatedTime().time_since_epoch().count(); // FILETIME ticks (1601 epoch)
        if (m.playing && m.updatedFt > 0) {
            int64_t nowFt = winrt::clock::now().time_since_epoch().count();
            int64_t delta = (nowFt - m.updatedFt) / 10000;
            if (delta > 0 && delta < 6LL * 3600 * 1000) m.positionMs += delta;
        }
        mImpl->status = m.title.empty() ? "nothing" : (m.playing ? "playing" : "paused");
        if (m.title.empty()) return std::nullopt;
        return m;
    } catch (...) {
        mImpl->current = nullptr;
        return std::nullopt;
    }
}

void Smtc::playPause() {
    std::lock_guard l(mImpl->mutex);
    try { if (mImpl->current) mImpl->current.TryTogglePlayPauseAsync().get(); } catch (...) {}
}
void Smtc::next() {
    std::lock_guard l(mImpl->mutex);
    try { if (mImpl->current) mImpl->current.TrySkipNextAsync().get(); } catch (...) {}
}
void Smtc::previous() {
    std::lock_guard l(mImpl->mutex);
    try { if (mImpl->current) mImpl->current.TrySkipPreviousAsync().get(); } catch (...) {}
}
void Smtc::seek(int64_t ms) {
    std::lock_guard l(mImpl->mutex);
    try { if (mImpl->current) mImpl->current.TryChangePlaybackPositionAsync(ms * 10000).get(); } catch (...) {}
}

} // namespace bf::lyrics::win
#endif
