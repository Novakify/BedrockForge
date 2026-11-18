// LyricsFlow port - text helpers: UTF-8, case folding, letter/script tests, and TitleCleaner (ported from
// dev.lyricsflow.media.TitleCleaner, LyricsFlow v2.0 by 374m, MIT). Ported for Fernando's personal build.
//
// Java uses java.text.Normalizer (NFD) and Unicode categories (\p{L}, \p{M}, UnicodeScript). We don't pull in ICU:
// folding covers ASCII, Latin-1, Latin Extended-A, Turkish dotless i, Greek and Cyrillic, which is what song titles
// need in practice. Deviations from Java are listed in docs/LYRICSFLOW_SPEC.md ("Text folding").
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bf::lyrics::text {

// ---- UTF-8
/// Decode one code point at s[i], advance i. Invalid bytes decode as U+FFFD (one byte consumed).
uint32_t              next(std::string_view s, size_t& i);
void                  append(std::string& out, uint32_t cp);
std::vector<uint32_t> codepoints(std::string_view s);
size_t                length(std::string_view s); // code points

// ---- character classes (approximations of Java's Character / UnicodeScript, see header comment)
uint32_t lower(uint32_t cp);
uint32_t upper(uint32_t cp);
bool     isLetter(uint32_t cp);
bool     isLetterOrDigit(uint32_t cp);
bool     isLatin(uint32_t cp);     // UnicodeScript.LATIN for letters
bool     isCjkOrHangul(uint32_t cp); // [　-鿿가-힯] (YRC word split)
bool     isHan(uint32_t cp);       // [一-鿿]
bool     isRtl(uint32_t cp);       // Hebrew / Arabic blocks (Fonts.isRtl)

// ---- string helpers
std::string              toLower(std::string_view s);
std::string              toUpper(std::string_view s); // Lyrics.show() with allCaps
/// Java String.trim(): strips chars <= ' ' at both ends.
std::string_view         trim(std::string_view s);
std::string              trimmed(std::string_view s);
/// Java s.trim().split("\\s+") (ASCII whitespace); an all-blank string gives one empty piece, like Java.
std::vector<std::string> splitWs(std::string_view s);
std::vector<std::string> splitLines(std::string_view s); // split("\\r?\\n")
std::string              join(std::vector<std::string> const& parts, std::string_view sep);
bool                     isBlank(std::string_view s);
bool                     startsWith(std::string_view s, std::string_view p);
bool                     endsWith(std::string_view s, std::string_view p);
bool                     contains(std::string_view s, std::string_view p);
/// collapse runs of 2+ spaces to one (replaceAll("\\s{2,}", " "))
std::string              squeezeSpaces(std::string_view s);

/// TitleCleaner.fold: lower-case (Locale.ROOT), ı/İ -> i, then strip diacritics (NFD + remove marks).
std::string fold(std::string_view s);
/// LyricsFetcher.norm: fold, keep only letters and digits; if that empties it, lower-case with whitespace removed.
std::string norm(std::string_view s);

// ---- TitleCleaner
std::string              clean(std::string_view s);
std::string              mainArtist(std::string_view a);
std::string              stripFeat(std::string_view title); // FEAT.matcher(title).replaceAll(" ")
/// [artist, title] from a YouTube tab title + channel name.
std::vector<std::string> splitYoutube(std::string_view title, std::string_view channel);
/// Search variants in priority order, deduplicated case-insensitively (TitleCleaner.variants).
struct Variant { std::string artist, title; };
std::vector<Variant>     variants(std::string_view artist, std::string_view title);

// LyricsFetcher patterns
bool                     isSpeedVersion(std::string_view title);  // SPEED: sped up / slowed (+reverb) / nightcore / daycore
std::string              removeSpeedTags(std::string_view title); // SPEED.matcher(raw).replaceAll(" ")
bool                     isRemix(std::string_view title);         // REMIX: remix / rmx / bootleg / mashup / flip / vip mix

} // namespace bf::lyrics::text
