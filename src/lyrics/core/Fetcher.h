// LyricsFlow port - lyric search across LRCLIB, NetEase (LRC + YRC word timing) and Musixmatch (richsync word timing).
// Ported from dev.lyricsflow.lyrics.LyricsFetcher (LyricsFlow v2.0 by 374m, MIT) for Fernando's personal build.
//
// Pure logic: the HTTP transport is injected (HttpGet), so the whole search - candidate scoring, duration checks,
// fallbacks, the speed-up/slowed "stretched" path - runs in unit tests against saved responses. On Windows the
// transport is WinHTTP (lyrics/win/Http_win.cpp).
//
// Note: Musixmatch's desktop API (apic-desktop.musixmatch.com, "web-desktop-app-v1.0") is unofficial and
// token-gated. It is kept as one source among several for this private build only.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lyrics/core/Lyrics.h"
#include "lyrics/core/Text.h"

namespace bf::lyrics::fetch {

struct HttpResponse {
    int         code = 0; // 0 = transport error / timeout
    std::string body;
};
using Headers = std::vector<std::pair<std::string, std::string>>;
/// GET url with headers. deadlineMs is an absolute steady-clock time (nowMs()); give up when it passes.
using HttpGet = std::function<HttpResponse(std::string const& url, Headers const& headers, int64_t deadlineMs)>;

int64_t     nowMs(); // steady clock, ms
std::string urlEncode(std::string_view s); // java.net.URLEncoder (UTF-8): space -> '+'
std::string query(std::vector<std::pair<std::string, std::string>> const& kv);

struct Options {
    bool    parallel   = true;  // false = run every candidate inline (tests)
    int64_t deadlineMs = 15000; // whole search budget, like the Java mod
};

/// The whole search (LyricsFetcher.fetch). Returns synced lyrics or nothing.
std::optional<Lyrics> fetch(HttpGet const& http, std::string const& artist, std::string const& title, std::string const& album,
                            int64_t durationMs, Options const& opt = {});

// ---- individual sources (exposed for tests); maxDiff = allowed |duration difference| in seconds
std::optional<Lyrics> lrclibExact(HttpGet const& http, text::Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline);
std::optional<Lyrics> lrclibSearch(HttpGet const& http, text::Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline);
std::optional<Lyrics> lrclibBestOf(HttpResponse const& r, int64_t durationMs, text::Variant const& v, double maxDiff);
std::optional<Lyrics> lrclibBestByTitle(HttpResponse const& r, int64_t durationMs, text::Variant const& v, double maxDiff);
std::optional<Lyrics> lrclibStretched(HttpGet const& http, text::Variant const& v, int64_t durationMs, int64_t deadline);
std::optional<Lyrics> netease(HttpGet const& http, text::Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline);
std::optional<Lyrics> neteaseWords(HttpGet const& http, text::Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline);
std::optional<Lyrics> musixmatchWords(HttpGet const& http, text::Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline);
/// Forget the cached Musixmatch user token (tests).
void resetMusixmatchToken();

} // namespace bf::lyrics::fetch
