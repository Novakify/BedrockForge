// LyricsForge - [BF] local lyric overrides: your own fixed / edited lyrics for a song, preferred over every source.
// Original code for Bedrock Forge.
//
// config/lyricsflow_overrides/<Artist> - <Title>.lrc  (enhanced LRC: [mm:ss.xx] lines, optional <mm:ss.xx> word tags)
// The panel's "Edit lyrics" writes the current lyrics there (word timing included) and opens it in Notepad; the runtime
// watches the file and swaps the lyrics in as soon as you save, so timing fixes can be made while the song plays.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lyrics/core/Lyrics.h"

namespace bf::lyrics {

/// "<Artist> - <Title>.lrc", characters Windows forbids replaced, trimmed, at most ~140 bytes
std::string overrideFileName(std::string_view artist, std::string_view title);
/// enhanced LRC of the lyrics (synthetic music-note lines skipped), with a short how-to header
std::string toOverrideLrc(Lyrics const& l, std::string_view artist, std::string_view title);
/// a template for a song that has no lyrics yet
std::string emptyOverrideLrc(std::string_view artist, std::string_view title);
/// [BF] tap-sync: lines (by Line::index) given new start times; untapped lines move with their nearest taps (linear
/// in between, the nearest tap's shift before the first / after the last). Words move with their line.
Lyrics retimeLines(Lyrics const& l, std::vector<std::pair<int, int64_t>> const& taps);
/// parse an override file (null = nothing usable in it)
std::optional<Lyrics> parseOverride(std::string_view text, int64_t durationMs);

} // namespace bf::lyrics
