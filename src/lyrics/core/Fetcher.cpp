// LyricsFlow port - lyric search. Logic ported from LyricsFlow v2.0 (374m, MIT); see Fetcher.h.
#include "lyrics/core/Fetcher.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>

#include "lyrics/core/Json.h"

namespace bf::lyrics::fetch {

using text::Variant;

namespace {
constexpr char kLrclib[] = "https://lrclib.net/api/";
constexpr char kMxm[]    = "https://apic-desktop.musixmatch.com/ws/1.1/";
constexpr char kMxmApp[] = "web-desktop-app-v1.0";

Headers const& mxmHeaders() {
    static Headers const h{{"authority", "apic-desktop.musixmatch.com"}, {"cookie", "AWSELBCORS=0; AWSELB=0"}};
    return h;
}
Headers const& neHeaders() {
    static Headers const h{{"Referer", "https://music.163.com/"},
                           {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"}};
    return h;
}

std::string base36(uint64_t v) {
    static char const* d = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string        s;
    do { s.insert(s.begin(), d[v % 36]); v /= 36; } while (v);
    return s;
}
std::string nonce() { return base36(uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())); }

std::optional<json::Value> parseBody(HttpResponse const& r) {
    if (r.code != 200) return std::nullopt;
    return json::parse(r.body);
}
std::string sv(json::Value const& o, std::string_view k) { return o.hasValue(k) ? o[k].str() : std::string(); }

// ---- tiny "future" that never blocks on destruction (abandoned searches finish in the background)
struct Slot {
    std::mutex              m;
    std::condition_variable cv;
    bool                    done = false;
    std::optional<Lyrics>   value;
};
using Task = std::function<std::optional<Lyrics>()>;

std::shared_ptr<Slot> spawn(Task task, bool parallel) {
    auto slot = std::make_shared<Slot>();
    auto run  = [slot, task = std::move(task)] {
        std::optional<Lyrics> r;
        try { r = task(); } catch (...) { r.reset(); }
        {
            std::lock_guard l(slot->m);
            slot->value = std::move(r);
            slot->done  = true;
        }
        slot->cv.notify_all();
    };
    if (parallel) std::thread(std::move(run)).detach();
    else run();
    return slot;
}
std::optional<Lyrics> waitFor(Slot& s, int64_t deadline) {
    std::unique_lock l(s.m);
    int64_t          wait = std::max<int64_t>(1, deadline - nowMs());
    s.cv.wait_for(l, std::chrono::milliseconds(wait), [&] { return s.done; });
    return s.done ? s.value : std::nullopt;
}

using Source = std::function<std::optional<Lyrics>(double maxDiff)>;

// LyricsFetcher.firstSynced: run all candidates at once, return the first synced result *in list order*.
std::optional<Lyrics> firstSynced(std::vector<Source> const& sources, int64_t deadline, double maxDiff, bool parallel) {
    std::vector<std::shared_ptr<Slot>> slots;
    for (auto const& src : sources)
        slots.push_back(spawn([src, maxDiff]() -> std::optional<Lyrics> {
            auto l = src(maxDiff);
            return (l && l->synced) ? l : std::nullopt;
        }, parallel));
    for (auto& s : slots)
        if (auto l = waitFor(*s, deadline)) return l;
    return std::nullopt;
}

bool titleMatch(std::string const& got, std::string const& want) {
    return got == want || text::startsWith(got, want) || text::startsWith(want, got);
}

std::optional<Lyrics> fromLrclib(json::Value const& o, int64_t durationMs) {
    if (durationMs <= 0 && o.hasValue("duration")) durationMs = int64_t(o["duration"].asNumber() * 1000.0);
    if (o.hasValue("syncedLyrics"))
        if (auto l = parseLrc(o["syncedLyrics"].str(), durationMs)) return l;
    if (o.hasValue("plainLyrics")) return spread(o["plainLyrics"].str(), durationMs, "lrclib");
    return std::nullopt;
}

// NetEase song search -> best id. strict = neteaseId() (artist must match, dd <= min(maxDiff, 6)).
int64_t neteasePick(HttpResponse const& r, Variant const& v, int64_t durationMs, double maxDiff, bool strict) {
    auto root = parseBody(r);
    if (!root || !(*root)["result"].has("songs")) return -1;
    std::string wantTitle  = text::norm(v.title);
    std::string wantArtist = text::norm(text::mainArtist(v.artist));
    int64_t     bestId     = -1;
    double      bestScore  = 1e300;
    for (auto const& o : (*root)["result"]["songs"].items()) {
        std::string name = text::norm(sv(o, "name"));
        if (!titleMatch(name, wantTitle)) continue;
        std::string arts;
        for (auto const& a : o["artists"].items()) arts += text::norm(sv(a, "name")) + "|";
        bool   artistOk = wantArtist.empty() || text::contains(arts, wantArtist);
        double dd = durationMs > 0 && o.has("duration") ? std::fabs(double(o["duration"].asInt() - durationMs)) / 1000.0 : 0.0;
        double score;
        if (strict) {
            if (!artistOk || dd > std::min(maxDiff, 6.0)) continue;
            score = dd * 4.0 + (name == wantTitle ? 0 : 20);
        } else {
            if ((!artistOk && dd > 2.5) || dd > maxDiff) continue;
            score = dd * 4.0 + (artistOk ? 0 : 50) + (name == wantTitle ? 0 : 20);
        }
        if (score < bestScore) {
            bestScore = score;
            bestId    = o["id"].asInt(-1);
        }
    }
    return bestId;
}

// ---- Musixmatch token (cached 6 h, like the Java mod)
std::mutex  gMxmMutex;
std::string gMxmToken;
int64_t     gMxmTokenAt = 0;

std::string mxmToken(HttpGet const& http, bool fresh, int64_t deadline) {
    {
        std::lock_guard l(gMxmMutex);
        if (!fresh && !gMxmToken.empty() && nowMs() - gMxmTokenAt < 21600000) return gMxmToken;
    }
    auto r = http(std::string(kMxm) + "token.get?app_id=" + kMxmApp + "&t=" + nonce(), mxmHeaders(), deadline);
    auto j = parseBody(r);
    if (!j) return {};
    auto const& msg = (*j)["message"];
    if (msg["header"]["status_code"].asInt() != 200) return {};
    std::string t = sv(msg["body"], "user_token");
    if (text::isBlank(t) || text::startsWith(t, "UpgradeOnly")) return {};
    std::lock_guard l(gMxmMutex);
    gMxmToken   = t;
    gMxmTokenAt = nowMs();
    return t;
}
std::optional<json::Value> mxmCall(HttpGet const& http, std::string const& method, std::vector<std::pair<std::string, std::string>> params,
                                   int64_t deadline) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string token = mxmToken(http, attempt > 0, deadline);
        if (token.empty()) return std::nullopt;
        auto q = params;
        q.emplace_back("app_id", kMxmApp);
        q.emplace_back("usertoken", token);
        q.emplace_back("t", nonce());
        auto r = http(std::string(kMxm) + method + "?" + query(q), mxmHeaders(), deadline);
        auto j = parseBody(r);
        if (!j) return std::nullopt;
        auto const& msg  = (*j)["message"];
        auto        code = msg["header"]["status_code"].asInt();
        if (code != 401) {
            if (code == 200 && msg["body"].isObject()) return msg["body"];
            return std::nullopt;
        }
        std::lock_guard l(gMxmMutex);
        gMxmToken.clear();
    }
    return std::nullopt;
}
} // namespace

