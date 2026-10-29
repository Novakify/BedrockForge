// LyricsFlow port - now-playing state and the song clock. Ported from dev.lyricsflow.media.{Track, MediaManager}
// (LyricsFlow v2.0 by 374m, MIT) for Fernando's personal build. Windows reports the media position in coarse,
// sometimes stale steps; LyricsFlow keeps its own clock (position + elapsed since the sample) and only re-syncs to
// Windows when the difference is real (a seek, or a stable offset seen on 2+ polls). Pure logic: the SMTC poller
// (lyrics/win/Smtc_win.cpp) feeds MediaSample values in, tests feed recorded ones.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lyrics/core/Config.h"

namespace bf::lyrics {

/// One poll of Windows' media session (GlobalSystemMediaTransportControlsSession).
struct MediaSample {
    std::string title, artist, album, sourceApp;
    int64_t     durationMs    = 0;
    int64_t     positionMs    = 0;  // position as Windows reports it, extrapolated to now by the backend
    bool        playing       = false;
    int64_t     rawPositionMs = -1; // timeline Position as-is
    int64_t     updatedFt     = -1; // timeline LastUpdatedTime (FILETIME ticks)
};

struct Track {
    MediaSample s;
    int64_t     sampledAtNs = 0;
    int64_t     positionNow(int64_t nowNs) const;
    std::string key() const; // lower(artist + "\0" + title), the songOffsets key
    bool        empty() const { return s.title.empty(); }
};

enum class MediaEvent { Song, Pause, Resume, Stop };

class SongClock {
public:
    /// Feed one poll (or "nothing playing" when sample is null). Returns the events it caused.
    std::vector<MediaEvent> poll(MediaSample const* sample, int64_t nowNs);
    /// Song position for the lyrics (incl. the global sync offset and the song's own offset), -1 = nothing playing.
    int64_t position(int64_t nowNs, Config const& cfg) const;
    int64_t positionRaw(int64_t nowNs) const;
    bool    hasTrack() const { return mHave; }
    bool    playing() const { return mHave && mTrack.s.playing; }
    Track const& track() const { return mTrack; }
    int64_t songSeq() const { return mSongSeq; }
    void    forceSync() { mForceSync = true; }

private:
    Track   mTrack;
    bool    mHave = false;
    std::string mLastKey;
    bool    mLastPlaying = false;
    int64_t mPausedUpdatedFt = -2;
    int     mStable = 0;
    int64_t mLastOffset = INT64_MIN;
    int64_t mLastPollNs = 0;
    int64_t mMissingSinceNs = -1;
    int64_t mSongSeq = 0;
    bool    mForceSync = false;
};

/// Source filter (LyricsFlow SmtcBackend): does this session's app id belong to the chosen source?
bool sourceMatches(Source src, std::string const& appId);

} // namespace bf::lyrics
