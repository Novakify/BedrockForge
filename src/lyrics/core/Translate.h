// LyricsForge - [LyricsForge] automatic translations: songs (or lines) that aren't English get a small English line
// under the original (Scene's translationUnder, the same place override-file / NetEase translations go).
// Original code for Bedrock Forge.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "lyrics/core/Fetcher.h"
#include "lyrics/core/Lyrics.h"

namespace bf::lyrics::translate {

/// the line is in a non-Latin script (Japanese, Korean, Chinese, Cyrillic, Greek, Arabic, Hebrew, Thai, Devanagari)
bool nonLatin(std::string_view line);
/// the lyrics are worth sending to the translator: a non-Latin line still without English, or (Latin script) a song
/// that doesn't read as English with lines still without English
bool needsTranslation(Lyrics const& l);
/// fills Line::translation for the lines whose English differs from the original (English lines stay as they are).
/// Returns how many lines got one. http = the same GET the fetcher uses (Google's public gtx endpoint, sl=auto).
int translateToEnglish(fetch::HttpGet const& http, Lyrics& l, int64_t deadlineMs);
/// [LyricsForge] lines the fonts can't draw (canDraw false for 25%+ of their letters): their English translation takes
/// their place, or, with none, the line is dropped - never a row of '?'. Returns how many lines changed.
int fitToFonts(Lyrics& l, std::function<bool(uint32_t)> const& canDraw);
/// counts toward a line's letters in fitToFonts (not spaces, digits, punctuation, symbols or emoji)
bool countsAsLetter(uint32_t c);
/// the translated text from one gtx reply ([[["en","orig",...],...],...]); "" when it can't be read
std::string parseGtx(std::string_view body);

} // namespace bf::lyrics::translate