int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string urlEncode(std::string_view s) {
    static char const* hex = "0123456789ABCDEF";
    std::string        out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '*' || c == '_') out += char(c);
        else if (c == ' ') out += '+';
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string query(std::vector<std::pair<std::string, std::string>> const& kv) {
    std::string out;
    for (auto const& [k, v] : kv) {
        if (!out.empty()) out += '&';
        out += urlEncode(k) + "=" + urlEncode(v);
    }
    return out;
}

void resetMusixmatchToken() {
    std::lock_guard l(gMxmMutex);
    gMxmToken.clear();
    gMxmTokenAt = 0;
}

// ------------------------------------------------------------------ LRCLIB
std::optional<Lyrics> lrclibExact(HttpGet const& http, Variant const& v, int64_t durationMs, double, int64_t deadline) {
    std::vector<std::pair<std::string, std::string>> q{{"artist_name", v.artist}, {"track_name", v.title}};
    if (durationMs > 0) q.emplace_back("duration", std::to_string(int64_t(std::floor(double(durationMs) / 1000.0 + 0.5))));
    auto r = http(std::string(kLrclib) + "get?" + query(q), {}, deadline);
    auto j = parseBody(r);
    return j ? fromLrclib(*j, durationMs) : std::nullopt;
}
std::optional<Lyrics> lrclibSearch(HttpGet const& http, Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline) {
    std::vector<std::pair<std::string, std::string>> q;
    if (v.artist.empty()) q = {{"q", v.title}};
    else q = {{"track_name", v.title}, {"artist_name", v.artist}};
    return lrclibBestOf(http(std::string(kLrclib) + "search?" + query(q), {}, deadline), durationMs, v, maxDiff);
}
std::optional<Lyrics> lrclibBestOf(HttpResponse const& r, int64_t durationMs, Variant const& v, double maxDiff) {
    auto arr = parseBody(r);
    if (!arr || !arr->isArray()) return std::nullopt;
    json::Value const* best       = nullptr;
    double             bestScore  = 1e300;
    std::string        wantTitle  = text::norm(v.title);
    std::string        wantArtist = text::norm(text::mainArtist(v.artist));
    for (auto const& o : arr->items()) {
        if (o.has("instrumental") && o["instrumental"].asBool()) continue;
        bool   synced = o.hasValue("syncedLyrics");
        double score  = synced ? 0.0 : 1000.0;
        if (durationMs > 0 && o.hasValue("duration")) {
            double dd = std::fabs(o["duration"].asNumber() - double(durationMs) / 1000.0);
            if (dd > maxDiff) continue;
            score += dd * 4.0;
        }
        std::string name = text::norm(sv(o, "trackName")), art = text::norm(sv(o, "artistName"));
        bool titleOk  = !name.empty() && (text::contains(name, wantTitle) || text::contains(wantTitle, name));
        bool artistOk = wantArtist.empty() || (!art.empty() && (text::contains(art, wantArtist) || text::contains(wantArtist, art)));
        if (!titleOk && !artistOk) continue;
        if (!titleOk) score += 200.0;
        if (!artistOk) score += 300.0;
        if (score < bestScore) { bestScore = score; best = &o; }
    }
    return (best && bestScore <= 1200.0) ? fromLrclib(*best, durationMs) : std::nullopt;
}
std::optional<Lyrics> lrclibBestByTitle(HttpResponse const& r, int64_t durationMs, Variant const& v, double maxDiff) {
    auto arr = parseBody(r);
    if (!arr || !arr->isArray()) return std::nullopt;
    std::string        wantArtist = text::norm(text::mainArtist(v.artist));
    std::string        wantTitle  = text::norm(v.title);
    json::Value const* best       = nullptr;
    double             bestScore  = 1e300;
    for (auto const& o : arr->items()) {
        if (o.has("instrumental") && o["instrumental"].asBool()) continue;
        std::string name = text::norm(sv(o, "trackName")), art = text::norm(sv(o, "artistName"));
        if (!(name == wantTitle || text::startsWith(name, wantTitle))) continue;
        bool   artistOk = wantArtist.empty() || text::contains(art, wantArtist) || text::contains(wantArtist, art);
        double dd       = durationMs > 0 && o.hasValue("duration") ? std::fabs(o["duration"].asNumber() - double(durationMs) / 1000.0) : 0.0;
        if ((!artistOk && dd > 2.5) || dd > maxDiff) continue;
        double score = (o.hasValue("syncedLyrics") ? 0 : 500) + dd * 4.0 + (artistOk ? 0 : 50);
        if (score < bestScore) { bestScore = score; best = &o; }
    }
    return (best && (durationMs <= 0 || bestScore <= 560.0)) ? fromLrclib(*best, durationMs) : std::nullopt;
}
std::optional<Lyrics> lrclibStretched(HttpGet const& http, Variant const& v, int64_t durationMs, int64_t deadline) {
    if (durationMs <= 0) return std::nullopt;
    std::vector<std::pair<std::string, std::string>> q{{"track_name", v.title}};
    if (!v.artist.empty()) q.emplace_back("artist_name", v.artist);
    auto arr = parseBody(http(std::string(kLrclib) + "search?" + query(q), {}, deadline));
    if (!arr || !arr->isArray()) return std::nullopt;
    std::string        wantTitle = text::norm(v.title), wantArtist = text::norm(v.artist);
    json::Value const* best      = nullptr;
    double             bestScore = 1e300;
    for (auto const& o : arr->items()) {
        if (!o.hasValue("syncedLyrics") || !o.hasValue("duration")) continue;
        std::string rawName = sv(o, "trackName");
        std::string name = text::norm(rawName), art = text::norm(sv(o, "artistName"));
        if (text::isSpeedVersion(rawName) || !titleMatch(name, wantTitle)) continue;
        if (!(wantArtist.empty() || text::contains(art, wantArtist) || text::contains(wantArtist, art))) continue;
        double k = double(durationMs) / (o["duration"].asNumber() * 1000.0);
        if (k < 0.55 || k > 1.6) continue;
        double score = (name == wantTitle ? 0 : 10) + (text::isRemix(rawName) ? 50 : 0);
        if (score < bestScore) { bestScore = score; best = &o; }
    }
    if (!best) return std::nullopt;
    auto orig = int64_t((*best)["duration"].asNumber() * 1000.0);
    auto l    = parseLrc((*best)["syncedLyrics"].str(), orig);
    if (!l) return std::nullopt;
    return scaled(*l, double(durationMs) / double(orig), durationMs);
}

