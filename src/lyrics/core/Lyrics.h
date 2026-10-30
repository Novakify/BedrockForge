// LyricsFlow port - lyrics model + every parser LyricsFlow v2.0 uses (LRC incl. enhanced <mm:ss.xx> word tags,
// NetEase YRC, Musixmatch richsync, plain-text spread) and its syllable-based word-time estimate.
// Ported from dev.lyricsflow.lyrics.{Lyrics, LyricsFetcher} (LyricsFlow v2.0 by 374m, MIT) for Fernando's personal
// build. Times are milliseconds of song position.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bf::lyrics {

struct Word {
    std::string text;
    int64_t     start = 0, end = 0;         // current timing (retime() may change it)
    int64_t     origStart = 0, origEnd = 0; // as parsed / estimated
    static Word of(std::string text, int64_t s, int64_t e) { return {std::move(text), s, e, s, e}; }
};

struct Line {
    int               index = 0; // >= 0 lyric line, < 0 synthetic "music notes" line
    std::string       text;
    int64_t           start = 0, end = 0;
    std::vector<Word> words;
    bool              estimated = true; // words were spread by syllables, not real word timing

    /// LyricsFlow Line.retime: AudioSync's k stretches estimated word timing inside the line.
    void    retime(double k);
    int64_t guessedEnd() const;
    /// index of the word being sung at t (last word with start <= t), -1 before the first
    int     wordAt(int64_t t) const;

    /// Synthetic "♪ ♫" line for instrumental gaps.
    static Line notes(int index, std::string text, int64_t start, int64_t end);
};

/// A parsed line timing: start, and end when the source gives one (-1 = take it from the next line).
struct Timing {
    int64_t start = 0;
    int64_t end   = -1;
};

struct Lyrics {
    std::vector<Line> lines;
    bool              synced     = false;
    bool              wordSynced = false; // every line carries real word timing
    std::string       source;

    /// Java Lyrics(times, texts, synced, source, durationMs, words). `words` may be empty (no word timing) or have
    /// one entry per line (an empty entry = that line has none -> its words are estimated).
    static Lyrics build(std::vector<Timing> const& times, std::vector<std::string> const& texts, bool synced, std::string source,
                        int64_t durationMs, std::vector<std::vector<Word>> const* words = nullptr);

    /// Line showing at t (null once t passes its end).
    Line const* lineAt(int64_t t) const;
    /// Last line that started at or before t, even if it ended.
    Line const* lastStartedBefore(int64_t t) const;
    bool        empty() const { return lines.empty(); }
};

// ---- estimation helpers (exposed for tests)
int    syllables(std::string_view word);
/// Typical singing rate (syllables/s) of the song: 75th percentile of line rates * 1.05, clamped 2.5..10 (4.5 default).
double songRate(std::vector<Timing> const& times, std::vector<std::string> const& texts);
/// Spread a line's words over [start, end] by syllables (Lyrics.Line.splitWords).
std::vector<Word> splitWords(std::string_view text, int64_t start, int64_t end, double rate);

// ---- parsers (null result = unusable, same thresholds as the Java code)
std::optional<Lyrics> parseLrc(std::string_view lrc, int64_t durationMs, std::string source = "lrclib");
std::optional<Lyrics> parseYrc(std::string_view yrc, int64_t durationMs);
std::optional<Lyrics> parseRichsync(std::string_view richsyncJson, int64_t durationMs);
std::optional<Lyrics> spread(std::string_view plain, int64_t durationMs, std::string source);
/// Lyrics for a sped-up/slowed upload: every time * k (LyricsFetcher.scaled).
Lyrics scaled(Lyrics const& l, double k, int64_t durationMs);
/// Rejects lyrics in another script than the (mostly Latin) title (LyricsFetcher.sameScript).
bool sameScript(std::string_view artist, std::string_view title, Lyrics const& l);
/// Disk cache form (MediaManager readCache/writeCache): exact lines + word timing, round-trips losslessly.
std::string           toCacheJson(Lyrics const& l);
std::optional<Lyrics> fromCacheJson(std::string_view json);
/// Cache file name for a track key (lower(artist + "\0" + title)): 16 hex digits of FNV-1a 64 + ".json".
std::string           cacheFileName(std::string_view trackKey);
/// Lyrics.show(): upper-case when allCaps is on.
std::string show(std::string_view s, bool allCaps);

} // namespace bf::lyrics
