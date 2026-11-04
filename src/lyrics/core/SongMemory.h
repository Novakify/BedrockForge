// LyricsForge - [BF] Song Memory (original code for Bedrock Forge).
//
// The first time a song plays, its energy (bass / mid / treble / loudness from BandAnalyzer, 20 frames a second) is
// written next to the song position. Later plays are averaged in, so a one-off TNT blast or chat ping washes out, and
// parts that were skipped fill in next time. From the saved map we find the song's shape - calm / mid / high
// sections, builds, drops and (from repeated lyric lines) the hooks - so the Director can look AHEAD instead of
// reacting late. Stored per song as a small binary file (~25 KB for 4 minutes); never any audio.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "lyrics/core/Bands.h"

namespace bf::lyrics {

struct Lyrics;

class SongMemory {
public:
    static constexpr int kFrameMs = 50;

    struct Section {
        enum class Kind : uint8_t { Calm, Mid, High };
        int64_t start = 0, end = 0;
        Kind    kind  = Kind::Mid;
        float   level = 0; // 0..1 mean energy, normalised to the song
    };
    struct Event {
        enum class Kind : uint8_t { Drop, Chorus };
        int64_t at        = 0;  // ms
        int64_t buildFrom = -1; // ms where the energy started climbing towards it, -1 = no build
        Kind    kind      = Kind::Drop;
        float   strength  = 0;  // 0..1
    };

    /// Storage key: lower(artist + "\0" + title) + duration in whole seconds (a remix / sped-up version is its own song)
    static std::string keyFor(std::string const& trackKey, int64_t durationMs);
    static std::string fileNameFor(std::string const& key); // "<fnv64 hex>.lfm"

    void reset(std::string key, int64_t durationMs);
    bool load(std::filesystem::path const& file); // false = missing / damaged (the map stays empty)
    bool save(std::filesystem::path const& file) const;
    /// take over another memory's saved map (same key) - the play recorded so far is kept
    void adoptMap(SongMemory&& saved);

    /// one frame of the current play at song position posMs
    void record(int64_t posMs, BandFrame const& f);
    /// the play ended (song change / stop): merge it into the averaged map. Returns true when it counted
    /// (at least 15 s heard); plays() goes up when at least half the song was heard.
    bool endPlay();

    /// sections / events / hooks from the map (+ repeated lyric lines when lyrics are given)
    void analyze(Lyrics const* lyrics);

    std::string const& key() const { return mKey; }
    int64_t            durationMs() const { return mDurationMs; }
    int                plays() const { return mPlays; }
    float              coverage() const;     // share of the song the map knows, 0..1
    bool               ready() const { return coverage() > 0.5f; }
    int64_t            heardThisPlayMs() const { return int64_t(mHeardFrames) * kFrameMs; }
    size_t             frameCount() const { return mFrames.size(); }

    /// smoothed, song-normalised energy at posMs (0..1), -1 = unknown
    float energyAt(int64_t posMs) const;
    /// the next drop / chorus within horizonMs after posMs, or nullptr
    Event const* nextEvent(int64_t posMs, int64_t horizonMs) const;

    std::vector<Section>                       sections;
    std::vector<Event>                         events;
    std::vector<std::pair<int64_t, int64_t>>   hooks; // time ranges of lyric lines that repeat

    /// human-readable summary (logs/song_memory.txt)
    std::string describe() const;

private:
    struct Frame { uint8_t bass = 0, mid = 0, treble = 0, loud = 0; };
    void grow(size_t n);

    std::string        mKey;
    int64_t            mDurationMs = 0;
    int                mPlays      = 0;
    std::vector<Frame>   mFrames;  // averaged map
    std::vector<uint8_t> mCounts;  // plays that heard each frame (0 = unknown)
    std::vector<Frame>   mPlay;    // this play
    std::vector<uint8_t> mPlayHeard;
    int                mHeardFrames = 0;
    long               mLastIdx     = -10;
    std::vector<float> mEnergy;    // analysed curve (normalised), NaN = unknown
};

} // namespace bf::lyrics