// ------------------------------------------------------------------ NetEase
std::optional<Lyrics> netease(HttpGet const& http, Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline) {
    std::string q  = text::trimmed(v.artist + " " + v.title);
    auto        r  = http("https://music.163.com/api/search/get?" + query({{"s", q}, {"type", "1"}, {"limit", "10"}}), neHeaders(), deadline);
    int64_t     id = neteasePick(r, v, durationMs, maxDiff, false);
    if (id < 0) return std::nullopt;
    auto lo = parseBody(http("https://music.163.com/api/song/lyric?id=" + std::to_string(id) + "&lv=1&kv=1&tv=-1", neHeaders(), deadline));
    if (!lo || !(*lo)["lrc"].has("lyric")) return std::nullopt;
    static std::regex const lead(R"(^(\[[^\]]*\])+)");
    static std::regex const credit(R"(\s*(lyrics|composer|producer|arranger|written by)\s*(:|\xEF\xBC\x9A).*)", std::regex::icase);
    std::string             clean;
    for (auto const& line : text::splitLines((*lo)["lrc"]["lyric"].str())) {
        std::string body = std::regex_replace(line, lead, "", std::regex_constants::format_first_only);
        // ".*[一-鿿].*[:：].*" (Chinese credit lines) and English credit lines are dropped
        bool han = false, hanColon = false;
        for (uint32_t c : text::codepoints(body)) {
            if (han && (c == ':' || c == 0xFF1A)) { hanColon = true; break; }
            if (text::isHan(c)) han = true;
        }
        if (hanColon || std::regex_match(body, credit)) continue;
        clean += line + "\n";
    }
    auto l = parseLrc(clean, durationMs);
    return (l && l->lines.size() >= 3) ? l : std::nullopt;
}
std::optional<Lyrics> neteaseWords(HttpGet const& http, Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline) {
    std::string q  = text::trimmed(v.artist + " " + v.title);
    auto        r  = http("https://music.163.com/api/search/get?" + query({{"s", q}, {"type", "1"}, {"limit", "10"}}), neHeaders(), deadline);
    int64_t     id = neteasePick(r, v, durationMs, maxDiff, true);
    if (id < 0) return std::nullopt;
    auto lo = parseBody(http("https://music.163.com/api/song/lyric/v1?id=" + std::to_string(id) + "&cp=false&tv=0&lv=0&rv=0&kv=0&yv=0&ytv=0&yrv=0",
                             neHeaders(), deadline));
    if (!lo || !(*lo)["yrc"].has("lyric")) return std::nullopt;
    auto y = parseYrc((*lo)["yrc"]["lyric"].str(), durationMs);
    return (y && sameScript(v.artist, v.title, *y)) ? y : std::nullopt;
}

