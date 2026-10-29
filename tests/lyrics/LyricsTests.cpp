// Unit tests for the LyricsFlow port core (src/lyrics/core). No game, no network: saved sample responses in
// fixtures/, and golden outputs produced by running LyricsFlow v2.0's own Java parsers (fixtures/golden_java.json,
// see tests/lyrics/README.md). Run: LyricsCoreTests [fixtures dir]
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Engine.h"
#include "lyrics/core/Fetcher.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Json.h"
#include "lyrics/core/Lyrics.h"
#include "lyrics/core/Media.h"
#include "lyrics/core/Physics.h"
#include "lyrics/core/Scene.h"
#include "lyrics/core/Text.h"

using namespace bf::lyrics;

// ---------------------------------------------------------------- mini test framework
namespace {
int         gFails = 0, gChecks = 0;
std::string gDir   = "tests/lyrics/fixtures";
std::string gCur;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++gChecks;                                                                                                     \
        if (!(cond)) { ++gFails; std::printf("  FAIL [%s] %s:%d  %s\n", gCur.c_str(), __FILE__, __LINE__, #cond); }   \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        ++gChecks;                                                                                                     \
        auto _a = (a);                                                                                                 \
        auto _b = (b);                                                                                                 \
        if (!(_a == _b)) {                                                                                             \
            ++gFails;                                                                                                  \
            std::ostringstream _s;                                                                                     \
            _s << _a << " != " << _b;                                                                                  \
            std::printf("  FAIL [%s] %s:%d  %s == %s  (%s)\n", gCur.c_str(), __FILE__, __LINE__, #a, #b, _s.str().c_str()); \
        }                                                                                                              \
    } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= double(eps))

std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Reg {
    Reg(char const* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name)                                                                                                     \
    void test_##name();                                                                                                \
    Reg  reg_##name(#name, test_##name);                                                                               \
    void test_##name()

std::string readFile(std::string const& name) {
    std::ifstream in(gDir + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
json::Value const& golden() {
    static json::Value g = *json::parse(readFile("golden_java.json"));
    return g;
}
void compareToGolden(Lyrics const& l, json::Value const& g) {
    CHECK_EQ(l.synced, g["synced"].asBool());
    CHECK_EQ(l.wordSynced, g["wordSynced"].asBool());
    CHECK_EQ(l.source, g["source"].asString());
    CHECK_EQ(l.lines.size(), g["lines"].size());
    for (size_t i = 0; i < std::min(l.lines.size(), g["lines"].size()); ++i) {
        auto const& gl = g["lines"][i];
        auto const& ln = l.lines[i];
        CHECK_EQ(ln.text, gl["text"].asString());
        CHECK_EQ(ln.start, gl["start"].asInt());
        CHECK_EQ(ln.end, gl["end"].asInt());
        CHECK_EQ(ln.estimated, gl["estimated"].asBool());
        CHECK_EQ(ln.words.size(), gl["words"].size());
        for (size_t j = 0; j < std::min(ln.words.size(), gl["words"].size()); ++j) {
            CHECK_EQ(ln.words[j].text, gl["words"][j][0].asString());
            CHECK_EQ(ln.words[j].start, gl["words"][j][1].asInt());
            CHECK_EQ(ln.words[j].end, gl["words"][j][2].asInt());
        }
    }
}

// ---- fake voxel world: ground at y < 64 everywhere, plus extra blocks / slabs
struct FakeWorld : IWorld {
    std::map<std::tuple<int, int, int>, Aabb> extra; // block -> box (full block = 0..1)
    int  groundTop = 64;                               // blocks with y < groundTop are full
    std::vector<Entity> ents;

    bool box(BlockPos p, Aabb& b) const {
        auto it = extra.find({p.x, p.y, p.z});
        if (it != extra.end()) { b = it->second; return true; }
        if (p.y < groundTop) { b = Aabb{}; return true; }
        return false;
    }
    RayHit clip(Vec3 from, Vec3 to, RayShape, bool) override {
        // march in small steps (good enough for tests); report the face we entered through
        Vec3   d   = to - from;
        double len = d.length();
        if (len < 1e-9) return {};
        int  steps = int(len / 0.02) + 1;
        Vec3 prev  = from;
        for (int i = 1; i <= steps; ++i) {
            Vec3     p  = from + d * (double(i) / steps);
            BlockPos bp = BlockPos::of(p);
            Aabb     b;
            if (box(bp, b)) {
                double lx = p.x - bp.x, ly = p.y - bp.y, lz = p.z - bp.z;
                if (lx >= b.x0 && lx <= b.x1 && ly >= b.y0 && ly <= b.y1 && lz >= b.z0 && lz <= b.z1) {
                    BlockPos pb = BlockPos::of(prev);
                    Face     f  = Face::Up;
                    if (pb.y > bp.y) f = Face::Up;
                    else if (pb.y < bp.y) f = Face::Down;
                    else if (pb.x < bp.x) f = Face::West;
                    else if (pb.x > bp.x) f = Face::East;
                    else if (pb.z < bp.z) f = Face::North;
                    else if (pb.z > bp.z) f = Face::South;
                    else if (ly > b.y1 - 0.05) f = Face::Up;
                    return {true, p, f};
                }
            }
            prev = p;
        }
        return {};
    }
    bool isAir(BlockPos p) override { Aabb b; return !box(p, b); }
    void collisionBoxes(BlockPos p, std::vector<Aabb>& out) override { Aabb b; if (box(p, b)) out.push_back(b); }
    bool hasFluid(BlockPos) override { return false; }
    bool canSeeSky(BlockPos p) override { return p.y >= groundTop; }
    int  skyLight(BlockPos p) override { return p.y >= groundTop ? 15 : 0; }
    std::vector<Entity> entities() override { return ents; }
};

// ---- fake HTTP routing URL substrings to fixture files
struct FakeHttp {
    std::vector<std::pair<std::string, std::string>> routes; // substring -> file ("" = 404)
    std::vector<std::string>                         log;
    fetch::HttpGet fn() {
        return [this](std::string const& url, fetch::Headers const&, int64_t) -> fetch::HttpResponse {
            log.push_back(url);
            for (auto const& [k, f] : routes)
                if (url.find(k) != std::string::npos) {
                    if (f.empty()) return {404, ""};
                    return {200, readFile(f)};
                }
            return {404, ""};
        };
    }
};
} // namespace

// ================================================================ JSON / text
TEST(json_basics) {
    auto v = json::parse(R"({"a":[1,2.5,-3e2,true,null,"xé🎵\n"],"b":{"c":"d"}})");
    CHECK(v.has_value());
    CHECK_EQ((*v)["a"].size(), size_t(6));
    CHECK_NEAR((*v)["a"][2].asNumber(), -300.0, 1e-9);
    CHECK_EQ((*v)["a"][5].asString(), std::string("x\xC3\xA9\xF0\x9F\x8E\xB5\n"));
    CHECK_EQ((*v)["b"]["c"].asString(), std::string("d"));
    CHECK((*v)["missing"].isNull());
    CHECK(!json::parse("{\"a\":}").has_value());
    CHECK(!json::parse("[1,2").has_value());
    CHECK_EQ(json::Value::makeNumber(200.0).str(), std::string("200"));
}

TEST(text_titlecleaner_matches_java) {
    auto const& g = golden();
    for (auto const& [in, out] : g["clean"].members()) CHECK_EQ(text::clean(in), out.asString());
    for (auto const& [in, out] : g["mainArtist"].members()) CHECK_EQ(text::mainArtist(in), out.asString());
    for (auto const& [in, out] : g["fold"].members()) CHECK_EQ(text::fold(in), out.asString());
    for (auto const& row : g["splitYoutube"].items()) {
        auto r = text::splitYoutube(row[0].asString(), row[1].asString());
        CHECK_EQ(r[0], row[2].asString());
        CHECK_EQ(r[1], row[3].asString());
    }
    for (auto const& vcase : g["variants"].items()) {
        auto r = text::variants(vcase["in"][0].asString(), vcase["in"][1].asString());
        CHECK_EQ(r.size(), vcase["out"].size());
        for (size_t i = 0; i < std::min(r.size(), vcase["out"].size()); ++i) {
            CHECK_EQ(r[i].artist, vcase["out"][i][0].asString());
            CHECK_EQ(r[i].title, vcase["out"][i][1].asString());
        }
    }
}

TEST(text_misc) {
    CHECK_EQ(text::norm("AC/DC!"), std::string("acdc"));
    CHECK_EQ(text::norm("Beyoncé"), std::string("beyonce"));
    CHECK(text::isSpeedVersion("Song (Sped Up)"));
    CHECK(text::isSpeedVersion("slowed + reverb"));
    CHECK(!text::isSpeedVersion("Speedway"));
    CHECK(text::isRemix("Song (VIP Mix)"));
    CHECK_EQ(text::toUpper("straße"), std::string("STRASSE"));
    CHECK_EQ(text::splitWs("  a  b\tc ").size(), size_t(3));
    CHECK_EQ(text::splitWs("   ").size(), size_t(1));
    CHECK(sourceMatches(Source::Spotify, "Spotify.exe"));
    CHECK(sourceMatches(Source::Youtube, "MSEdge"));
    CHECK(sourceMatches(Source::Youtube, "308046B0AF4A39CB"));
    CHECK(!sourceMatches(Source::Spotify, "chrome"));
}

// ================================================================ parsers vs the Java original
TEST(lrc_matches_java) {
    auto l = parseLrc(readFile("song.lrc"), 200000);
    CHECK(l.has_value());
    if (l) compareToGolden(*l, golden()["lrc"]);
    auto l0 = parseLrc(readFile("song.lrc"), 0);
    if (l0) compareToGolden(*l0, golden()["lrc_nodur"]);
}
TEST(enhanced_lrc_matches_java) {
    auto l = parseLrc(readFile("enhanced.lrc"), 60000);
    CHECK(l.has_value());
    if (l) compareToGolden(*l, golden()["enhanced"]);
}
TEST(yrc_matches_java) {
    auto y = parseYrc(readFile("netease_yrc.txt"), 200000);
    CHECK(y.has_value());
    if (y) compareToGolden(*y, golden()["yrc"]);
}
TEST(spread_matches_java) {
    auto s = spread("Line one here\n[bracket skip]\nSecond line of text\nThird and final line", 180000, "lrclib");
    CHECK(s.has_value());
    if (s) compareToGolden(*s, golden()["spread"]);
}
TEST(richsync) {
    auto body = *json::parse(readFile("mxm_richsync.json"));
    auto l    = parseRichsync(body["message"]["body"]["richsync"]["richsync_body"].asString(), 200000);
    CHECK(l.has_value());
    if (!l) return;
    CHECK(l->wordSynced);
    CHECK_EQ(l->source, std::string("Musixmatch"));
    CHECK_EQ(l->lines.size(), size_t(3));
    CHECK_EQ(l->lines[1].words.size(), size_t(5)); // "Walk"+"ing" join into one word
    CHECK_EQ(l->lines[1].words[0].text, std::string("Walking"));
    CHECK_EQ(l->lines[0].words[2].text, std::string("the")); // "the " ends a word on its trailing space
    CHECK_EQ(l->lines[0].words[0].start, int64_t(12340));
    CHECK_EQ(l->lines[0].words[0].end, int64_t(12790)); // next word's start
    CHECK_EQ(l->lines[0].words.back().end, int64_t(15200)); // line te
    CHECK_EQ(l->lines[0].wordAt(13300), 2);
}
TEST(hash_and_easing_match_java) {
    for (auto const& h : golden()["hash01"].items()) CHECK_NEAR(anim::hash01(h[0].asInt()), h[1].asNumber(), 1e-6);
    for (auto const& e : golden()["ease"].items()) {
        float t = float(e[0].asNumber());
        CHECK_NEAR(anim::easeOutBack(t), e[1].asNumber(), 1e-5);
        CHECK_NEAR(anim::easeInOut(t), e[2].asNumber(), 1e-5);
        CHECK_NEAR(anim::easeOutCubic(t), e[3].asNumber(), 1e-5);
    }
}
TEST(lyrics_queries) {
    auto l = *parseLrc(readFile("song.lrc"), 200000);
    CHECK(l.lineAt(0) == nullptr);
    CHECK(l.lineAt(13000) != nullptr && l.lineAt(13000)->text == "Hello from the test song");
    auto sc = scaled(l, 0.8, 160000);
    CHECK_EQ(sc.lines[0].start, int64_t(9872));
    CHECK(text::endsWith(sc.source, "(stretched)"));
    CHECK(sameScript("Test Artist", "Test Song", l));
    auto y = *parseYrc(readFile("netease_yrc.txt"), 200000);
    CHECK(sameScript("Test Artist", "Test Song", y)); // 1 CJK line out of 4 is fine
}

// ================================================================ search (no network: fixtures)
TEST(lrclib_bestof_picks_synced_in_tolerance) {
    fetch::HttpResponse r{200, readFile("lrclib_search.json")};
    auto l = fetch::lrclibBestOf(r, 200000, {"Test Artist", "Test Song"}, 3.0);
    CHECK(l.has_value() && l->synced && l->lines.size() == 7); // id 13 (synced, 0.5 s off), not the plain 201 s one
    auto none = fetch::lrclibBestOf(r, 200000, {"Nobody", "Unrelated"}, 3.0);
    CHECK(!none.has_value());
}
TEST(fetch_prefers_musixmatch_word_sync) {
    fetch::resetMusixmatchToken();
    FakeHttp h;
    h.routes = {{"token.get", "mxm_token.json"},   {"matcher.track.get", "mxm_matcher.json"}, {"track.richsync.get", "mxm_richsync.json"},
                {"lrclib.net/api/get", "lrclib_get.json"}, {"lrclib.net/api/search", "lrclib_search.json"},
                {"song/lyric/v1", "netease_yrc.json"}, {"song/lyric", "netease_lyric.json"}, {"api/search/get", "netease_search.json"}};
    auto l = fetch::fetch(h.fn(), "Test Artist", "Test Song", "", 200000, {false, 15000});
    CHECK(l.has_value());
    if (l) {
        CHECK_EQ(l->source, std::string("Musixmatch"));
        CHECK(l->wordSynced);
    }
}
TEST(fetch_falls_back_to_netease_yrc) {
    fetch::resetMusixmatchToken();
    FakeHttp h;
    h.routes = {{"token.get", "mxm_token_upgrade.json"}, {"lrclib.net/api/get", "lrclib_get.json"}, {"lrclib.net/api/search", "lrclib_search.json"},
                {"song/lyric/v1", "netease_yrc.json"}, {"song/lyric", "netease_lyric.json"}, {"api/search/get", "netease_search.json"}};
    auto l = fetch::fetch(h.fn(), "Test Artist", "Test Song", "", 200000, {false, 15000});
    CHECK(l.has_value());
    if (l) {
        CHECK_EQ(l->source, std::string("netease words"));
        CHECK(l->wordSynced);
    }
}
TEST(fetch_falls_back_to_lrclib_line_sync) {
    fetch::resetMusixmatchToken();
    FakeHttp h;
    h.routes = {{"token.get", ""}, {"lrclib.net/api/get", "lrclib_get.json"}, {"lrclib.net/api/search", "lrclib_empty.json"}, {"music.163.com", ""}};
    auto l = fetch::fetch(h.fn(), "Test Artist", "Test Song", "", 200000, {false, 15000});
    CHECK(l.has_value());
    if (l) {
        CHECK_EQ(l->source, std::string("lrclib"));
        CHECK(!l->wordSynced);
    }
}
TEST(fetch_netease_drops_credit_lines) {
    FakeHttp h;
    h.routes = {{"song/lyric", "netease_lyric.json"}, {"api/search/get", "netease_search.json"}};
    auto l = fetch::netease(h.fn(), {"Test Artist", "Test Song"}, 200000, 3.0, fetch::nowMs() + 5000);
    CHECK(l.has_value());
    if (l) {
        CHECK_EQ(l->lines.size(), size_t(4));
        CHECK_EQ(l->lines[0].text, std::string("Hello from the test song"));
    }
    // it picked id 222 (0.4 s off), not 111 (60 s off)
    bool sawId = false;
    for (auto const& u : h.log) sawId |= u.find("id=222") != std::string::npos;
    CHECK(sawId);
}
TEST(fetch_speed_version_stretches) {
    fetch::resetMusixmatchToken();
    FakeHttp h;
    h.routes = {{"lrclib.net/api/get", ""}, {"lrclib.net/api/search", "lrclib_search_speed.json"}};
    auto l = fetch::fetch(h.fn(), "Test Artist", "Test Song (sped up)", "", 160000, {false, 15000});
    CHECK(l.has_value());
    if (l) {
        CHECK(text::endsWith(l->source, "(stretched)"));
        CHECK_EQ(l->lines[0].start, int64_t(9872)); // 12340 * 0.8
    }
}
TEST(url_encoding) {
    CHECK_EQ(fetch::urlEncode("a b&c=é"), std::string("a+b%26c%3D%C3%A9"));
    CHECK_EQ(fetch::query({{"q", "x y"}, {"n", "1"}}), std::string("q=x+y&n=1"));
}

// ================================================================ song clock
TEST(song_clock_keeps_and_resyncs) {
    SongClock   clk;
    Config      cfg;
    int64_t     ms = 1000000;
    MediaSample s;
    s.title = "Test Song"; s.artist = "Test Artist"; s.durationMs = 200000; s.playing = true;
    s.positionMs = 10000; s.rawPositionMs = 10000; s.updatedFt = 1;
    auto ev = clk.poll(&s, 0);
    CHECK(ev.size() == 1 && ev[0] == MediaEvent::Song);
    CHECK_EQ(clk.songSeq(), int64_t(1));
    // 250 ms later Windows reports a stale position: our clock keeps running
    s.positionMs = 10050;
    clk.poll(&s, 250 * ms);
    CHECK_EQ(clk.positionRaw(250 * ms), int64_t(10250));
    // a seek: raw position jumps, updated time changes, seen twice -> trusted
    s.positionMs = 60000; s.rawPositionMs = 60000; s.updatedFt = 2;
    clk.poll(&s, 500 * ms);
    s.positionMs = 60250; s.rawPositionMs = 60000; s.updatedFt = 3;
    clk.poll(&s, 750 * ms);
    CHECK(clk.positionRaw(750 * ms) < 20000); // one agreeing poll isn't enough (Java needs stable >= 2 when not a seek)
    s.positionMs = 60500; s.updatedFt = 4;
    clk.poll(&s, 1000 * ms);
    CHECK_NEAR(double(clk.positionRaw(1000 * ms)), 60500.0, 50.0);
    // offsets
    cfg.syncOffsetMs = 200;
    cfg.songOffsets[clk.track().key()] = -100;
    CHECK_EQ(clk.position(1000 * ms, cfg), clk.positionRaw(1000 * ms) + 100);
    // pause -> event
    s.playing = false;
    ev        = clk.poll(&s, 1250 * ms);
    CHECK(ev.size() == 1 && ev[0] == MediaEvent::Pause);
    // nothing playing for > 2.5 s -> stop
    clk.poll(nullptr, 1300 * ms);
    ev = clk.poll(nullptr, 3900 * ms);
    CHECK(ev.size() == 1 && ev[0] == MediaEvent::Stop);
    CHECK(!clk.hasTrack());
}

// ================================================================ config / presets
TEST(config_roundtrip_and_presets) {
    Config c;
    c.lyricsStyle = LyricsStyle::Wall;
    c.outlineColor = int32_t(0xFF102030);
    c.songOffsets["a"] = 300;
    auto   j = json::parse(toJson(c));
    CHECK(j.has_value());
    Config d;
    CHECK(applyJson(d, *j) > 60);
    CHECK(d.lyricsStyle == LyricsStyle::Wall);
    CHECK_EQ(uint32_t(d.outlineColor), 0xFF102030u);
    CHECK_EQ(d.songOffsets["a"], 300);
    for (char const* p : {"ground_drop", "aurora_cursive", "neon_arena", "wall_gold", "combat_safe"}) {
        gCur = std::string("preset ") + p;
        auto pj = json::parse(readFile(std::string("../../../resources/lyricsflow/presets/") + p + ".json"));
        CHECK(pj.has_value());
        if (!pj) continue;
        Config e;
        CHECK(applyJson(e, (*pj)["settings"]) >= 3);
        CHECK(!(*pj)["name"].asString().empty());
    }
    Theme t = currentTheme(Config{});
    CHECK_EQ(t.main, 0xFFFFE3F6u);
    CHECK_EQ(colLerp(0xFF000000u, 0xFFFFFFFFu, 0.5f), 0xFF7F7F7Fu); // Java truncation
}

// ================================================================ scene placement
namespace {
Lyrics testLyrics() { return *parseLrc(readFile("song.lrc"), 200000); }
CameraState testCam() {
    CameraState cam;
    cam.pos   = {0.5, 65.62, 0.5};
    cam.yaw   = 0;  // looking +Z
    cam.pitch = 0;
    return cam;
}
} // namespace

TEST(scene_floating_places_words_in_front) {
    FakeWorld  w;
    Lyrics     l = testLyrics();
    Scene      sc;
    Config     c; // FLOATING, randomPosition on
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
    auto cam = testCam();
    auto wfn = [](std::string_view s, std::string_view) { return 0.5f * float(text::length(s)); };
    in.t     = 12400;
    sc.tick(in, c, cam, &w, wfn);
    CHECK(!sc.visible().empty()); // "Hello" appeared
    for (auto const& p : sc.visible()) {
        Vec3 d = p.a - cam.pos;
        CHECK(d.z > 1.0);               // in front
        CHECK(d.length() < 12.0);       // FLOATING distance range
        CHECK(p.a.y > 64.0);            // not inside the ground
    }
    // later: more words of the line appear, one chunk per word
    in.t = 14500;
    for (int i = 0; i < 60; ++i) sc.tick(in, c, cam, &w, wfn);
    CHECK(sc.visible().size() >= 4);
    auto words = sc.draw(in, c, cam, &w);
    CHECK(!words.empty());
    // much later every word of that line has exited and been removed
    in.t = 40000;
    for (int i = 0; i < 5; ++i) sc.tick(in, c, cam, &w, wfn);
    for (auto const& p : sc.visible()) CHECK(p.line.index != 0 || p.exitAt >= 0);
}
TEST(scene_wall_sticks_to_wall_facing_camera) {
    FakeWorld w;
    for (int x = -6; x <= 6; ++x)
        for (int y = 64; y <= 70; ++y) w.extra[{x, y, 6}] = Aabb{}; // wall 5.5 blocks ahead, face toward -Z
    Lyrics     l = testLyrics();
    Scene      sc;
    Config     c;
    c.lyricsStyle = LyricsStyle::Wall;
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true; in.t = 12400;
    auto cam = testCam();
    sc.tick(in, c, cam, &w, [](std::string_view s, std::string_view) { return 0.5f * float(text::length(s)); });
    CHECK_EQ(sc.visible().size(), size_t(1)); // WALL = whole line in one placement
    if (sc.visible().empty()) return;
    auto const& p = sc.visible()[0];
    CHECK(p.surf.has_value() && p.surfN.has_value());
    if (p.surfN) CHECK_NEAR(p.surfN->z, -1.0, 1e-9);
    CHECK_NEAR(p.a.z, 6.0 - 0.04, 0.02); // 0.04 in front of the wall face
    if (p.surf) { // the text's normal (+Z local after facing) points back at the camera
        Vec3 n = p.surf->rotate({0, 0, 1});
        CHECK(n.z < -0.99);
    }
}
TEST(scene_sky_is_above_horizon) {
    Lyrics     l = testLyrics();
    Scene      sc;
    Config     c;
    c.lyricsStyle = LyricsStyle::Sky;
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true; in.t = 12400;
    auto cam = testCam();
    sc.tick(in, c, cam, nullptr, [](std::string_view s, std::string_view) { return 0.5f * float(text::length(s)); });
    CHECK(!sc.visible().empty());
    for (auto const& p : sc.visible()) {
        CHECK(p.pitch < 0); // negative pitch = looking up
        CHECK(p.a.y > cam.pos.y + 5);
    }
}
TEST(scene_combat_safe_avoids_crosshair) {
    FakeWorld  w;
    Lyrics     l = testLyrics();
    Config     c;
    c.combatSafe = true;
    auto cam = testCam();
    for (int64_t seq = 1; seq <= 6; ++seq) {
        Scene      sc;
        SceneInput in;
        in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = seq; in.playing = true; in.t = 12400;
        sc.tick(in, c, cam, &w, [](std::string_view s, std::string_view) { return 0.5f * float(text::length(s)); });
        for (auto const& p : sc.visible()) {
            Vec3 d = p.a - cam.pos;
            CHECK(std::fabs(wrap(float(yawOf(d)) - cam.yaw)) >= c.keepOutDeg - 0.01f || std::fabs(float(pitchOf(d)) - cam.pitch) >= c.keepOutDeg * 0.75f);
        }
    }
}
TEST(word_anim_enter_exit) {
    Placed p;
    p.line = testLyrics().lines[0];
    p.w0 = 0; p.w1 = 1;
    Config c;
    Word const& w = p.line.words[0];
    CHECK(!wordAnim(p, 0, w, w.start - 100, c, 1.0f).has_value());
    auto mid = wordAnim(p, 0, w, w.start + 100, c, 1.0f);
    CHECK(mid.has_value() && (*mid)[0] > 0.3f);
    auto full = wordAnim(p, 0, w, w.start + 400, c, 1.0f);
    CHECK(full.has_value() && (*full)[0] > 0.99f);
    p.exitAt = w.start + 500;
    CHECK(!wordAnim(p, 0, w, p.exitAt + 700, c, 1.0f).has_value()); // gone after 650 ms exit
}

// ================================================================ glyphs / engine / export
TEST(glyph_builder_neon_passes) {
    FontSet fs;
    CHECK(fs.loadFromJson("lyricsflow_script", readFile("font_test.json"), "x.png"));
    Font const& f = *fs.get("lyricsflow_script");
    CHECK_NEAR(f.width("ABC"), 1.8, 1e-5);
    Config c; // NEON, soft shadow, two-colour on
    DrawWord dw;
    dw.text = "ABC"; dw.font = "lyricsflow_script"; dw.origin = {0, 70, 5}; dw.orient = Quat::rotationYXZ(kPi, 0, 0); dw.scale = 0.5f; dw.alpha = 1; dw.glow = 0.65f;
    std::vector<BfGlyphV1> out;
    buildWordGlyphs(dw, f, c, makeStyle(c, 0), {0, 70, 0}, out);
    CHECK_EQ(out.size(), size_t(3 * 4)); // shadow + 2 offset passes + main
    // main pass is last and carries outline/glow params; its centre is near the word origin
    auto const& last = out.back();
    CHECK_NEAR(last.pos[2], 5.0, 0.05);
    CHECK(last.glowK > 0); // glow on by default
    CHECK_EQ(last.fill, toRgba(0xFFFFE3F6u));
    // quad height = (top - bottom) * scale
    double hh = std::sqrt(double(last.up[0]) * last.up[0] + double(last.up[1]) * last.up[1] + double(last.up[2]) * last.up[2]);
    CHECK_NEAR(hh * 2, (0.72 + 0.02) * 0.5, 1e-4);
    c.textStyle = TextStyle::Clean; c.textShadow = 0;
    out.clear();
    buildWordGlyphs(dw, f, c, makeStyle(c, 0), {0, 70, 0}, out);
    CHECK_EQ(out.size(), size_t(3));
}
TEST(engine_frame_and_export) {
    Engine e;
    CHECK(e.fonts().loadFromJson("lyricsflow_script", readFile("font_test.json"), "x.png"));
    FrameStore::get().setAtlases({"x.png"});
    FakeWorld  w;
    Lyrics     l = testLyrics();
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
    auto cam = testCam();
    for (int i = 0; i < 40; ++i) {
        in.t = 12400 + i * 50;
        e.frame(in, cam, &w);
    }
    CHECK(!e.glyphs().empty());
    e.publish(true, 70.0f);
    BfLyricFrameV1         hdr{};
    std::vector<BfGlyphV1> buf(4096);
    uint32_t               n = BF_CopyLyricFrame(&hdr, buf.data(), uint32_t(buf.size()));
    CHECK_EQ(n, uint32_t(e.glyphs().size()));
    CHECK_EQ(hdr.version, 1u);
    CHECK_EQ(hdr.active, 1u);
    CHECK_EQ(hdr.atlasCount, 1u);
    CHECK_NEAR(hdr.origin[1], cam.pos.y, 1e-9);
}

// ================================================================ falling letters
namespace {
std::vector<BfGlyphV1> letterQuads(Vec3 at, int n) {
    std::vector<BfGlyphV1> q(static_cast<size_t>(n), BfGlyphV1{});
    for (int i = 0; i < n; ++i) {
        auto& g   = q[size_t(i)];
        g.pos[0]  = float(at.x + i * 0.3); g.pos[1] = float(at.y); g.pos[2] = float(at.z);
        g.right[0] = -0.1f; g.up[1] = 0.15f; // upright, facing -Z (like text facing a camera at -Z)
        g.alpha   = 1;
    }
    return q;
}
} // namespace

TEST(letters_fall_and_lie_flat_on_ground) {
    FakeWorld     w;
    LetterPhysics ph;
    PhysicsParams prm;
    ph.spawn(letterQuads({0.5, 68.0, 3.5}, 5), {0, 0, 0}, 7, prm);
    CHECK_EQ(ph.letters().size(), size_t(5));
    for (int i = 0; i < 240; ++i) ph.step(1.0f / 60.0f, &w, prm);
    for (auto const& l : ph.letters()) {
        CHECK(l.state == Letter::State::Resting);
        CHECK_NEAR(l.pos.y, 64.0, 0.02);           // on top of the ground block
        Vec3 n = l.orient.rotate({0, 0, 1});
        CHECK(n.y > 0.99);                           // lying flat, face up
    }
}
TEST(letters_land_on_slab_and_steps) {
    FakeWorld w;
    w.extra[{0, 64, 3}] = Aabb{0, 0, 0, 1, 0.5, 1}; // bottom slab
    w.extra[{2, 64, 3}] = Aabb{};                     // full block step
    LetterPhysics ph;
    PhysicsParams prm;
    prm.bounce = 0;
    std::vector<BfGlyphV1> q = letterQuads({0.5, 67.0, 3.5}, 1), q2 = letterQuads({2.5, 67.0, 3.5}, 1);
    q.insert(q.end(), q2.begin(), q2.end());
    ph.spawn(q, {0, 0, 0}, 3, prm);
    for (int i = 0; i < 240; ++i) ph.step(1.0f / 60.0f, &w, prm);
    CHECK_EQ(ph.letters().size(), size_t(2));
    if (ph.letters().size() == 2) {
        double ys[2] = {ph.letters()[0].pos.y, ph.letters()[1].pos.y};
        // one on the slab (64.5), one on the step (65); x drifts a little, so check the set
        bool slab = std::fabs(ys[0] - 64.5) < 0.03 || std::fabs(ys[1] - 64.5) < 0.03;
        bool step = std::fabs(ys[0] - 65.0) < 0.03 || std::fabs(ys[1] - 65.0) < 0.03;
        CHECK(slab);
        CHECK(step);
    }
}
TEST(letters_tumble_mode_settles) {
    FakeWorld     w;
    LetterPhysics ph;
    PhysicsParams prm;
    prm.land = LetterLand::Tumble;
    ph.spawn(letterQuads({0.5, 70.0, 3.5}, 3), {0, 0, 0}, 11, prm);
    for (int i = 0; i < 600; ++i) ph.step(1.0f / 60.0f, &w, prm);
    for (auto const& l : ph.letters()) {
        CHECK(l.state == Letter::State::Resting);
        CHECK(l.pos.y > 63.9 && l.pos.y < 64.3);
    }
}
TEST(letters_fade_out_and_cap) {
    FakeWorld     w;
    LetterPhysics ph;
    PhysicsParams prm;
    prm.lifetimeMs = 500; prm.fadeMs = 300; prm.maxLetters = 4;
    ph.spawn(letterQuads({0.5, 64.5, 3.5}, 6), {0, 0, 0}, 1, prm);
    CHECK_EQ(ph.letters().size(), size_t(4)); // capped, oldest dropped
    for (int i = 0; i < 120; ++i) ph.step(1.0f / 60.0f, &w, prm);
    CHECK(ph.letters().empty());
}
TEST(falling_letters_through_engine) {
    Engine e;
    e.fonts().loadFromJson("lyricsflow_script", readFile("font_test.json"), "x.png");
    e.config().fallingLetters = true;
    FakeWorld  w;
    Lyrics     l = testLyrics();
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
    auto cam = testCam();
    size_t maxLetters = 0;
    for (int i = 0; i < 400; ++i) { // 12.4 s -> 32.4 s of song at 50 ms per frame
        in.t  = 12400 + i * 50;
        in.dt = 0.05f;
        e.frame(in, cam, &w);
        maxLetters = std::max(maxLetters, e.physics().letters().size());
    }
    CHECK(maxLetters > 5);
    bool anyResting = false;
    for (auto const& lt : e.physics().letters()) anyResting |= lt.state == Letter::State::Resting;
    CHECK(anyResting);
}

int main(int argc, char** argv) {
    if (argc > 1) gDir = argv[1];
    else if (char const* e = std::getenv("LYRICS_FIXTURES")) gDir = e;
    for (auto& [name, fn] : registry()) {
        gCur       = name;
        int before = gFails;
        fn();
        std::printf("%s %s\n", gFails == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("\n%d checks, %d failed\n", gChecks, gFails);
    return gFails ? 1 : 0;
}
