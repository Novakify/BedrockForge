// LyricsFlow port - song clock. Logic ported from LyricsFlow v2.0 MediaManager/Track (374m, MIT); see Media.h.
#include "lyrics/core/Media.h"

#include <algorithm>
#include <cstdlib>

#include "lyrics/core/Text.h"

namespace bf::lyrics {

int64_t Track::positionNow(int64_t nowNs) const {
    int64_t p = s.positionMs;
    if (s.playing) p += (nowNs - sampledAtNs) / 1000000;
    if (s.durationMs > 0) p = std::min(p, s.durationMs);
    return p;
}
std::string Track::key() const { return text::toLower(s.artist + std::string(1, '\0') + s.title); }

std::vector<MediaEvent> SongClock::poll(MediaSample const* in, int64_t nowNs) {
    std::vector<MediaEvent> ev;
    if (!in || text::trim(in->title).empty()) {
        if (mHave) {
            if (mMissingSinceNs < 0) mMissingSinceNs = nowNs;
            if (nowNs - mMissingSinceNs > 2500LL * 1000000) {
                ev.push_back(MediaEvent::Stop);
                mHave    = false;
                mLastKey.clear();
            }
        }
        return ev;
    }
    Track t;
    t.s             = *in;
    t.s.title       = text::trimmed(t.s.title);
    t.s.artist      = text::trimmed(t.s.artist);
    t.s.album       = text::trimmed(t.s.album);
    t.s.durationMs  = std::max<int64_t>(0, t.s.durationMs);
    t.s.positionMs  = std::max<int64_t>(0, t.s.positionMs);
    t.sampledAtNs   = nowNs;
    mMissingSinceNs = -1;
    std::string key = t.key();
    if (!mHave || key != mLastKey) {
        mLastKey         = key;
        mLastPlaying     = t.s.playing;
        mPausedUpdatedFt = t.s.playing ? -2 : t.s.updatedFt;
        mStable          = 0;
        mLastOffset      = INT64_MIN;
        mTrack           = t;
        mHave            = true;
        ++mSongSeq;
        ev.push_back(MediaEvent::Song);
        mLastPollNs = nowNs;
        return ev;
    }
    Track const prev  = mTrack;
    bool const  fresh = t.s.updatedFt > 0 && t.s.updatedFt != mPausedUpdatedFt;
    if (t.s.playing) {
        if (prev.s.playing) {
            int64_t ours    = prev.positionNow(nowNs);
            int64_t offset  = t.s.positionMs - ours;
            int64_t sinceMs = mLastPollNs > 0 ? (nowNs - mLastPollNs) / 1000000 : 250;
            if (mLastOffset != INT64_MIN && std::llabs(offset - mLastOffset) <= 60 + sinceMs / 5) ++mStable;
            else mStable = 0;
            mLastOffset  = offset;
            int64_t diff = std::llabs(offset);
            bool    seek = prev.s.rawPositionMs >= 0 && t.s.rawPositionMs >= 0 && std::llabs(t.s.rawPositionMs - prev.s.rawPositionMs) > 400 &&
                        t.s.updatedFt != prev.s.updatedFt;
            bool trusted = (seek && mStable >= 1) || (fresh && mStable >= 2);
            if (trusted && diff > (mForceSync ? 120 : 250)) {
                mLastOffset = 0; // re-sync: take Windows' position as of now
            } else { // keep our own clock (Track.keepClock)
                t.s.positionMs = prev.s.positionMs;
                t.sampledAtNs  = prev.sampledAtNs;
            }
        } else {
            mStable     = 0;
            mLastOffset = INT64_MIN;
            if (!fresh && t.s.rawPositionMs >= 0) { // resumed: continue from where we paused unless Windows moved far
                int64_t from   = std::llabs(t.s.rawPositionMs - prev.s.positionMs) < 3000 ? prev.s.positionMs : t.s.rawPositionMs;
                t.s.positionMs = from;
                t.sampledAtNs  = nowNs;
            }
        }
    }
    mLastPollNs = nowNs;
    mForceSync  = false;
    if (!t.s.playing) mPausedUpdatedFt = t.s.updatedFt;
    mTrack = t;
    if (t.s.playing != mLastPlaying) ev.push_back(t.s.playing ? MediaEvent::Resume : MediaEvent::Pause);
    mLastPlaying = t.s.playing;
    return ev;
}

int64_t SongClock::positionRaw(int64_t nowNs) const { return mHave ? mTrack.positionNow(nowNs) : -1; }
int64_t SongClock::position(int64_t nowNs, Config const& cfg) const {
    if (!mHave) return -1;
    auto    it  = cfg.songOffsets.find(mTrack.key());
    int64_t own = it == cfg.songOffsets.end() ? 0 : it->second;
    return std::max<int64_t>(0, mTrack.positionNow(nowNs) + cfg.syncOffsetMs + own);
}

// [BF] Apple Music for Windows (Microsoft Store app "AppleInc.AppleMusicWin_...!App"; iTunes too).
bool isAppleMusicApp(std::string const& appId) {
    std::string a = text::toLower(appId);
    return text::contains(a, "applemusic") || text::contains(a, "itunes");
}

// [BF] Apple Music on Windows reports odd SMTC metadata: the artist can be empty (album artist holds it) or come as
// "Artist \u2014 Album" in one field. Titles are real song titles, so no YouTube-style "Artist - Song" split.
void cleanAppleMusic(MediaSample& m) {
    static constexpr std::string_view kSep = " \xE2\x80\x94 "; // " em-dash "
    if (size_t i = m.artist.find(kSep); i != std::string::npos && i > 0) {
        std::string album = text::trimmed(std::string_view(m.artist).substr(i + kSep.size()));
        m.artist          = text::trimmed(std::string_view(m.artist).substr(0, i));
        if (m.album.empty()) m.album = album;
    }
}

bool sourceMatches(Source src, std::string const& appId) {
    std::string a = text::toLower(appId);
    if (src == Source::Any) return true;
    if (src == Source::Spotify) return text::contains(a, "spotify");
    if (src == Source::AppleMusic) return isAppleMusicApp(appId);
    static char const* browsers[] = {"chrome", "msedge", "edge", "firefox", "308046b0af4a39cb", "opera", "brave", "vivaldi", "yandex", "arc", "zen"};
    for (auto b : browsers)
        if (text::contains(a, b)) return true;
    return text::contains(a, "youtube");
}

} // namespace bf::lyrics