// ------------------------------------------------------------------ Musixmatch
std::optional<Lyrics> musixmatchWords(HttpGet const& http, Variant const& v, int64_t durationMs, double maxDiff, int64_t deadline) {
    auto body = mxmCall(http, "matcher.track.get", {{"q_artist", v.artist}, {"q_track", v.title}}, deadline);
    if (!body || !body->has("track")) return std::nullopt;
    auto const& tr         = (*body)["track"];
    std::string gotTitle   = text::norm(sv(tr, "track_name")), gotArtist = text::norm(sv(tr, "artist_name"));
    std::string wantTitle  = text::norm(v.title), wantArtist = text::norm(v.artist);
    bool        titleOk    = !wantTitle.empty() && titleMatch(gotTitle, wantTitle);
    bool        artistOk   = wantArtist.empty() || text::contains(gotArtist, wantArtist) || text::contains(wantArtist, gotArtist);
    if (!titleOk || !artistOk) return std::nullopt;
    if (durationMs > 0 && tr.has("track_length") && tr["track_length"].asNumber() > 0.0)
        if (std::fabs(tr["track_length"].asNumber() - double(durationMs) / 1000.0) > maxDiff) return std::nullopt;
    std::string id = tr["track_id"].str();
    if (tr.has("has_richsync") && tr["has_richsync"].asInt() == 1) {
        auto rs = mxmCall(http, "track.richsync.get", {{"track_id", id}}, deadline);
        if (rs && rs->has("richsync")) {
            auto l = parseRichsync((*rs)["richsync"]["richsync_body"].str(), durationMs);
            if (l && sameScript(v.artist, v.title, *l)) return l;
        }
    }
    if (tr.has("has_subtitles") && tr["has_subtitles"].asInt() == 1) {
        auto st = mxmCall(http, "track.subtitle.get", {{"track_id", id}, {"subtitle_format", "lrc"}}, deadline);
        if (st && st->has("subtitle")) {
            auto l = parseLrc((*st)["subtitle"]["subtitle_body"].str(), durationMs);
            if (l && l->synced && l->lines.size() >= 3 && sameScript(v.artist, v.title, *l)) return l;
        }
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ the whole search
std::optional<Lyrics> fetch(HttpGet const& http, std::string const& artist, std::string const& title, std::string const&, int64_t durationMs,
                            Options const& opt) {
    int64_t const deadline = nowMs() + opt.deadlineMs;
    auto          vars     = text::variants(artist, title);
    if (vars.empty()) return std::nullopt;
    Variant const v0 = vars.front(), vLast = vars.back();
    int64_t const dur = durationMs;

    std::vector<Source> first{
        [=](double md) { return lrclibExact(http, v0, dur, md, deadline); },
        [=](double md) { return lrclibSearch(http, v0, dur, md, deadline); },
        [=](double md) {
            auto r = http(std::string(kLrclib) + "search?" + query({{"q", text::trimmed(vLast.artist + " " + vLast.title)}}), {}, deadline);
            return lrclibBestOf(r, dur, vLast, md);
        },
        [=](double md) {
            auto r = http(std::string(kLrclib) + "search?" + query({{"track_name", v0.title}}), {}, deadline);
            return lrclibBestByTitle(r, dur, v0, md);
        },
        [=](double md) { return netease(http, v0, dur, md, deadline); },
    };
    std::vector<Source> more;
    for (size_t i = 1; i < vars.size(); ++i) {
        Variant v = vars[i];
        more.push_back([=](double md) { return lrclibExact(http, v, dur, md, deadline); });
        more.push_back([=](double md) { return lrclibSearch(http, v, dur, md, deadline); });
        if (i == 1) {
            more.push_back([=](double md) {
                auto r = http(std::string(kLrclib) + "search?" + query({{"track_name", v.title}}), {}, deadline);
                return lrclibBestByTitle(r, dur, v, md);
            });
            more.push_back([=](double md) { return netease(http, v, dur, md, deadline); });
        }
    }

    // sped up / slowed / nightcore uploads: exact match first, else the original song's lyrics stretched to fit
    if (text::isSpeedVersion(title)) {
        Variant             clean{text::mainArtist(artist), text::clean(text::removeSpeedTags(title))};
        std::vector<Source> sp{
            [=](double md) { return lrclibExact(http, v0, dur, md, deadline); },
            [=](double) { return lrclibStretched(http, clean, dur, deadline); },
        };
        return firstSynced(sp, deadline, 3.0, opt.parallel);
    }

    bool const   remix   = text::isRemix(title);
    double const wordsMd = remix ? 2.0 : 3.0;
    auto         wordsF  = spawn([=] { return neteaseWords(http, v0, dur, wordsMd, deadline); }, opt.parallel);
    auto         mxmF    = spawn([=] { return musixmatchWords(http, v0, dur, wordsMd, deadline); }, opt.parallel);

    auto l = firstSynced(first, deadline, remix ? 2.0 : 3.0, opt.parallel);
    if (!l && !more.empty()) l = firstSynced(more, deadline, remix ? 2.0 : 3.0, opt.parallel);

    bool fromMxm = false;
    if (auto w = waitFor(*mxmF, nowMs() + std::max<int64_t>(1, std::min<int64_t>(4000, deadline - nowMs())))) {
        if (w->lines.size() >= 3) { l = w; fromMxm = true; }
    }
    if (!fromMxm && (!l || !l->wordSynced))
        if (auto w = waitFor(*wordsF, nowMs() + std::max<int64_t>(1, std::min<int64_t>(4000, deadline - nowMs()))))
            if (w->wordSynced && w->lines.size() >= 3) l = w;

    // last resort: same candidates with a 10 s duration tolerance
    if (!l && nowMs() < deadline - 1500) l = firstSynced(first, deadline, 10.0, opt.parallel);
    if (l && !sameScript(v0.artist, v0.title, *l)) l.reset();
    return l;
}

} // namespace bf::lyrics::fetch
