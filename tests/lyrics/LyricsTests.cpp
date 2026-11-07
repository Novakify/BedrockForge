// Unit tests for the LyricsFlow port core (src/lyrics/core). No game, no network: saved sample responses in
// fixtures/, and golden outputs produced by running LyricsFlow v2.0's own Java parsers (fixtures/golden_java.json,
// see tests/lyrics/README.md). Run: LyricsCoreTests [fixtures dir]
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Engine.h"
#include "lyrics/core/Panel.h"
#include "lyrics/core/Fetcher.h"
#include "lyrics/core/Glyphs.h"
#include "lyrics/core/Json.h"
#include "lyrics/core/Lyrics.h"
#include "lyrics/core/Media.h"
#include "lyrics/core/Beat.h"
#include "lyrics/core/Bands.h"
#include "lyrics/core/SongMemory.h"
#include "lyrics/core/Palette.h"
#include "lyrics/core/Physics.h"
#include "lyrics/core/Scene.h"
#include "lyrics/core/Text.h"
#include "lyrics/core/TopBar.h"
#include "lyrics/core/Sky.h"
#include "lyrics/core/LetterFx.h"
#include "lyrics/core/SkyFx.h"
#include "lyrics/core/SkySystem.h"
#include "lyrics/core/SkyMood.h"
#include "lyrics/core/Conductor.h"
#include "lyrics/core/Cinema.h"
#include "lyrics/core/Overrides.h"
#include "lyrics/core/Atmos.h"
#include "lyrics/core/Bursts.h"
#include "lyrics/core/Ribbon.h"
#include "lyrics/core/PlayerHud.h"
#include "lyrics/core/Portal.h"
#include "lyrics/core/Translate.h"
#include <set>
#include <filesystem>
#include "lyrics/core/SkyWriter.h"
#include "lyrics/core/Studio.h"
#include "lyrics/core/Clip.h"
#include "lyrics/core/Pulse.h"
#include "lyrics/core/Moments.h"
#include "lyrics/core/TempoGuess.h"

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
TEST(cinema_grade_flash_letterbox) {
    Config cfg; cfg.director = true; cfg.reactivity = 2; cfg.scene = "blood_moon";
    Cinema c;
    MusicState m; m.active = true; m.budget = 0.6f;
    c.step(&m, cfg, 1.0f / 60);
    for (int i = 0; i < 300; ++i) c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().active);
    CHECK(c.out().amount > 0.3f && c.out().tint[0] > 1.05f);      // red grade in
    CHECK(c.out().flash == 0.0f);
    m.dropNow = true; c.step(&m, cfg, 1.0f / 60); m.dropNow = false;
    CHECK(c.out().flash > 0.05f && c.out().flash <= Cinema::kDropFlashCap + 1e-6f); // [LyricsForge] a soft swell, capped low
    CHECK(c.out().bloomBoost > 0.0f && c.out().caBoost > 0.0f);
    m.chorusNow = true; c.step(&m, cfg, 1.0f / 60); m.chorusNow = false; // too soon: no second flash
    float f1 = c.out().flash;
    for (int i = 0; i < 30; ++i) c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().flash < f1 * 0.45f);                             // fades (softly: 0.45 s)
    m.build = 1.0f;
    for (int i = 0; i < 400; ++i) c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().letterbox > 0.05f && c.out().letterbox <= Cinema::kLetterMax + 1e-6f);
    cfg.letterbox = false;
    for (int i = 0; i < 400; ++i) c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().letterbox < 0.005f);
    // scene switch crossfades instead of snapping
    cfg.scene = "liquid_paradise"; c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().tint[0] > 1.0f);
    for (int i = 0; i < 200; ++i) c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().tint[0] < 0.9f);
    // music stops -> the look fades out
    m.active = false;
    for (int i = 0; i < 600; ++i) c.step(&m, cfg, 1.0f / 60);
    CHECK(c.out().amount < 0.01f && !c.out().active);
}

TEST(lyric_overrides_roundtrip) {
    CHECK_EQ(overrideFileName("AC/DC", "Back: In Black?"), std::string("AC_DC - Back_ In Black_.lrc"));
    CHECK_EQ(overrideFileName("", ""), std::string("unknown.lrc"));
    auto l = parseLrc(readFile("enhanced.lrc"), 30000);
    CHECK(l.has_value());
    std::string const txt = toOverrideLrc(*l, "Artist", "Song");
    CHECK(txt.find("<00:10.50>pixel") != std::string::npos);
    auto back = parseOverride("\xEF\xBB\xBF" + txt, 30000);
    CHECK(back.has_value());
    if (back && l) {
        CHECK_EQ(back->source, std::string("override"));
        CHECK_EQ(back->lines.size(), l->lines.size());
        for (size_t i = 0; i < back->lines.size() && i < l->lines.size(); ++i) {
            CHECK_EQ(back->lines[i].text, l->lines[i].text);
            CHECK(std::llabs(back->lines[i].start - l->lines[i].start) <= 10);
        }
    }
    CHECK(parseOverride(emptyOverrideLrc("a", "b"), 30000).has_value());
    { // tap-sync: tapped lines move to the taps, the rest follow (linear in between)
        auto base = parseOverride("[00:10.00]one\n[00:20.00]two\n[00:30.00]three\n[00:40.00]four\n[00:50.00]five\n", 60000);
        CHECK(base.has_value());
        if (base) {
            std::vector<int> idx;
            for (auto const& ln : base->lines) if (ln.index >= 0) idx.push_back(ln.index);
            auto r = retimeLines(*base, {{idx[0], 11000}, {idx[2], 33000}});
            std::vector<int64_t> st;
            for (auto const& ln : r.lines) if (ln.index >= 0) st.push_back(ln.start);
            CHECK_EQ(st[0], int64_t(11000));
            CHECK_EQ(st[1], int64_t(22000));  // halfway between +1 s and +3 s
            CHECK_EQ(st[2], int64_t(33000));
            CHECK_EQ(st[4], int64_t(53000));  // after the last tap: its shift
        }
    }
    // duets + translations
    auto d = parseOverride("[00:01.00]Hello there\n[00:01.00]tr: Hola\n[00:04.00]v2: <00:04.00>I <00:04.50>answer\n[00:08.00]End\n", 12000);
    CHECK(d.has_value());
    if (d) {
        std::vector<Line const*> real;
        for (auto const& ln : d->lines) if (ln.index >= 0) real.push_back(&ln);
        CHECK_EQ(real.size(), size_t(3));
        if (real.size() == 3) {
            CHECK_EQ(real[0]->translation, std::string("Hola"));
            CHECK_EQ(int(real[1]->voice), 1);
            CHECK_EQ(real[1]->text, std::string("I answer"));
            CHECK_EQ(real[1]->words.front().text, std::string("I"));
            CHECK_EQ(int(real[2]->voice), 0);
            std::string const again = toOverrideLrc(*d, "a", "b");
            CHECK(again.find("tr: Hola") != std::string::npos && again.find("v2: ") != std::string::npos);
        }
    }
}

TEST(atmos_motes_follow_music) {
    Atmos at;
    MusicState m; m.active = true; m.treble = 0.8f; m.budget = 0.8f; m.energy = 0.6f;
    Atmos::In in; in.music = &m; in.camPos = {100, 70, 100};
    for (int i = 0; i < 120; ++i) { in.now += 1.0 / 60; at.step(in); }
    CHECK_EQ(at.size(), size_t(Atmos::kCount));
    std::vector<BfGlyphV1> out;
    at.emit(in, in.camPos, out);
    CHECK(!out.empty());
    for (auto const& q : out) CHECK(q.flags == BF_GLYPH_SOLID && q.alpha > 0 && q.alpha <= 0.9f);
    in.camPos = {400, 70, 400}; // walk far away: motes come along
    for (int i = 0; i < 10; ++i) at.step(in);
    out.clear(); at.emit(in, in.camPos, out);
    for (auto const& q : out) CHECK(std::sqrt(q.pos[0] * q.pos[0] + q.pos[1] * q.pos[1] + q.pos[2] * q.pos[2]) < Atmos::kRadius + 2);
    m.active = false;
    for (int i = 0; i < 900; ++i) at.step(in);
    CHECK_EQ(at.size(), size_t(0));
}

TEST(material_impacts) {
    CHECK(surfaceOf("minecraft:grass_block") == Surface::Grass);
    CHECK(surfaceOf("minecraft:stone") == Surface::Stone);
    CHECK(surfaceOf("minecraft:sandstone") == Surface::Stone);
    CHECK(surfaceOf("minecraft:sand") == Surface::Sand);
    CHECK(surfaceOf("minecraft:oak_planks") == Surface::Wood);
    CHECK(surfaceOf("minecraft:flowing_water") == Surface::Water);
    CHECK(surfaceOf("minecraft:snow_layer") == Surface::Snow);
    CHECK(surfaceOf("minecraft:iron_block") == Surface::Metal);
    CHECK(surfaceOf("minecraft:oak_leaves") == Surface::Leaves);
    CHECK(surfaceOf("") == Surface::Generic);
    Bursts b;
    Landing l; l.pos = {0, 64, 0}; l.surface = Surface::Lava; l.strength = 1; l.size = 0.25f;
    b.spawn(l, 1);
    CHECK(b.size() >= 4);
    std::vector<BfGlyphV1> out;
    b.emit(Vec3{0, 65, -3}, 0, 10, out);
    CHECK_EQ(out.size(), b.size());
    for (int i = 0; i < 40; ++i) b.step(0.05f);
    CHECK_EQ(b.size(), size_t(0));
}

TEST(word_ribbon_runs_with_the_singing) {
    Ribbon rb;
    DrawWord w; w.text = "hello"; w.font = "f"; w.origin = {0, 70, 5}; w.scale = 0.5f; w.alpha = 1; w.sung = true; w.durMs = 1000; w.key = 3000;
    auto width = [](std::string_view, std::string_view) { return 2.0f; };
    double t = 10;
    for (int i = 0; i <= 10; ++i) { w.sungMs = i * 100; rb.step({w}, width, Vec3{0, 70, 0}, t += 0.1, 0.5f, true); }
    CHECK(rb.points() >= 8);
    std::vector<BfGlyphV1> out;
    rb.emit(0xFFFF00FF, 0.8f, out);
    CHECK(out.size() >= 8);
    // the head walked from the left end of the word to the right end, under it
    Vec3 a = wordPoint(w, -1.0, -0.62, Vec3{0, 70, 0}), b = wordPoint(w, 1.0, -0.62, Vec3{0, 70, 0});
    CHECK(b.x - a.x > 0.9 && b.y < 0.0);
    for (int i = 0; i < 20; ++i) rb.step({}, width, Vec3{0, 70, 0}, t += 0.1, 0.5f, true);
    CHECK_EQ(rb.points(), size_t(0));
}

TEST(corner_player_hud) {
    FontSet fonts;
    fonts.loadFromJson("outfit", readFile("font_test.json"), "x.png");
    fonts.loadFromJson("outfit_bold", readFile("font_test.json"), "x.png");
    fonts.setAtlasIndex("outfit", 0); fonts.setAtlasIndex("outfit_bold", 0);
    Config c; c.hudPlayer = true;
    Lyrics l = *parseLrc(readFile("song.lrc"), 200000);
    SceneInput in; in.title = "Song"; in.artist = "Artist"; in.t = 1500; in.playing = true; in.lyrics = &l; in.guiW = 480; in.guiH = 270; in.songSeq = 1;
    PlayerHud h; PlayerHud::Extra ex; ex.durationMs = 200000;
    std::vector<BfGlyphV1> out;
    for (int i = 0; i < 30; ++i) { out.clear(); h.build(in, ex, c, fonts, out); }
    CHECK(h.visible() > 0.9f);
    CHECK(out.size() > 20);
    for (auto const& q : out) CHECK((q.flags & BF_GLYPH_SCREEN) != 0);
    c.hudPlayer = false;
    for (int i = 0; i < 60; ++i) { out.clear(); h.build(in, ex, c, fonts, out); }
    CHECK(out.empty());
}

TEST(manual_sections_and_translations) {
    auto l = parseOverride("[00:01.00]one\n[00:05.00]two\n[00:09.00]three\n[00:04.00]@build\n[00:08.00]@DROP\n[00:12.00]@chorus\n", 20000);
    CHECK(l.has_value());
    if (!l) return;
    CHECK_EQ(l->marks.size(), size_t(3));
    std::string const txt = toOverrideLrc(*l, "a", "b");
    CHECK(txt.find("@drop") != std::string::npos && txt.find("@build") != std::string::npos);
    auto back = fromCacheJson(toCacheJson(*l));
    CHECK(back && back->marks.size() == 3);
    // the Conductor follows the marks: build ramps from 4 s to the drop at 8 s, the drop fires exactly there
    Conductor cd;
    ConductorIn in; in.playing = true; in.songSeq = 1; in.dt = 0.05f; in.marks = &l->marks; in.bandsLive = true;
    bool dropAt8 = false; float buildAt7 = 0;
    for (int64_t t = 0; t <= 10000; t += 50) {
        in.posMs = t;
        cd.update(in);
        if (cd.state().dropNow) dropAt8 = t >= 8000 && t <= 8050;
        if (t == 7500) buildAt7 = cd.state().build;
    }
    CHECK(dropAt8);
    CHECK(buildAt7 > 0.4f);
    // NetEase translations by time
    Lyrics x = *parseLrc("[00:01.00]hello\n[00:03.00]world\n[00:05.00]again\n", 8000);
    CHECK_EQ(attachTranslations(x, "[00:01.00]hola\n[00:03.05]mundo\n"), 2);
    int n = 0;
    for (auto const& ln : x.lines) if (!ln.translation.empty()) ++n;
    CHECK_EQ(n, 2);
}

TEST(portal_opens_on_chorus) {
    Portal p;
    MusicState m; m.active = true;
    Portal::In in; in.music = &m;
    for (int i = 0; i < 10; ++i) p.step(in);
    CHECK(!p.open());
    m.chorusNow = true; in.now = 5; p.step(in); m.chorusNow = false;
    CHECK(p.open());
    for (int i = 0; i < 40; ++i) { in.now += 1.0 / 60; p.step(in); }
    std::vector<BfGlyphV1> out;
    p.emit(in, out);
    CHECK(out.size() > 100);
    for (int i = 0; i < 300; ++i) { in.now += 1.0 / 60; p.step(in); }
    CHECK(!p.open());
    m.dropNow = true; p.step(in); m.dropNow = false; // too soon after the last one
    CHECK(!p.open());
}

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
    CHECK(sourceMatches(Source::AppleMusic, "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App"));
    CHECK(!sourceMatches(Source::AppleMusic, "Spotify.exe"));
    CHECK(sourceMatches(Source::Any, "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App"));
    {
        MediaSample am;
        am.title = "Hate Me - Remastered"; am.artist = "Ellie Goulding \xE2\x80\x94 Hate Me (feat. Juice WRLD)";
        cleanAppleMusic(am);
        CHECK_EQ(am.artist, std::string("Ellie Goulding"));
        CHECK_EQ(am.album, std::string("Hate Me (feat. Juice WRLD)"));
        CHECK_EQ(am.title, std::string("Hate Me - Remastered")); // no YouTube split
        MediaSample plain;
        plain.artist = "Rihanna"; plain.album = "Talk That Talk";
        cleanAppleMusic(plain);
        CHECK_EQ(plain.artist, std::string("Rihanna"));
        CHECK_EQ(plain.album, std::string("Talk That Talk"));
        Config cd;
        CHECK(cd.source == Source::Any);
    }
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
TEST(lyrics_cache_roundtrip) {
    for (char const* which : {"lrc", "yrc"}) {
        gCur = std::string("cache ") + which;
        auto l = std::string(which) == "lrc" ? *parseLrc(readFile("song.lrc"), 200000) : *parseYrc(readFile("netease_yrc.txt"), 200000);
        auto r = fromCacheJson(toCacheJson(l));
        CHECK(r.has_value());
        if (!r) continue;
        CHECK_EQ(r->lines.size(), l.lines.size());
        CHECK_EQ(r->wordSynced, l.wordSynced);
        CHECK_EQ(r->source, l.source);
        for (size_t i = 0; i < l.lines.size(); ++i) {
            CHECK_EQ(r->lines[i].text, l.lines[i].text);
            CHECK_EQ(r->lines[i].end, l.lines[i].end);
            CHECK_EQ(r->lines[i].words.size(), l.lines[i].words.size());
            if (!l.lines[i].words.empty()) CHECK_EQ(r->lines[i].words.back().end, l.lines[i].words.back().origEnd);
        }
    }
    CHECK_EQ(cacheFileName("a").size(), size_t(21));
    CHECK(cacheFileName("x") != cacheFileName("y"));
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
TEST(lrclib_rejects_other_artist_short_title) { // [BF] in game: the instrumental "lit(var)" showed a rap song's lyrics
    fetch::HttpResponse r{200, R"([{"id":1,"trackName":"Lit","artistName":"Some Rapper","duration":121,"instrumental":false,
        "syncedLyrics":"[00:11.52] Pull up to the party\n[00:15.86] Looking like an army\n"}])"};
    CHECK(!fetch::lrclibBestOf(r, 121000, {"kensuke ushio", "lit"}, 3.0).has_value());
    CHECK(!fetch::lrclibBestByTitle(r, 121000, {"kensuke ushio", "lit"}, 3.0).has_value());
    CHECK(fetch::lrclibBestOf(r, 121000, {"Some Rapper", "Lit"}, 3.0).has_value()); // the right artist still matches
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
TEST(glyph_builder_italic) { // [BF] italic slant: quads become parallelograms leaning right, top bar text stays upright
    FontSet fs;
    CHECK(fs.loadFromJson("lyricsflow_script", readFile("font_test.json"), "x.png"));
    Font const& f = *fs.get("lyricsflow_script");
    Config c;
    c.textStyle = TextStyle::Clean; c.textShadow = 0;
    DrawWord dw;
    dw.text = "A"; dw.font = "lyricsflow_script"; dw.origin = {0, 70, 5}; dw.orient = Quat{}; dw.scale = 1.0f; dw.alpha = 1;
    std::vector<BfGlyphV1> up, it;
    buildWordGlyphs(dw, f, c, makeStyle(c, 0), {0, 70, 0}, up);
    c.italic = 0.25f;
    buildWordGlyphs(dw, f, c, makeStyle(c, 0), {0, 70, 0}, it);
    CHECK_EQ(it.size(), size_t(1));
    CHECK_NEAR(up[0].up[0], 0.0, 1e-6);                         // upright: up has no x part
    CHECK_NEAR(it[0].up[0], 0.25 * double(it[0].up[1]), 1e-5);  // slanted: x leans by italic * height
    CHECK_NEAR(it[0].up[1], up[0].up[1], 1e-6);                 // same height
    dw.plain = true; // top bar text
    it.clear();
    buildWordGlyphs(dw, f, c, makeStyle(c, 0), {0, 70, 0}, it);
    CHECK_NEAR(it[0].up[0], 0.0, 1e-6);
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
    // legacy caller (size 0 = an RF from the 8-atlas days): BF must not write past atlasFiles[7]
    std::vector<unsigned char> raw(sizeof(BfLyricFrameV1), 0xAB);
    auto* old = reinterpret_cast<BfLyricFrameV1*>(raw.data());
    old->size = 0;
    BF_CopyLyricFrame(old, nullptr, 0);
    size_t const legacyEnd = offsetof(BfLyricFrameV1, atlasFiles) + 8 * sizeof(old->atlasFiles[0]);
    bool untouched = true;
    for (size_t i = legacyEnd; i < raw.size(); ++i) untouched &= raw[i] == 0xAB;
    CHECK(untouched);
    CHECK(old->atlasCount <= 8u);
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
    CHECK(!ph.takeLandings().empty());              // [BF] each landing reported once for the impact bits
    CHECK(ph.takeLandings().empty());
}
TEST(letters_lie_readable_in_order) { // reference video: the line lies on the ground, top away from you, in order
    FakeWorld     w;
    LetterPhysics ph;
    PhysicsParams prm;
    prm.spin = 0.3f;
    ph.spawn(letterQuads({0.5, 65.5, 3.5}, 5), {0, 0, 0}, 9, prm);
    ph.step(1.0f / 60.0f, &w, prm);
    int waiting = 0;
    for (auto const& l : ph.letters()) waiting += l.state == Letter::State::Waiting;
    CHECK(waiting >= 3); // they let go one after another
    for (int i = 0; i < 240; ++i) ph.step(1.0f / 60.0f, &w, prm);
    double lastX = -1e9;
    for (auto const& l : ph.letters()) {
        CHECK(l.state == Letter::State::Resting);
        Vec3 up = l.orient.rotate({0, 1, 0});
        CHECK(up.z > 0.9);                    // text top points away from the camera at -Z
        CHECK(std::fabs(l.pos.z - 3.5) < 0.6); // lands about where it hung
        CHECK(l.pos.x > lastX);                // reading order kept
        lastX = l.pos.x;
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


// ---- frame dump for tools/LyricsFonts/render_frame.py (set LYRICS_DUMP_DIR to write <dir>/<name>.json)
namespace {
void dumpFrame(std::string const& name, Engine& e, CameraState const& cam, float fov, float aspect) {
    char const* dir = std::getenv("LYRICS_DUMP_DIR");
    if (!dir) return;
    std::ofstream o(std::string(dir) + "/" + name + ".json");
    o << "{\"yaw\":" << cam.yaw << ",\"pitch\":" << cam.pitch << ",\"fov\":" << fov << ",\"aspect\":" << aspect << ",\"atlases\":[";
    for (size_t i = 0; i < e.fonts().all().size(); ++i) o << (i ? "," : "") << "\"" << e.fonts().all()[i].atlasPath << "\"";
    o << "],\"glyphs\":[";
    bool first = true;
    for (auto const& g : e.glyphs()) {
        o << (first ? "" : ",") << "[" << g.pos[0] << "," << g.pos[1] << "," << g.pos[2] << "," << g.right[0] << "," << g.right[1] << "," << g.right[2]
          << "," << g.up[0] << "," << g.up[1] << "," << g.up[2] << "," << g.uv[0] << "," << g.uv[1] << "," << g.uv[2] << "," << g.uv[3] << ","
          << g.fill << "," << g.fill2 << "," << g.outline << "," << g.glow << "," << g.alpha << "," << g.outlineW << "," << g.glowW << ","
          << g.glowK << "," << g.atlas << "," << g.flags << "," << g.pxRange << "]";
        first = false;
    }
    o << "]}\n";
}
bool loadRealFonts(Engine& e) {
    std::string base = gDir + "/../../../resources/lyricsflow/fonts/";
    bool ok = true;
    for (char const* f : {"lyricsflow_script", "outfit", "greatvibes", "kaushan", "minecraft"})
        ok &= e.fonts().loadFromJson(f, readFile(std::string("../../../resources/lyricsflow/fonts/") + f + ".json"), base + f + ".png");
    return ok;
}
} // namespace

TEST(top_bar_phases_and_karaoke) {
    Engine e;
    CHECK(loadRealFonts(e));
    Lyrics     l = *parseLrc(readFile("song.lrc"), 200000);
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true; in.dt = 0.05f;
    in.title = "Test Song"; in.artist = "Test Artist"; in.guiW = 480; in.guiH = 270;
    auto cam = testCam();
    in.t = 10000;
    e.frame(in, cam, nullptr);
    CHECK(e.topBar().phase() == TopBar::Phase::Title);
    size_t solids = 0;
    for (auto const& g : e.glyphs()) solids += (g.flags & BF_GLYPH_SOLID) ? 1 : 0;
    CHECK(solids >= 7); // shadow, outline, pill, vinyl (4), bars (4)
    for (int i = 0; i < 80; ++i) { in.t = 10000 + i * 50; e.frame(in, cam, nullptr); } // 4 s
    CHECK(e.topBar().phase() == TopBar::Phase::Lyrics);
    for (int i = 0; i < 40; ++i) { in.t = 13900 + i * 25; e.frame(in, cam, nullptr); }
    CHECK(e.topBar().lastWord() >= 2); // "the"/"test" by ~14.9 s
    dumpFrame("topbar_karaoke", e, cam, 70, 480.0f / 270);
    in.playing = false;
    for (int i = 0; i < 5; ++i) e.frame(in, cam, nullptr);
    CHECK(e.topBar().phase() == TopBar::Phase::Paused);
}

TEST(preset_frames_render_dump) { // world text for each look, dumped for the CPU preview renderer
    for (char const* p : {"neon_arena", "aurora_cursive", "wall_gold", "wall_coral", "ground_drop"}) {
        gCur = std::string("preset frame ") + p;
        Engine e;
        CHECK(loadRealFonts(e));
        auto pj = json::parse(readFile(std::string("../../../resources/lyricsflow/presets/") + p + ".json"));
        if (pj) applyJson(e.config(), (*pj)["settings"]);
        FakeWorld w;
        for (int x = -8; x <= 8; ++x)
            for (int y = 64; y <= 72; ++y) w.extra[{x, y, 7}] = Aabb{};
        Lyrics     l = *parseLrc(readFile("song.lrc"), 200000);
        SceneInput in;
        in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true; in.dt = 1.0f / 30;
        in.title = "Test Song"; in.artist = "Test Artist"; in.guiW = 480; in.guiH = 270;
        auto cam = testCam();
        if (std::string(p) == "aurora_cursive") cam.pitch = -20;
        int frames = std::string(p) == "ground_drop" ? 150 : 75;
        for (int i = 0; i < frames; ++i) {
            in.t          = 12300 + int64_t(i) * 33;
            in.nowSeconds = i / 30.0;
            e.frame(in, cam, &w);
        }
        CHECK(!e.glyphs().empty());
        dumpFrame(std::string("preset_") + p, e, cam, 70, 480.0f / 270);
    }
}

// ================================================================ world panel (LyricsFlow 2.1 WorldPanel)
namespace {
struct PanelRig {
    Engine                 e;
    Panel                  p;
    PanelAssets            as;
    PanelMedia             media;
    std::vector<BfGlyphV1> out;
    Vec3                   eye{0.5, 65.62, 0.5};
    int64_t                now = 0;
    PanelRig() {
        loadRealFonts(e);
        media.title = "Song"; media.artist = "Artist"; media.hasTrack = true; media.durationMs = 200000; media.positionMs = 50000;
        p.open(eye, {0, 0, 1}, 0); // looking +Z
        now = 1300;               // landed (aiming starts at 1200 ms)
    }
    PanelInput in(float x, float y, bool click = false, bool down = false) {
        PanelInput i;
        i.eye = eye; i.look = (p.toWorld(x, y) - eye).normalized(); i.nowMs = now; i.click = click; i.down = down || click;
        return i;
    }
    std::vector<PanelAction> step(PanelInput const& i) { out.clear(); now += 16; return p.frame(i, eye, e.config(), e.fonts(), as, media, out); }
    std::vector<PanelAction> click(float x, float y) {
        step(in(x, y));                 // aim (sets the hot widget)
        auto a = step(in(x, y, true));  // press
        auto b = step(in(x, y));        // release
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
    static bool has(std::vector<PanelAction> const& v, PanelAction::Kind k) {
        for (auto const& a : v) if (a.kind == k) return true;
        return false;
    }
};
} // namespace

TEST(panel_placement_and_aim) {
    Panel p;
    p.open({0.5, 65.62, 0.5}, {0, 0, 1}, 0);
    // 3.2 blocks in front, 0.05 below the eye; px (190, 111) is the centre; +x is the player's right (-X when facing +Z)
    Vec3 c = p.toWorld(190, 111);
    CHECK_NEAR(c.z, 0.5 + 3.2, 1e-6);
    CHECK_NEAR(c.y, 65.62 - 0.05, 1e-6);
    CHECK(p.toWorld(300, 111).x < c.x);
    for (float x : {20.0f, 190.0f, 370.0f})
        for (float y : {5.0f, 111.0f, 215.0f}) {
            auto h = p.aimAt({0.5, 65.62, 0.5}, (p.toWorld(x, y) - Vec3{0.5, 65.62, 0.5}).normalized());
            CHECK(h.has_value());
            CHECK_NEAR((*h)[0], x, 1e-3);
            CHECK_NEAR((*h)[1], y, 1e-3);
        }
    CHECK(!p.aimAt({0.5, 65.62, 0.5}, {0, 0, -1}).has_value());              // looking away
    CHECK(!p.aimAt({0.5, 65.62, 0.5}, (p.toWorld(5, 100) - Vec3{0.5, 65.62, 0.5}).normalized()).has_value()); // x < 14
}
TEST(panel_open_animation_and_close) {
    PanelRig r;
    Panel    p;
    std::vector<BfGlyphV1> out;
    p.open(r.eye, {0, 0, 1}, 0);
    PanelInput i;
    i.eye = r.eye; i.look = {0, 0, 1};
    i.nowMs = 300;
    p.frame(i, r.eye, r.e.config(), r.e.fonts(), r.as, r.media, out);
    size_t early = out.size();
    CHECK(early > 0);              // the record card is flying
    CHECK(!p.hovered());           // no aiming before 1200 ms
    out.clear(); i.nowMs = 1700;
    p.frame(i, r.eye, r.e.config(), r.e.fonts(), r.as, r.media, out);
    CHECK(out.size() > early * 3); // panel unfolded with its content
    CHECK(p.hovered());            // looking straight at the panel centre
    bool world = true;
    for (auto const& g : out) world &= (g.flags & (BF_GLYPH_SCREEN | BF_GLYPH_VIEW)) == 0;
    CHECK(world);
    p.close(1800);
    CHECK(p.visible(2600));
    CHECK(!p.visible(2800));
}
TEST(panel_widgets) {
    PanelRig r;
    CHECK(r.p.tab() == Panel::Tab::Home); // [BF] the panel opens on the Cinema home
    r.click(206 + 27.17f + 12, 210);      // Sky tab (2nd pill)
    CHECK(r.p.tab() == Panel::Tab::Sky);
    // Sky page: "Random" toggle (288, 45, 78 x 16) - the skies change by themselves
    bool before = r.e.config().skyShuffle;
    auto a      = r.click(330, 52);
    CHECK_EQ(r.e.config().skyShuffle, !before);
    CHECK(PanelRig::has(a, PanelAction::Kind::SaveConfig));
    if (!before) CHECK(r.e.config().skyPreset != "off");     // random turns the sky on
    // sky tile 2 (Wildfire Dusk, the realistic skies come first) at x 206 + 2 * 56, y 66: picking one = your sky, random off
    r.click(206 + 2 * 56 + 20, 78);
    CHECK_EQ(r.e.config().skyPreset, std::string("dusk"));
    CHECK(!r.e.config().skyShuffle);
    // pager >, then the first tile of page 2 = Supernova
    r.click(358, 151);
    r.click(206 + 20, 78);
    CHECK_EQ(r.e.config().skyPreset, std::string("supernova"));
    // Strength slider: click at the far right of the track (258 + 108) -> 1.0
    r.click(369, 165);
    CHECK_NEAR(r.e.config().skyStrength, 1.0, 1e-3);
    // tabs: Lyrics (3rd pill)
    r.click(206 + 2 * 27.17f + 12, 210);
    CHECK(r.p.tab() == Panel::Tab::Lyrics);
    bool fl = r.e.config().lyricsStyle == LyricsStyle::Floating;
    r.click(300, 75); // "Floating words" (row 1: y 67)
    CHECK_EQ(r.e.config().lyricsStyle == LyricsStyle::Floating, !fl);
    int off = r.e.config().syncOffsetMs;
    r.click(359, 187); // timing ›
    CHECK_EQ(r.e.config().syncOffsetMs, off + 100);
    r.click(258, 171); // Distance slider, far left -> 0.4 (closest)
    CHECK_NEAR(r.e.config().lyricsDistance, 0.4, 1e-3);
    // player buttons (on the record card) and seek
    CHECK(PanelRig::has(r.click(98, 207), PanelAction::Kind::PlayPause));
    CHECK(PanelRig::has(r.click(126, 207), PanelAction::Kind::Next));
    auto sk = r.click(30 + 68, 173);
    bool seek = false;
    for (auto const& x : sk) seek |= x.kind == PanelAction::Kind::Seek && x.ms > 90000 && x.ms < 110000;
    CHECK(seek);
    // [BF] forgiving aim: just outside the "Whole line" toggle (288, 123, 78 x 16) still hits it
    bool wl = r.e.config().wholeLine;
    r.click(287, 131); // 1 px left of it (3 px from "Spread out"): nearer wins
    CHECK_EQ(r.e.config().wholeLine, !wl);
    // ... and a point in the 2 px gap between FX rows goes to the nearer one (Drift row 94..107 vs Dissolve 109..122)
    r.click(206 + 4 * 27.17f + 12, 210); // FX tab (5th pill)
    CHECK(r.p.tab() == Panel::Tab::Fx);
    r.click(206 + 40.5f + 19, 107.6f);
    CHECK(r.e.config().exitAnim == ExitAnim::Drift);
    r.click(206 + 40.5f + 19, 108.6f);
    CHECK(r.e.config().exitAnim == ExitAnim::Dissolve);
    // close button
    CHECK(PanelRig::has(r.click(358, 13), PanelAction::Kind::Close));
}

// ================================================================ performance (2.1 AutoPerf / quality / profiles)
TEST(autoperf_levels) {
    Config   cfg; // target 60
    AutoPerf ap;
    int64_t  t = 0;
    for (int i = 0; i < 12; ++i, t += 500) ap.tick(t, 30, 4.0, cfg); // 30 fps, mod costs 4 ms: heavy and low
    CHECK(ap.level() >= 2);
    CHECK_EQ(ap.quality(cfg), 0);
    Config e = effectiveConfig(cfg, ap);
    CHECK(e.maxLetters <= 60);
    for (int i = 0; i < 300; ++i, t += 500) ap.tick(t, 120, 0.2, cfg); // cheap again: relaxes (a "bad" level waits 2 min)
    CHECK_EQ(ap.level(), 0);
    CHECK_EQ(ap.quality(cfg), 2);
    AutoPerf cheap;                                                   // low FPS but the mod is cheap: never blamed
    for (int i = 0; i < 12; ++i) cheap.tick(int64_t(i) * 500, 25, 0.1, cfg);
    CHECK_EQ(cheap.level(), 0);
    cfg.vsync = true;
    CHECK_EQ(ap.target(cfg), 57);
}
TEST(quality_and_profiles) {
    Config   cfg;
    AutoPerf ap;
    cfg.quality = 0;
    Config e = effectiveConfig(cfg, ap);
    CHECK(!e.glow);
    CHECK(!e.twoColor);
    applyProfile(cfg, 1);
    CHECK(cfg.combatSafe);
    CHECK(!cfg.fallingLetters);
    CHECK_EQ(cfg.maxLines, 1);
    applyProfile(cfg, 0);
    CHECK(!cfg.combatSafe);
    CHECK_EQ(cfg.quality, 2);
}

TEST(pvp_calm) { // [LyricsForge] in a fight the look steps aside, then comes back
    using namespace bf::lyrics;
    Config cfg; cfg.director = true; cfg.showcase = true; cfg.letterbox = true; cfg.impactFrames = true;
    MusicState m; m.active = true; m.budget = 1; m.build = 1; m.dropNow = true; m.bigDrop = true;
    Cinema a, b;
    for (int i = 0; i < 40; ++i) { a.step(&m, cfg, 0.05f); m.dropNow = false; }
    m.dropNow = true;
    b.setCombat(1.0f);
    for (int i = 0; i < 40; ++i) { b.step(&m, cfg, 0.05f); m.dropNow = false; }
    CHECK(a.out().showcase > 0.5f && a.out().letterbox > 0.01f);
    CHECK(b.out().showcase < 0.001f && b.out().letterbox < 0.001f && b.out().flash < 0.001f);
    CHECK_NEAR(b.out().fovScale, 1.0f, 0.001f);
    CHECK(b.out().vignette < 0.001f);
    cfg.combatCalm = false;                                            // the toggle turns it off
    Cinema c; c.setCombat(1.0f);
    for (int i = 0; i < 40; ++i) c.step(&m, cfg, 0.05f);
    CHECK(c.out().showcase > 0.5f);
}

TEST(font_script_fallback) { // [LyricsForge] Japanese glyphs come from the cjk atlas instead of '?'
    auto js = [](std::vector<int> cps, double adv) {
        std::string g;
        for (int c : cps) g += (g.empty() ? "" : ",") + std::string("{\"unicode\":") + std::to_string(c) + ",\"advance\":" + std::to_string(adv) +
             ",\"planeBounds\":{\"left\":0,\"bottom\":0,\"right\":0.5,\"top\":0.7},\"atlasBounds\":{\"left\":0,\"bottom\":10,\"right\":10,\"top\":0}}";
        return "{\"atlas\":{\"type\":\"sdf\",\"distanceRange\":6,\"size\":48,\"width\":64,\"height\":64,\"yOrigin\":\"top\"},\"metrics\":{\"ascender\":0.8,\"descender\":-0.2,\"lineHeight\":1.2},\"glyphs\":[" + g + "]}";
    };
    FontSet fs;
    CHECK(fs.loadFromJson("poppins", js({'A', '?'}, 0.6), "p.png"));
    CHECK(fs.loadFromJson("cjk_jp", js({0x3042}, 1.0), "j.png"));
    fs.setAtlasIndex("poppins", 0);
    fs.setAtlasIndex("cjk_jp", FontSet::kNoAtlas);
    fs.linkFallbacks();
    CHECK(std::fabs(fs.get("poppins")->width("\xe3\x81\x82") - 0.3f) < 1e-4f); // no slot yet: a small gap, no '?'
    fs.setAtlasIndex("cjk_jp", 1);
    fs.linkFallbacks();
    CHECK(std::fabs(fs.get("poppins")->width("A\xe3\x81\x82") - 1.6f) < 1e-4f); // A + the Japanese glyph
    std::vector<BfGlyphV1> out;
    DrawWord w; w.text = "\xe3\x81\x82"; w.alpha = 1; w.plain = true;
    Config c;
    buildWordGlyphs(w, *fs.get("poppins"), c, makeStyle(c, 0), Vec3{}, out, true);
    CHECK(!out.empty() && out.back().atlas == 1u);
}

TEST(fit_to_fonts) { // [LyricsForge] lines we can't draw: their translation, or nothing - never '????'
    using namespace bf::lyrics::translate;
    Lyrics l;
    auto add = [&](std::string t, std::string tr, int64_t s0) { Line ln; ln.index = int(l.lines.size()); ln.text = t; ln.translation = tr; ln.start = s0; ln.end = s0 + 2000; ln.words = {Word::of(t, s0, s0 + 2000)}; l.lines.push_back(ln); };
    add("\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4", "Hello there friend", 0);   // Korean (not in the atlas), translated
    add("\xea\xb0\x80\xeb\x82\x98", "", 3000);                              // Korean, no translation
    add("You don't need to change", "", 6000);
    auto canDraw = [](uint32_t c) { return c < 0x250; };
    CHECK(fitToFonts(l, canDraw) == 2);
    CHECK(l.lines.size() == 2);                                   // the untranslatable line is gone
    CHECK(l.lines[0].text == "Hello there friend");               // the translation took its place
    CHECK(l.lines[0].translation.empty());
    CHECK(l.lines[0].words.size() == 3);                          // and has words again
    CHECK(l.lines[1].text == "You don't need to change");
}

TEST(auto_translate) { // [LyricsForge] non-English lines get a small English line; English lines stay alone
    using namespace bf::lyrics::translate;
    auto mk = [](std::vector<std::string> ts) { Lyrics l; int k = 0; for (auto& t : ts) { Line ln; ln.index = k++; ln.text = t; l.lines.push_back(ln); } return l; };
    Lyrics mixed = mk({"\xe3\x81\x8a\xe3\x81\xaf\xe3\x82\x88\xe3\x81\x86", "Flamingo, oh oh oh-woah", "You don't need to change"});
    CHECK(nonLatin(mixed.lines[0].text));
    CHECK(!nonLatin(mixed.lines[1].text));
    CHECK(needsTranslation(mixed));
    std::string asked;
    auto fake = [&](std::string const& url, bf::lyrics::fetch::Headers const&, int64_t) {
        asked = url;
        return bf::lyrics::fetch::HttpResponse{200, R"([[["Good morning\n","x",null],["Flamingo, oh oh oh-woah\n","x",null],["You don't have to change","x",null]],null,"ja"])"};
    };
    CHECK(translateToEnglish(fake, mixed, bf::lyrics::fetch::nowMs() + 5000) == 1);
    CHECK(mixed.lines[0].translation == "Good morning");
    CHECK(mixed.lines[1].translation.empty());   // same words
    CHECK(mixed.lines[2].translation.empty());   // already English, only reworded
    CHECK(asked.find("translate.googleapis.com") != std::string::npos);
    Lyrics english = mk({"I love you baby", "you are the one for me", "never let me go tonight", "oh yeah", "I want you now and here", "we are the night"});
    CHECK(!needsTranslation(english));
    Lyrics spanish = mk({"Quiero que te quedes conmigo", "Bailando bajo la luna", "Tu corazon es mio esta noche", "Mi amor no se va", "Dame tu mano y vamos"});
    CHECK(needsTranslation(spanish));
    CHECK(parseGtx("garbage").empty());
    CHECK(parseGtx(R"([[["Hello","Hola",null]],null,"es"])") == "Hello");
}

TEST(color_words) { // [LyricsForge] colour words turn their colour
    CHECK(colorWord("pink") == 0xFF5FB4u);
    CHECK(colorWord("Pink!") == 0xFF5FB4u);
    CHECK(colorWord("flamingos") != 0);
    CHECK(colorWord("blue,") == 0x3B8BFFu);
    CHECK(colorWord("blue-eyed") == 0x3B8BFFu);
    CHECK(colorWord("golden") != 0);
    CHECK(colorWord("the") == 0);
    CHECK(colorWord("predictable") == 0);
    CHECK(colorWord("") == 0);
    CHECK(colorWord("\xe3\x83\x94\xe3\x83\xb3\xe3\x82\xaf\xe8\x89\xb2\xe3\x81\xab") == 0xFF5FB4u); // pinku-iro ni
    CHECK(colorWord("\xe5\x8b\x95\xe7\x89\xa9\xe5\x9c\x92") == 0); // doubutsuen (zoo): no colour
    CHECK(colorWord("\xe9\x9d\xa2\xe7\x99\xbd\xe3\x81\x84") == 0); // omoshiroi (interesting) is not white
    CHECK(colorWord("\xe8\xb5\xa4\xe3\x81\xa1\xe3\x82\x83\xe3\x82\x93") == 0); // akachan (baby) is not red
    CHECK(colorWord("\xe9\x9d\x92\xe6\x98\xa5") == 0);                         // seishun (youth) is not blue
    CHECK(colorWord("\xe8\xb5\xa4\xe3\x81\x84") == 0xFF3B3Bu);                 // akai (red)
    CHECK(colorWord("\xe7\x9c\x9f\xe3\x81\xa3\xe9\x9d\x92\xe3\x81\xaa") == 0x3B8BFFu); // massao na (deep blue)
    // a whole unspaced line holding a colour is a phrase, not a word: no tint for the line
    CHECK(colorWord("\xe3\x83\x94\xe3\x83\xb3\xe3\x82\xaf\xe3\x81\xae\xe7\xa9\xba\xe3\x82\x92\xe8\xa6\x8b\xe4\xb8\x8a\xe3\x81\x92\xe3\x81\xa6\xe3\x81\x84\xe3\x81\x9f") == 0);
}

TEST(finale_constellation) { // [LyricsForge] the song's last line rises into the sky as stars, holds, dissolves
    using bf::lyrics::SkyWriter;
    SkyWriter sw;
    SkyWriter::In in; in.dt = 0.1f; in.enabled = false; // any sky: the finale isn't tied to starry skies
    Line l; l.index = 9; l.start = 1000; l.text = "Woah oh";  // short line: one star per letter
    sw.finale(l, in);
    CHECK(sw.finaleShowing());
    std::vector<BfGlyphV1> out;
    for (int i = 0; i < 20; ++i) sw.step(in);
    sw.emit(in, Vec3{}, out);
    CHECK(out.size() >= 6);                                  // stars (+ halos) drawn
    in.songSeq = 5; for (int i = 0; i < 10; ++i) sw.step(in); // the next song started: the finale stays up
    CHECK(sw.finaleShowing());
    for (int i = 0; i < 40; ++i) sw.step(in);                 // held long enough: it dissolves
    CHECK(!sw.finaleShowing());
}

TEST(audit_fixes) { // [LyricsForge] the review's findings, kept fixed
    using namespace bf::lyrics::translate;
    { // a cache with only some lines translated still asks for the rest
        Lyrics l;
        for (int i = 0; i < 6; ++i) {
            Line ln; ln.index = i; ln.start = i * 1000;
            ln.text = "\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86"; // aiu
            if (i < 3) ln.translation = "abc";
            l.lines.push_back(ln);
        }
        CHECK(needsTranslation(l));
        for (auto& ln : l.lines) ln.translation = "abc";
        CHECK(!needsTranslation(l));
    }
    { // emoji / variation selectors / hearts are not letters: "I <heart> you" stays a line
        CHECK(!countsAsLetter(0x2764)); CHECK(!countsAsLetter(0xFE0F)); CHECK(!countsAsLetter(0x1F496));
        CHECK(countsAsLetter('I')); CHECK(countsAsLetter(0x3042));
        Lyrics l; Line ln; ln.index = 0; ln.start = 0; ln.text = "I \xe2\x9d\xa4\xef\xb8\x8f you";
        ln.words = {Word::of(ln.text, 0, 1000)}; l.lines.push_back(ln);
        fitToFonts(l, [](uint32_t c) { return c < 0x80; });
        CHECK(l.lines.size() == 1);
    }
    { // an English song with a Japanese verse: only the Japanese lines are sent (small batches), already-done ones skipped
        Lyrics l; int k = 0;
        auto add = [&](std::string t, std::string tr = "") { Line ln; ln.index = k; ln.start = 1000 * k++; ln.text = t; ln.translation = tr; l.lines.push_back(ln); };
        for (int r = 0; r < 4; ++r) { add("How many shrimps do you have to eat"); add("Before you make your skin turn pink"); add("You don't need to change"); }
        add("\xe3\x82\xa8\xe3\x83\x93", "Shrimp");                   // ebi, translated last time
        add("\xe3\x83\x95\xe3\x83\xa9\xe3\x83\x9f\xe3\x83\xb3\xe3\x82\xb4"); // furamingo, still open
        CHECK(needsTranslation(l));
        std::string asked;
        auto fake = [&](std::string const& url, bf::lyrics::fetch::Headers const&, int64_t) {
            asked = url;
            return bf::lyrics::fetch::HttpResponse{200, R"([[["Flamingo","x",null]],null,"ja"])"};
        };
        CHECK(translateToEnglish(fake, l, bf::lyrics::fetch::nowMs() + 5000) == 1);
        CHECK(asked.find("How") == std::string::npos);             // no English line in the request
        CHECK(asked.find("%E3%82%A8") == std::string::npos);        // the done line wasn't asked again
        CHECK(l.lines.back().translation == "Flamingo");
        CHECK(lastDiag().find("1 sent") == 0);
        CHECK(!needsTranslation(l));
    }
    { // a font filter: only fonts that draw the song's letters
        auto f = songFontFor(SkyMood::Hype, true, 0xFF5FA8, 12345u, "", [](std::string_view id) { return id == "bangers"; });
        CHECK(f == "bangers" || f.empty());
        auto none = songFontFor(SkyMood::Hype, true, 0xFF5FA8, 12345u, "", [](std::string_view) { return false; });
        CHECK(none.empty());
    }
}

TEST(cover_skies) { // [LyricsForge] the cover sets the sky colour: pink cover -> pink skies
    auto pink = coverSkies(0xFF5FA8);
    CHECK(!pink.empty());
    CHECK(pink.front() == "cotton_candy");
    CHECK(std::find(pink.begin(), pink.end(), "sakura") != pink.end());
    CHECK(std::find(pink.begin(), pink.end(), "eclipse_heart") == pink.end()); // bright pink: no dark skies
    CHECK(std::find(pink.begin(), pink.end(), "cherry") == pink.end());
    CHECK(coverIsVividBright(0xFF5FA8));
    { // random fonts that fit: a pink cover's set, never the same one twice in a row, all of them real fonts
        auto const& pf = songFonts(SkyMood::Hype, true, 0xFF5FA8);
        CHECK(pf.size() >= 4 && pf.front() == "shrikhand");
        std::set<std::string> seen;
        for (uint32_t r = 0; r < 4000; r += 37) {
            auto f = songFontFor(SkyMood::Hype, true, 0xFF5FA8, r * 2654435761u, "shrikhand");
            CHECK(f != "shrikhand"); seen.insert(f);
            CHECK(std::filesystem::exists("resources/lyricsflow/fonts/" + f + ".json"));
        }
        CHECK(seen.size() >= 4); // not predictable
        for (int m = 0; m < 6; ++m) for (auto f : songFonts(SkyMood(m), true, 0x202020))
            CHECK(std::filesystem::exists("resources/lyricsflow/fonts/" + std::string(f) + ".json"));
        CHECK(songFontFor(SkyMood::Bright, false, 0, 1, "").empty());
    }
    CHECK(!coverIsVividBright(0x402030));
    CHECK(std::find(pink.begin(), pink.end(), "storm") == pink.end());
    CHECK(coverSkies(0x808080).empty()); // grey cover: nothing to match
    CHECK(coverSkies(0).empty());
    CHECK(coverSkies(0x2E86FF).front() == "ocean");
    std::vector<std::string> recent = {"cotton_candy", "sakura"};
    auto p = pickCoverSky(0xFF5FA8, recent, "cherry", 12345);
    CHECK(!p.empty() && p != "cotton_candy" && p != "sakura" && p != "cherry");
}

TEST(sky_mood) { // [LyricsForge] Random fits the song
    using namespace bf::lyrics;
    MoodIn hype; hype.highShare = 0.8f; hype.calmShare = 0.1f; hype.dropsPerMin = 1.6f; hype.cover = 0xFF4FA0;
    auto const hm = moodOf(hype).mood;
    CHECK(hm == SkyMood::Hype || hm == SkyMood::Bright);           // a loud, drop-heavy, colourful song
    MoodIn sad; sad.highShare = 0.1f; sad.calmShare = 0.6f; sad.cover = 0x506070;
    sad.words = {"I cry alone in the rain", "goodbye my love", "tears on the cold glass", "nothing left"};
    CHECK(moodOf(sad).mood == SkyMood::Melancholy);
    MoodIn love; love.highShare = 0.35f; love.calmShare = 0.3f; love.cover = 0xF0A0D0;
    love.words = {"baby I love you", "hold my heart", "forever mine", "kiss me slow"};
    CHECK(moodOf(love).mood == SkyMood::Dreamy);
    MoodIn dark; dark.highShare = 0.7f; dark.calmShare = 0.1f; dark.cover = 0x200808;
    dark.words = {"blood on the fire", "burn it all", "fight the devil", "scream"};
    CHECK(moodOf(dark).mood == SkyMood::Dark);
    MoodIn night; night.highShare = 0.35f; night.calmShare = 0.3f; night.cover = 0x1A2A6A;
    night.words = {"under the moon tonight", "stars in the sky", "midnight dreams"};
    CHECK(moodOf(night).mood == SkyMood::Night);
    for (int m = 0; m < 6; ++m) {                                    // every pick is a curated sky that exists
        Config cfg;
        for (auto id : moodSkies(SkyMood(m))) { CHECK(isCurated(id)); CHECK(sky::find(id, cfg).has_value()); }
        auto calm = pickMoodSky(SkyMood(m), 0.0f, {}, "", 7), peak = pickMoodSky(SkyMood(m), 1.0f, {}, "", 7);
        auto const& l = moodSkies(SkyMood(m));
        auto idx = [&](std::string const& id) { return int(std::find(l.begin(), l.end(), id) - l.begin()); };
        CHECK(idx(calm) <= 1 && idx(peak) >= int(l.size()) - 2);    // quiet parts low on the list, drops at the top
        CHECK(pickMoodSky(SkyMood(m), 1.0f, {}, peak, 7) != peak);  // never the same sky again on the next change
    }
    for (auto id : curatedSkies()) CHECK(sky::find(id, Config{}).has_value());
    CHECK(!isCurated("matrix") && !isCurated("custom") && isCurated("cotton_candy") && isCurated("prism"));
}

TEST(world_light) { // [LyricsForge] the sky lights the world
    using namespace bf::lyrics::sky;
    Config cfg;
    cfg.awaken = false;
    auto dusk = *find("dusk", cfg), night = *find("bright_night", cfg), summer = *find("summer", cfg);
    auto w = worldLightFor(dusk, 1, 1, 0.3f, 0, 0);
    CHECK(w.tint[0] > 1.0f && w.tint[2] < 1.0f);                       // dusk warms the ground
    CHECK(worldLightFor(summer, 1, 1, 0.3f, 0, 0).tint[2] > 1.0f);     // a blue day cools it
    for (auto const& p : {dusk, night, summer}) {                      // hue only: the tint keeps the brightness
        auto x = worldLightFor(p, 1, 1, 0.3f, 1, 0);
        CHECK_NEAR(0.2126f * x.tint[0] + 0.7152f * x.tint[1] + 0.0722f * x.tint[2], 1.0f, 0.2f);
    }
    auto drop = worldLightFor(night, 1, 1, 0.3f, 1, 0);
    CHECK(drop.amount > worldLightFor(night, 1, 1, 0.3f, 0, 0).amount + 0.08f); // the drop lifts it (gently, no strobe)
    CHECK(drop.sat > 1.1f && drop.exposure > worldLightFor(night, 1, 1, 0.3f, 0, 0).exposure);
    CHECK(worldLightFor(night, 1, 1, 0.3f, 0, 0).exposure < worldLightFor(summer, 1, 1, 0.3f, 0, 0).exposure);
    CHECK(worldLightFor(summer, 1, 0.4f, 0, 0, 0).exposure < 0.9f);    // dimmed...
    CHECK(worldLightFor(night, 1, 0.3f, 0, 0, 0).exposure >= 0.6f);    // ...never below 60%
    for (auto const& p : presets()) {                                   // every sky lights the world in its colour
        auto x = worldLightFor(p, 1, 1, 0.3f, 0, 0);
        float dev = 0; for (float v : x.tint) dev = std::max(dev, std::fabs(v - 1.0f));
        CHECK(x.amount > 0.3f && dev >= 0.29f);
    }
    CHECK(worldLightFor(night, 1, 1, 0, 0, 1).exposure > 1.3f);        // lightning lights the ground
    auto off = worldLightFor(dusk, 0, 1, 0.5f, 1, 1);                  // sky not visible: no change
    CHECK(off.neutral());
    System sys;                                                         // eased in over a few frames, out with the sky
    SystemIn in; in.dtSec = 0.05; in.kick = 1; in.pulse = 1.3f;
    cfg.skyPreset = "dusk";
    AutoPerf perf;
    for (int i = 0; i < 60; ++i) sys.step(true, cfg, perf, in);
    CHECK(sys.world().amount > 0.3f && sys.world().tint[0] > 1.0f);
    cfg.skyPreset = "off";
    for (int i = 0; i < 240; ++i) sys.step(true, cfg, perf, in);
    CHECK(sys.world().neutral());
}

TEST(live_sky_params) {
    using namespace bf::lyrics::sky;
    Config cfg;
    CHECK_EQ(int(presets().size()), 43);
    CHECK_EQ(int(order().size()), 30);
    CHECK_EQ(find("summer", cfg)->ordinal, 38);                    // [LyricsForge] realistic skies 38..42
    CHECK_EQ(find("silver_lining", cfg)->ordinal, 42);
    CHECK(!find("summer", cfg)->painted());
    CHECK(body(*find("bright_night", cfg)).has_value());
    for (auto id : order()) CHECK(find(id, cfg).has_value());
    CHECK(!find("off", cfg).has_value());
    auto syn = *find("synthwave", cfg);
    CHECK_EQ(syn.ordinal, 0);
    CHECK(find("polar", cfg)->painted());
    CHECK_EQ(find("thunder", cfg)->ordinal, 28);
    float id3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Params q = pack(syn, aurora1(syn, cfg), aurora2(syn, cfg), 0.0, cfg, id3);
    CHECK_EQ(int(q[0][0]), 0x0B); CHECK_EQ(int(q[0][2]), 0x24); CHECK_EQ(int(q[0][3]), 255);
    CHECK_EQ(int(q[4][3]), 4);                      // SYNTH kind 3 + 1
    CHECK_EQ(int(q[4][0]), 0xFF); CHECK_EQ(int(q[4][1]), 0xD3);
    CHECK_EQ(int(q[5][0]), 170);                    // pattern 1 / 1.5
    CHECK_EQ(int(q[5][1]), 153);                    // lively 0.6
    CHECK_EQ(int(q[5][2]), 217);                    // breath 0.85 + 0.15 sin(0)
    CHECK_EQ(int(q[5][3]), 255);
    CHECK_EQ((int(q[6][0]) << 8 | q[6][1]), 18000); // az 180 * 100
    CHECK_EQ((int(q[6][2]) << 8 | q[6][3]), 10700); // (el 17 + 90) * 100 [BF] raised
    CHECK_EQ((int(q[7][0]) << 8 | q[7][1]), 10000); // radius 10 * 1000
    CHECK_EQ(int(q[7][2]), 0);                      // PRESET
    // identity matrix: enc(1) = 49151, enc(0) = 32768 (round(32767.5))
    CHECK_EQ((int(q[8][0]) << 8 | q[8][1]), 49151);
    CHECK_EQ((int(q[8][2]) << 8 | q[8][3]), 32768);
    auto pol = *find("polar", cfg);
    CHECK(!body(pol).has_value() || pol.moon != 0);
    CHECK_EQ(int(pack(pol, 0, 0, 0, cfg, id3)[7][2]), 20);
    cfg.skyPalette = 2;
    CHECK_EQ(aurora1(syn, cfg), 0xFFB35Cu);
    cfg.customTop = 0x123456;
    CHECK_EQ(find("custom", cfg)->top, 0x123456u);
    auto const& st = stars();
    CHECK_EQ(int(st.size()), 1300);
    CHECK(std::abs(st[0].az - 237.5747f) < 1e-3f); // java.util.Random(1337): reference values from the JVM
    CHECK(std::abs(st[0].el - 11.948353f) < 1e-3f);
    CHECK(std::abs(st[1].bright - 0.46347016f) < 1e-6f);
    CHECK(std::abs(st[2].phase - 2.0018098f) < 1e-5f);
    std::vector<StarDraw> sd;
    float up[3] = {0, 1, 0};
    drawStars(*find("aurora", cfg), cfg, 2, 1.0f, 3.0, up, sd);
    CHECK(sd.size() > 300 && sd.size() <= 520);
    Clock c;
    Frame f{};
    Clock::In ci; ci.dtSec = 0.05;
    for (int i = 0; i < 100; ++i) f = c.tick(&syn, cfg, ci);
    CHECK(f.alpha > 0.99f);
    CHECK(std::abs(f.t - float((1000.0 + 5.0) * 2.2)) < 0.05f);
}

TEST(sky_layers) {
    using namespace bf::lyrics::sky;
    Config   cfg;
    AutoPerf perf;
    Fx       fx;
    Clock    clk;
    Clock::In ci;
    ci.dtSec = 0.05;
    auto storm = *find("storm", cfg), aurora = *find("aurora", cfg), sakura = *find("sakura", cfg);
    Frame f{};
    for (int i = 0; i < 60; ++i) f = clk.tick(&storm, cfg, ci);
    CHECK(f.visible > 0.95f);
    FxIn in;
    in.cam = Cam::of(0, -30); // looking up toward +Z
    CHECK(std::abs(in.cam.fz - std::cos(rad(30))) < 1e-9 && in.cam.fy > 0.4);
    CHECK(std::abs(in.cam.rx + 1) < 1e-9);  // facing +Z, right is -X (Minecraft)
    std::vector<Quad> out;
    int  rainQuads = 0, skyQuads = 0, flashSeen = 0;
    for (int i = 0; i < 400; ++i) {
        f = clk.tick(&storm, cfg, ci);
        in.nowMs += 50; in.dtSec = 0.05;
        out.clear();
        fx.frame(&storm, cfg, perf, f, in, out);
        int w = 0, s = 0;
        for (auto const& q : out) (q.layer == Layer::World ? w : s)++;
        rainQuads = std::max(rainQuads, w); skyQuads = std::max(skyQuads, s);
        for (auto const& q : out) if (q.v[0].rgb == 0xC8D4FFu) { ++flashSeen; break; }
    }
    CHECK_EQ(int(fx.particles()), int(220 * 0.85f));
    CHECK(rainQuads > 40);                     // rain in front of the camera
    CHECK(skyQuads > 40);                      // lightning: glow fan + bolt (storm has no stars)
    CHECK(flashSeen > 3);                      // lightning flashes every 0.5..2.5 s
    // shooting stars: aurora has them; one every 1.2..4.7 s, each lives 0.9 s
    Fx fx2;
    int  maxShots = 0, spawned = 0;
    size_t prev = 0;
    for (int i = 0; i < 600; ++i) {            // 30 s
        in.nowMs += 50;
        out.clear();
        fx2.shooting(1.0f, in.nowMs, in.cam, out);
        if (fx2.shootingCount() > prev) ++spawned;
        prev = fx2.shootingCount();
        maxShots = std::max(maxShots, int(fx2.shootingCount()));
    }
    CHECK(spawned >= 6 && spawned <= 26);
    CHECK(maxShots <= 2);
    // underground: weather fades out with the sky above
    ci.skyOpen = false;
    for (int i = 0; i < 80; ++i) f = clk.tick(&sakura, cfg, ci);
    CHECK(f.outside < 0.01f);
    out.clear();
    Fx fx3;
    fx3.frame(&sakura, cfg, perf, f, in, out);
    for (auto const& q : out) CHECK(q.layer == Layer::Sky);
    // picture replaces everything
    in.picture = true;
    out.clear();
    fx3.frame(&aurora, cfg, perf, f, in, out);
    CHECK(out.empty());
    // My picture file choice
    CHECK(isPictureFile("Pano.JPG"));
    CHECK(!isPictureFile("notes.txt"));
    CHECK_EQ(pictureChoice(cfg, {"b.png", "a.jpg", "x.txt"}), std::string("a.jpg"));
    cfg.skyImageFile = "b.png";
    CHECK_EQ(pictureChoice(cfg, {"b.png", "a.jpg"}), std::string("b.png"));
    cfg.skyImageFile = "gone.png";
    CHECK_EQ(pictureChoice(cfg, {"b.png", "a.jpg"}), std::string("a.jpg"));
    CHECK_EQ(pictureChoice(cfg, {}), std::string(""));
    int pw, ph;
    pictureSize(8000, 3000, pw, ph);
    CHECK(pw == 4096 && ph == 2048);
    CHECK(std::abs(nightOf(18000) - 1) < 1e-6 && nightOf(6000) == 0 && std::abs(nightOf(12500) - 0.5f) < 1e-6);
}

TEST(awaken) { // [LyricsForge] no music = the vanilla world; a song starts = ~5 s of everything coming alive
    using namespace bf::lyrics::sky;
    Config cfg; cfg.skyPreset = "dusk";
    AutoPerf perf;
    System sys;
    SystemIn in; in.dtSec = 0.05; in.pitch = -10;
    BfSkyFrameV1 h{};
    auto run = [&](float sec, bool playing) { in.playing = playing; for (int i = 0; i < int(sec / 0.05f); ++i) { in.nowMs += 50; sys.step(true, cfg, perf, in); } };
    run(3.0f, false);
    h.size = sizeof h; BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(h.active, 0u);                                  // silence: vanilla
    CHECK(sys.awaken() == 0.0f && sys.world().amount < 0.01f);
    run(0.3f, true);                                         // the song starts
    h.size = sizeof h; BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(h.active, 1u);
    CHECK(h.params[14][0] > 0 && h.params[14][1] == 255);    // the sky rises over the vanilla one
    CHECK(sys.awaken() > 0.0f && sys.awaken() < 0.2f);
    CHECK(sys.world().amount < 0.05f);                       // the world isn't lit yet
    run(3.0f, true);
    CHECK(sys.world().amount > 0.12f);                       // the light flowed in (glides now, ~0.8 s)
    run(3.0f, true);
    CHECK(sys.awaken() == 1.0f);
    h.size = sizeof h; BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(int(h.params[14][0]), 0);                        // the startup is over
    run(2.0f, false);                                        // a short pause: still alive
    CHECK(sys.awaken() == 1.0f);
    run(4.0f, false);                                        // real silence: the outro (the world exhales)
    CHECK(sys.inOutro());
    CHECK(sys.awaken() > 0.0f && sys.awaken() < 1.0f);
    h.size = sizeof h; BF_CopySkyFrame(&h, nullptr, 0);
    CHECK(h.params[14][0] > 0 && h.params[14][1] == 255);    // the sky sinks back over the vanilla one
    float const mid = sys.awaken();
    run(1.0f, true);                                         // the music comes back mid-outro: it rises again from there
    CHECK(!sys.inOutro());
    CHECK(sys.awaken() >= mid);
    run(6.0f, true);
    CHECK(sys.awaken() == 1.0f);
    in.songSeq = 7; run(0.2f, true);                         // the next song with no gap: a chapter
    in.songSeq = 8; run(0.5f, true);
    CHECK(sys.inChapter());
    CHECK(sys.awaken() == 1.0f);                             // the world stays alive through it
    run(4.0f, true);
    CHECK(!sys.inChapter());
    run(4.5f, false);                                        // silence again: outro ...
    CHECK(sys.inOutro());
    run(4.5f, false);                                        // ... then vanilla
    CHECK(sys.awaken() == 0.0f && !sys.inOutro());
    run(1.0f, false);
    h.size = sizeof h; BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(h.active, 0u);
}

TEST(sky_system_store) {
    using namespace bf::lyrics::sky;
    Config   cfg;
    cfg.awaken = false; // the sky without music (Awaken has its own test)
    AutoPerf perf;
    System   sys;
    SystemIn in;
    in.dtSec = 0.05; in.pitch = -40; in.spritesFile = "C:/x/sprites.png";
    BfSkyFrameV1 h{};
    h.size = sizeof h;
    sys.step(true, cfg, perf, in); // skyPreset "off": inactive
    BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(h.active, 0u);
    cfg.skyPreset = "midnight";
    for (int i = 0; i < 40; ++i) { in.nowMs += 50; sys.step(true, cfg, perf, in); }
    std::vector<BfSkyVertV1> v(20000);
    h.size = sizeof h;
    uint32_t n = BF_CopySkyFrame(&h, v.data(), uint32_t(v.size()));
    CHECK_EQ(h.active, 1u);
    CHECK(n > 100 && n % 4 == 0 && n == h.vertTotal); // [LyricsForge] constellations; the stars are the shader's own now
    CHECK_EQ(int(h.params[7][2]), find("midnight", cfg)->ordinal);
    CHECK(h.info[2] > 0.5f && std::string(h.spritesFile) == "C:/x/sprites.png");
    CHECK_EQ((v[0].color >> 16) & 255, 0xFFu); // star 0xF4F0FF: R low byte 0xF4, B in bits 16..23 = 0xFF
    in.wave = 0.5f; in.kick = 1.0f; in.lightning = 0.5f; in.nowMs += 50;  // [LyricsForge] the music reaches the shader
    sys.step(true, cfg, perf, in);
    BF_CopySkyFrame(&h, nullptr, 0);
    CHECK(h.params[13][0] > 100 && h.params[13][1] > 0 && h.params[13][1] < 128 && h.params[13][3] > 100); // [LyricsForge] kick eased + halved
    in.wave = -1; in.nowMs += 50; sys.step(true, cfg, perf, in); BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(int(h.params[13][3]), 0);
    CHECK_EQ(v[0].color & 255, 0xF4u);
    uint32_t gen = h.imageGeneration;
    cfg.skyImage = true; in.pictureFile = "C:/x/sky/pano.jpg";
    sys.step(true, cfg, perf, in);
    h.size = sizeof h;
    BF_CopySkyFrame(&h, v.data(), uint32_t(v.size()));
    CHECK_EQ(h.picture, 1u);
    CHECK_EQ(h.vertCount, 0u);           // the picture replaces stars & co
    CHECK(h.imageGeneration == gen + 1);
    h.size = 8;                           // a caller with an older/smaller header gets nothing
    CHECK_EQ(BF_CopySkyFrame(&h, v.data(), 4), 0u);
    in.overworld = false; cfg.skyImage = false;
    for (int i = 0; i < 60; ++i) sys.step(true, cfg, perf, in);
    h.size = sizeof h;
    BF_CopySkyFrame(&h, nullptr, 0);
    CHECK_EQ(h.active, 0u);              // nether / end keep their sky
}

TEST(letter_fx_exits) {
    BfGlyphV1 q{};
    q.pos[0] = 0; q.pos[1] = 2; q.pos[2] = 5;
    q.right[0] = 0.2f; q.up[1] = 0.3f;
    q.uv[0] = 0.1f; q.uv[1] = 0.2f; q.uv[2] = 0.3f; q.uv[3] = 0.6f;
    q.alpha = 1; q.fill = 0xFFFFFFFFu;
    std::vector<BfGlyphV1> word{q, q};
    word[1].pos[0] = 0.5f;
    struct M { ExitAnim m; size_t frags; } const ms[] = {{ExitAnim::Shatter, 4}, {ExitAnim::Dissolve, 72}, {ExitAnim::Explode, 2},
                                                          {ExitAnim::Burn, 40}, {ExitAnim::Glitch, 10},
                                                          {ExitAnim::Vortex, 8}};
    for (auto const& m : ms) {
        LetterFx fx;
        fx.spawn(word, Vec3{100, 64, 100}, m.m, 42);
        CHECK_EQ(fx.size(), m.frags);
        std::vector<BfGlyphV1> out;
        fx.emit(Vec3{100, 64, 100}, out);
        CHECK(!out.empty());
        // fragments keep the glyph's uv inside the original rect and start where the letter was
        for (auto const& g : out) {
            CHECK(g.uv[0] >= 0.1f - 1e-5f && g.uv[2] <= 0.3f + 1e-5f && g.uv[1] >= 0.2f - 1e-5f && g.uv[3] <= 0.6f + 1e-5f);
            CHECK(std::abs(g.pos[2] - 5.0f) < 0.5f);
        }
        for (int i = 0; i < 40; ++i) fx.step(0.05f); // 2 s: everything has played out
        CHECK_EQ(fx.size(), size_t(0));
    }
    CHECK(!LetterFx::handles(ExitAnim::Fall));
    CHECK(LetterFx::handles(ExitAnim::Burn));
}

TEST(letter_fx_cap_and_atlas) { // [BF] review fixes
    BfGlyphV1 q{};
    q.right[0] = 0.2f; q.up[1] = 0.3f; q.alpha = 1; q.atlas = 3;
    std::vector<BfGlyphV1> word{q, q};
    LetterFx fx;
    for (int i = 0; i < 200; ++i) fx.spawn(word, Vec3{}, ExitAnim::Dissolve, i); // 14400 pieces asked for
    CHECK_EQ(fx.size(), size_t(6000));                                          // capped, newest kept
    fx.dropAtlas(2);
    CHECK_EQ(fx.size(), size_t(6000));
    fx.dropAtlas(3);
    CHECK_EQ(fx.size(), size_t(0));
    LetterPhysics ph;
    ph.spawn(word, Vec3{0, 70, 0}, 1, PhysicsParams{});
    ph.dropAtlas(3);
    CHECK(ph.letters().empty());
}
TEST(exits_reach_every_word) { // [BF] screen text breaks apart; a fast Fall still drops every word before the line goes
    for (int mode = 0; mode < 2; ++mode) {
        Engine e;
        e.fonts().loadFromJson("lyricsflow_script", readFile("font_test.json"), "x.png");
        if (mode == 0) { e.config().lyricsStyle = LyricsStyle::Screen; e.config().exitAnim = ExitAnim::Shatter; }
        else { e.config().exitAnim = ExitAnim::Fall; e.config().fallingLetters = true; e.config().animSpeed = 3.0f; e.config().fallDelayMs = 600; }
        FakeWorld  w;
        Lyrics     l = testLyrics();
        SceneInput in;
        in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
        auto   cam = testCam();
        size_t peak = 0;
        for (int i = 0; i < 400; ++i) {
            in.t  = 12400 + i * 50;
            in.dt = 0.05f;
            e.frame(in, cam, &w);
            peak = std::max(peak, mode == 0 ? e.fx().size() : e.physics().letters().size());
        }
        CHECK(peak > 5);
    }
}

TEST(cover_accent_and_player_styles) { // [BF]
    // a mostly dark cover with a green patch and a little red: the accent is the green, pushed to full brightness
    std::vector<uint8_t> px(32 * 32 * 4, 0);
    for (int i = 0; i < 32 * 32; ++i) {
        uint8_t* p = &px[size_t(i) * 4];
        p[3] = 255;
        int x = i % 32, y = i / 32;
        if (x < 16 && y < 20) { p[0] = 40; p[1] = 170; p[2] = 70; }
        else if (x > 28) { p[0] = 200; p[1] = 30; p[2] = 30; }
        else { p[0] = p[1] = p[2] = 20; }
    }
    uint32_t a = coverAccent(px.data(), 32, 32, 0x123456);
    CHECK(((a >> 8) & 255) == 255);            // green channel at the top
    CHECK(((a >> 16) & 255) < 140);            // not red
    std::vector<uint8_t> grey(16 * 16 * 4, 128);
    CHECK_EQ(coverAccent(grey.data(), 16, 16, 0x123456), 0x123456u); // nothing vivid: fallback
    for (int style = 1; style <= 2; ++style) {
        PanelRig r;
        r.e.config().playerStyle = style;
        r.as.cover               = 0;
        r.as.coverAccent         = 0x4DFFC1;
        r.step(r.in(300, 100));
        bool glass = false;
        for (auto const& g : r.out) glass |= (g.flags & BF_GLYPH_GLASS) && (g.flags & BF_GLYPH_SOLID);
        CHECK(glass);
        CHECK(PanelRig::has(r.click(98, 207), PanelAction::Kind::PlayPause)); // the controls still sit where they did
        CHECK(PanelRig::has(r.click(126, 207), PanelAction::Kind::Next));
    }
}

TEST(bass_hits_only_when_loud) { // [BF] Neon pulse / beat effects
    auto run = [](float kickAmp, float padAmp, int& hits, float& peak) {
        BeatDetector d(48000);
        std::vector<float> buf(480);
        peak = 0;
        double ph = 0;
        for (int blk = 0; blk < 800; ++blk) { // 8 s, 120 bpm kicks (every 50 blocks), a steady pad underneath
            for (int i = 0; i < 480; ++i) {
                int    s   = blk * 480 + i;
                double tk  = double(s % 24000) / 48000.0; // time since the last kick
                double env = std::exp(-tk / 0.09);
                ph += 2 * 3.14159265 * (55.0 + 60.0 * std::exp(-tk / 0.03)) / 48000.0; // kick: pitch drop 115 -> 55 Hz
                buf[size_t(i)] = float(kickAmp * env * std::sin(ph) + padAmp * std::sin(2 * 3.14159265 * 440.0 * s / 48000.0));
            }
            d.feed(buf.data(), buf.size());
            peak = std::max(peak, d.level(0.01f));
        }
        hits = d.hits();
    };
    int   h;
    float pk;
    run(0.6f, 0.1f, h, pk); // loud kicks
    CHECK(h >= 12 && h <= 17);
    CHECK(pk > 0.6f);
    run(0.02f, 0.1f, h, pk); // quiet song: no thump
    CHECK_EQ(h, 0);
    run(0.0f, 0.5f, h, pk); // loud but no bass (a lead / vocals): no thump
    CHECK_EQ(h, 0);
}


// [BF] multi-band analyser: a 60 Hz tone is bass, an 8 kHz tone is treble, silence is 0
TEST(bands_split) {
    auto run = [](double hz, double amp) {
        BandAnalyzer a(48000.0f);
        std::vector<float> buf(4800);
        for (int b = 0; b < 10; ++b) { // 1 s
            for (int i = 0; i < 4800; ++i) buf[size_t(i)] = float(amp * std::sin(2 * 3.14159265 * hz * double(b * 4800 + i) / 48000.0));
            a.feed(buf.data(), buf.size(), int64_t(b + 1) * 100000000);
        }
        std::vector<BandFrame> out;
        a.drain(out);
        return std::make_pair(out.size(), out.empty() ? BandFrame{} : out.back());
    };
    auto [n1, lo] = run(60, 0.5);
    CHECK(n1 >= 18 && n1 <= 21); // 20 frames a second
    CHECK(lo.bass > 0.8f && lo.treble < 0.4f);
    auto [n2, hi] = run(8000, 0.5);
    std::printf("  8 kHz: bass %.2f mid %.2f treble %.2f\n", hi.bass, hi.mid, hi.treble);
    CHECK(hi.treble > 0.7f && hi.treble > hi.mid + 0.1f && hi.bass < 0.4f);
    auto [n3, mid] = run(800, 0.5);
    CHECK(mid.mid > hi.mid && mid.mid > lo.mid);
    auto [n4, quiet] = run(440, 0.0);
    CHECK(quiet.loud == 0.0f);
}

// [BF] Song Memory: a made-up song (quiet intro, 16 s build, drop at 0:56, calm, second high part)
namespace {
float fakeLevel(int64_t ms) {
    double s = double(ms) / 1000.0;
    if (s < 40) return 0.30f;
    if (s < 56) return float(0.30 + 0.25 * (s - 40) / 16.0); // build
    if (s < 100) return 0.85f;                                // drop -> high
    if (s < 140) return 0.35f;
    return 0.80f;
}
void playFake(SongMemory& m, int64_t fromMs, int64_t toMs, float noiseAt = -1) {
    for (int64_t t = fromMs; t < toMs; t += 50) {
        BandFrame f;
        float     v = fakeLevel(t);
        if (noiseAt >= 0 && std::llabs(t - int64_t(noiseAt)) < 300) v = 1.0f; // a TNT blast in one play
        f.bass = f.mid = f.treble = f.loud = v;
        m.record(t, f);
    }
}
} // namespace
TEST(song_memory_shape) {
    SongMemory m;
    m.reset(SongMemory::keyFor("artist\x01song", 180000), 180000);
    playFake(m, 0, 180000, 120000);
    CHECK(m.endPlay());
    CHECK_EQ(m.plays(), 1);
    CHECK(m.coverage() > 0.95f);
    m.analyze(nullptr);
    std::printf("%s", m.describe().c_str());
    SongMemory::Event const* drop = nullptr;
    for (auto const& e : m.events) if (e.kind == SongMemory::Event::Kind::Drop && !drop) drop = &e;
    CHECK(drop != nullptr);
    if (drop) {
        CHECK(std::llabs(drop->at - 56000) < 1500);
        CHECK(drop->buildFrom >= 0 && std::llabs(drop->buildFrom - 40000) < 4000);
    }
    CHECK(m.nextEvent(50000, 8000) == drop);  // seen 6 s ahead
    CHECK(m.nextEvent(20000, 8000) == nullptr);
    CHECK(m.energyAt(10000) < 0.3f && m.energyAt(80000) > 0.7f);
    // a second play that only heard the first minute still counts and keeps the map
    playFake(m, 0, 60000);
    CHECK(m.endPlay());
    CHECK_EQ(m.plays(), 1); // under half the song: merged, but not a full play
    // a play of 5 s is ignored
    playFake(m, 0, 5000);
    CHECK(!m.endPlay());
}
TEST(song_memory_save_load_and_hooks) {
    SongMemory m;
    std::string const key = SongMemory::keyFor("artist\x01song", 180000);
    m.reset(key, 180000);
    playFake(m, 0, 180000);
    m.endPlay();
    auto file = std::filesystem::temp_directory_path() / SongMemory::fileNameFor(key);
    CHECK(m.save(file));
    SongMemory b;
    b.reset(key, 180000);
    CHECK(b.load(file));
    CHECK_EQ(b.plays(), 1);
    CHECK(b.coverage() > 0.95f);
    SongMemory other;
    other.reset("someone else|180", 180000);
    CHECK(!other.load(file)); // same file name can't hand another song's map over
    std::filesystem::remove(file);
    // hooks: the repeated lines become chorus events
    std::vector<Timing>      times{{10000, -1}, {14000, -1}, {18000, -1}, {24000, -1}, {120000, -1}, {150000, -1}, {154000, -1}, {160000, -1}};
    std::vector<std::string> texts{"verse one here", "we light it up", "keep it going now", "verse two again", "bridge",
                                   "we light it up", "keep it going now", "outro line"};
    Lyrics lyr = Lyrics::build(times, texts, true, "test", 180000);
    b.analyze(&lyr);
    CHECK_EQ(b.hooks.size(), size_t(2));
    int chorus = 0;
    for (auto const& e : b.events) chorus += e.kind == SongMemory::Event::Kind::Chorus;
    CHECK(chorus >= 1);
}

TEST(conductor_directs_from_memory) { // [BF] the Director: budget per section, build before the drop, one drop hit
    SongMemory m;
    m.reset(SongMemory::keyFor("artist\x01song", 180000), 180000);
    playFake(m, 0, 180000);
    m.endPlay();
    m.analyze(nullptr);
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.memory = &m; in.bandsLive = true;
    int drops = 0; int64_t dropAt = -1; bool started = false;
    float bud20 = 0, build52 = 0, bud58 = 0, imp60 = 1;
    for (int64_t t = 0; t < 90000; t += 17) {
        in.posMs = t;
        float v = fakeLevel(t);
        in.bands.bass = in.bands.mid = in.bands.treble = in.bands.loud = v;
        c.update(in);
        auto const& s = c.state();
        if (t == 0) started = s.songStart;
        if (s.dropNow) { drops++; if (dropAt < 0) dropAt = t; }
        if (std::llabs(t - 20000) < 9) bud20 = s.budget;
        if (std::llabs(t - 52000) < 9) build52 = s.build;
        if (std::llabs(t - 58000) < 9) bud58 = s.budget;
        if (std::llabs(t - 60000) < 9) imp60 = s.impact;
    }
    std::printf("conductor: drops %d at %lld, budget20 %.2f build52 %.2f budget58 %.2f impact60 %.2f\n", drops,
                (long long)dropAt, bud20, build52, bud58, imp60);
    CHECK(started);
    CHECK(c.state().known);
    CHECK_EQ(drops, 1);
    CHECK(std::llabs(dropAt - 56000) < 1500);
    CHECK(bud20 < 0.45f);
    CHECK(build52 > 0.3f);
    CHECK(bud58 > 0.9f);
    CHECK(imp60 < 0.05f);
}
TEST(conductor_live_guess_without_memory) {
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 3; in.dt = 1.0f / 60.0f; in.bandsLive = true;
    int drops = 0; float budQuiet = 0, budLoud = 0;
    for (int64_t t = 0; t < 70000; t += 17) {
        in.posMs = t;
        float v = t < 30000 ? 0.35f + 0.02f * float(std::sin(double(t) / 300.0)) : 0.85f;
        in.bands.bass = in.bands.mid = in.bands.treble = in.bands.loud = v;
        c.update(in);
        drops += c.state().dropNow;
        if (std::llabs(t - 25000) < 9) budQuiet = c.state().budget;
        if (std::llabs(t - 60000) < 9) budLoud = c.state().budget;
    }
    std::printf("conductor live: drops %d budget quiet %.2f loud %.2f\n", drops, budQuiet, budLoud);
    CHECK(!c.state().known);
    CHECK_EQ(drops, 1);
    CHECK(budLoud > budQuiet + 0.2f);
    // pausing / disabling never fires events
    in.enabled = false;
    c.update(in);
    CHECK(!c.state().active && !c.state().dropNow);
}

TEST(sky_writes_the_song) { // [BF] hook lines become constellations, finale lights them, a new song dissolves them
    std::vector<Timing>      times{{1000, 4000}, {5000, 8000}, {9000, 12000}, {13000, 16000}, {20000, 24000}, {25000, 28000}};
    std::vector<std::string> texts{"first verse line", "we light it up", "another verse", "we light it up", "bridge part", "outro"};
    Lyrics lyr = Lyrics::build(times, texts, true, "test", 30000);
    CHECK(isHookLine(lyr, lyr.lines[1]));
    CHECK(!isHookLine(lyr, lyr.lines[0]));
    SkyWriter w;
    SkyWriter::In in;
    in.lyrics = &lyr; in.playing = true; in.songSeq = 1; in.dt = 1.0f / 30.0f;
    size_t afterFirst = 0;
    for (int64_t t = 0; t < 19000; t += 33) {
        in.t = t;
        w.step(in);
        if (t > 8500 && t < 8600) afterFirst = w.constellations().size();
    }
    CHECK_EQ(afterFirst, size_t(1));                  // written when "we light it up" ended
    CHECK_EQ(w.constellations().size(), size_t(2));   // and again at the second hook
    auto const& c = w.constellations()[0];
    CHECK(c.stars.size() >= 4 && c.stars.size() <= 18); // [BF] a star per word (not per letter)
    CHECK(!c.links.empty());
    for (auto const& s : c.stars) CHECK(s.dir.y > 0.4); // up in the sky
    std::vector<BfGlyphV1> q;
    w.emit(in, Vec3{}, q);
    CHECK(q.size() > 8);
    in.songSeq = 2; in.t = 0;
    w.step(in);
    CHECK(w.constellations().empty()); // moved to dissolving
    q.clear();
    w.emit(in, Vec3{}, q);
    CHECK(!q.empty());
}
TEST(studio_scenes) {
    CHECK(builtinScenes().size() >= 10);
    auto s = findScene("celestial_dream");
    CHECK(s.has_value());
    Config c;
    applyScene(c, *s);
    CHECK_EQ(c.scene, std::string("celestial_dream"));
    CHECK_EQ(c.skyPreset, std::string("aurora"));
    auto j = sceneToJson(captureScene(c, "mine", "My \"scene\""));
    auto back = sceneFromJson(j);
    CHECK(back.has_value());
    if (back) { CHECK_EQ(back->name, std::string("My \"scene\"")); CHECK_EQ(back->sky, std::string("aurora")); CHECK_EQ(back->letterFx, c.letterFx); }
}

TEST(drop_moment_and_chorus_look) { // [BF] held breath before a known drop, then the world turns into its chorus look
    SongMemory m;
    m.reset(SongMemory::keyFor("artist\x01song", 180000), 180000);
    playFake(m, 0, 180000);
    m.endPlay();
    m.analyze(nullptr);
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.memory = &m; in.bandsLive = true;
    int64_t dropAt = -1, liftAt = -1, endAt = -1;
    float holdMax = 0, holdAtDrop = 1, tsMin = 9, liftMax = 0;
    int lifts = 0;
    std::vector<std::pair<int64_t, float>> holds;
    for (int64_t t = 0; t < 130000; t += 17) {
        in.posMs = t;
        float v = fakeLevel(t);
        in.bands.bass = in.bands.mid = in.bands.treble = in.bands.loud = v;
        c.update(in);
        auto const& s = c.state();
        if (dropAt < 0) { holds.push_back({t, s.hold}); tsMin = std::min(tsMin, s.timeScale); }
        if (s.dropNow && dropAt < 0) { dropAt = t; holdAtDrop = s.hold; }
        if (s.liftNow) { lifts++; if (liftAt < 0) liftAt = t; }
        if (s.liftEnd && endAt < 0) endAt = t;
        liftMax = std::max(liftMax, s.lift);
    }
    CHECK(dropAt > 0);
    int early = 0;
    for (auto [t, h] : holds) {
        if (t < dropAt - 700 && h >= 0.05f) early++;            // no hush long before the drop
        if (t >= dropAt - 200) holdMax = std::max(holdMax, h);
    }
    CHECK_EQ(early, 0);
    std::printf("drop moment: drop %lld hold max %.2f at-drop %.2f timeScale min %.2f lift at %lld end %lld lifts %d\n",
                (long long)dropAt, holdMax, holdAtDrop, tsMin, (long long)liftAt, (long long)endAt, lifts);
    CHECK(holdMax > 0.6f);                                       // it holds its breath right before (strength 0.86: a big one)
    CHECK_EQ(holdAtDrop, 0.0f);                                  // ...and lets go exactly on the hit
    CHECK(tsMin < 0.5f);                                         // motion slows nearly to a stop
    CHECK_EQ(liftAt, dropAt);                                    // the chorus look starts on the drop
    CHECK(liftMax > 0.9f);
    CHECK(endAt > dropAt + 10000 && endAt < 125000);             // and eases back out in the calm after it
    // Cinema: the hush drains colour, the drop flash may go to the drop cap
    Config cfg; cfg.director = true; cfg.scene = "blood_moon"; cfg.reactivity = 3;
    Cinema cin;
    MusicState ms; ms.active = true; ms.budget = 0.6f;
    for (int i = 0; i < 200; ++i) cin.step(&ms, cfg, 1.0f / 60);
    float const sat0 = cin.out().saturation;
    ms.hold = 1;
    for (int i = 0; i < 40; ++i) cin.step(&ms, cfg, 1.0f / 60);
    CHECK(cin.out().saturation < sat0 * 0.75f);
    CHECK(cin.out().vignette > 0.25f);
    ms.hold = 0; ms.dropNow = true; ms.bigDrop = true; cin.step(&ms, cfg, 1.0f / 60); ms.dropNow = false;
    CHECK(cin.out().flash > Cinema::kFlashCap && cin.out().flash <= Cinema::kDropFlashCap + 1e-6f);
    cfg.dropMoment = false; ms.hold = 1;
    for (int i = 0; i < 120; ++i) cin.step(&ms, cfg, 1.0f / 60);
    CHECK(cin.out().saturation > sat0 * 0.9f);                   // switched off: no hush
}

TEST(chorus_looks_and_auto_scene) {
    Config cfg;
    cfg.scene = "celestial_dream"; cfg.skyPreset = "aurora";
    auto l = chorusLook(cfg);
    CHECK_EQ(l.sky, std::string("astral"));
    CHECK_EQ(l.letterFx, 10);
    CHECK_EQ(l.exit, int(ExitAnim::Shatter));
    for (auto const& s : builtinScenes()) { // every built-in scene has a chorus look whose sky exists
        cfg.scene = s.id; cfg.skyPreset = s.sky;
        auto k = chorusLook(cfg);
        CHECK(!k.sky.empty());
        CHECK(sky::find(k.sky, cfg).has_value());
        CHECK(k.exit >= 0);
    }
    cfg.scene = ""; // a hand-made look keeps its sky, its letters just break apart
    auto h = chorusLook(cfg);
    CHECK(h.sky.empty());
    CHECK_EQ(h.exit, int(ExitAnim::Shatter));
    StudioScene mine = captureScene(cfg, "mine", "Mine");
    mine.chorusSky = "warp"; mine.chorusFx = 7;
    auto back = sceneFromJson(sceneToJson(mine));
    CHECK(back.has_value());
    CHECK_EQ(back->chorusSky, std::string("warp"));
    CHECK_EQ(back->chorusFx, 7);
    cfg.scene = "mine";
    CHECK_EQ(chorusLook(cfg, {*back}).sky, std::string("warp"));
    // Auto director: stable per song, pools by energy, always a real scene
    std::string a1 = autoSceneFor("artist\x01song a", 0.8f), a2 = autoSceneFor("artist\x01song a", 0.8f);
    CHECK_EQ(a1, a2);
    CHECK(findScene(a1).has_value());
    CHECK(findScene(autoSceneFor("x", 0.1f)).has_value());
    CHECK(findScene(autoSceneFor("x", -1)).has_value());
    CHECK(autoSceneFor("", 0.5f).empty());
    int differ = 0;
    for (int i = 0; i < 20; ++i) differ += autoSceneFor("song " + std::to_string(i), 0.8f) != a1;
    CHECK(differ > 5);                                           // songs don't all get the same scene
    bool calm = false;
    for (auto id : {"celestial_dream", "dreamcore", "melancholy", "dream_ocean", "cinematic_romance", "liquid_paradise"})
        calm |= autoSceneFor("quiet one", 0.1f) == id;
    CHECK(calm);
}

TEST(sky_chorus_wave_and_meteors) {
    using namespace bf::lyrics::sky;
    Config cfg;
    auto p = *find("aurora", cfg);
    Fx fx;
    FxIn in; in.nowMs = 1000; in.dtSec = 1.0 / 60;
    Frame f; f.visible = 1; f.bright = 1;
    AutoPerf perf;
    std::vector<Quad> q0, q1, q2;
    fx.frame(&p, cfg, perf, f, in, q0);
    in.wave = 0.4f;
    fx.frame(&p, cfg, perf, f, in, q1);
    CHECK(q1.size() == q0.size());                               // [LyricsForge] the wave is the shader's now
    in.wave = -1; in.meteors = 6; in.nowMs = 1100;
    fx.frame(&p, cfg, perf, f, in, q2);
    in.meteors = 0;
    size_t before = fx.shootingCount();
    CHECK(before >= 6);
    std::vector<Quad> q3;
    in.nowMs = 1500;
    fx.frame(&p, cfg, perf, f, in, q3);
    CHECK(q3.size() >= q0.size() + 6);                          // they fly
    in.nowMs = 4000;
    std::vector<Quad> q4;
    fx.frame(&p, cfg, perf, f, in, q4);
    CHECK(fx.shootingCount() < before);                          // and burn out
}

TEST(panel_cinema_home) {
    PanelRig r;
    CHECK(r.p.tab() == Panel::Tab::Home);
    // first card = Celestial Dream (206..256, 58..86)
    r.click(206 + 25, 70);
    CHECK_EQ(r.e.config().scene, std::string("celestial_dream"));
    CHECK_EQ(r.e.config().skyPreset, std::string("aurora"));
    // intensity: far right of the track -> 1.6
    r.click(369, 145);
    CHECK_NEAR(r.e.config().intensity, 1.6, 1e-3);
    bool cl = r.e.config().chorusLooks;
    r.click(260 + 40, 164);
    CHECK_EQ(r.e.config().chorusLooks, !cl);
    bool au = r.e.config().autoScene;
    r.click(280, 47);
    CHECK_EQ(r.e.config().autoScene, !au);
    CHECK(PanelRig::has(r.click(240, 182), PanelAction::Kind::SaveScene));
    r.click(320, 182); // Advanced -> FX tab
    CHECK(r.p.tab() == Panel::Tab::Fx);
    // More -> FPS & quality
    r.click(206 + 5 * 27.17f + 12, 210);
    CHECK(r.p.tab() == Panel::Tab::More);
    r.click(320, 152);
    CHECK(r.p.tab() == Panel::Tab::Fps);
    // pager to page 2: the 7th scene
    r.click(206 + 1 * 27.17f + 12, 210); r.click(206 + 12, 210); // Sky, back Home
    r.click(358, 47);
    r.click(206 + 25, 70);
    CHECK_EQ(r.e.config().scene, builtinScenes()[6].id);
}

TEST(only_big_drops_hold_their_breath) { // [BF] real-music tuning: Song Memory finds many small "drops"
    SongMemory m;
    m.reset(SongMemory::keyFor("a\x01b", 120000), 120000);
    SongMemory::Event big; big.at = 30000; big.kind = SongMemory::Event::Kind::Drop; big.strength = 0.9f;
    SongMemory::Event small = big; small.at = 70000; small.strength = 0.5f;
    m.events = {big, small};
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.memory = &m;
    float h1 = 0, h2 = 0; bool big1 = false, big2 = true; int drops = 0;
    for (int64_t t = 0; t < 90000; t += 17) {
        in.posMs = t;
        c.update(in);
        auto const& s = c.state();
        if (t > 29000 && t < 30000) h1 = std::max(h1, s.hold);
        if (t > 69000 && t < 70000) h2 = std::max(h2, s.hold);
        if (s.dropNow) { drops++; (t < 50000 ? big1 : big2) = s.bigDrop; }
    }
    CHECK_EQ(drops, 2);
    CHECK(h1 > 0.6f && big1);
    CHECK(h2 < 0.01f && !big2);
}

TEST(clip_mode_timing) { // [BF] NVIDIA Instant Replay after the big moment
    auto hk = parseHotkey("Alt+F10");
    CHECK_EQ(hk.count, 2); CHECK_EQ(hk.keys[0], 0x12); CHECK_EQ(hk.keys[1], 0x79);
    CHECK_EQ(parseHotkey("ctrl+shift+s").count, 3);
    CHECK_EQ(parseHotkey("alt+banana").count, 0);
    ClipTimer t;
    MusicState m; m.active = true;
    int fired = 0; float at = -1, time = 0;
    auto run = [&](float secs) {
        for (float x = 0; x < secs; x += 1.0f / 60, time += 1.0f / 60) {
            if (t.step(&m, true, 1.0f / 60, 1)) { fired++; if (at < 0) at = time; }
            m.liftNow = m.dropNow = m.chorusNow = m.bigDrop = false;
        }
    };
    run(5);
    m.liftNow = m.dropNow = true; m.bigDrop = false; run(20); // a small drop: no clip
    CHECK_EQ(fired, 0);
    m.liftNow = m.chorusNow = true; run(20);                 // the first chorus: clip 8 s later
    CHECK_EQ(fired, 1);
    CHECK(at > 32.5f && at < 34.0f);                         // chorus at 25 s + 8 s
    m.liftNow = m.dropNow = m.bigDrop = true; run(30);       // too soon after the last one
    CHECK_EQ(fired, 1);
    run(100);
    m.liftNow = m.dropNow = m.bigDrop = true; run(20);       // a big drop, 2 min later: second clip
    CHECK_EQ(fired, 2);
    run(130);
    m.liftNow = m.dropNow = m.bigDrop = true; run(20);       // max two per song
    CHECK_EQ(fired, 2);
    CHECK(!t.step(&m, false, 1.0f / 60, 2));                 // off: nothing
}

TEST(first_listen_sees_the_chorus_coming) { // [BF] no energy map yet: the lyrics' repeated block is the big moment
    std::vector<Timing>      times{{10000, -1}, {14000, -1}, {30000, -1}, {34000, -1}, {38000, -1}, {50000, -1}, {70000, -1}, {74000, -1}, {78000, -1}};
    std::vector<std::string> texts{"first verse line", "second verse line", "we light it up", "keep it going now", "never let it down",
                                   "bridge part", "we light it up", "keep it going now", "never let it down"};
    Lyrics lyr = Lyrics::build(times, texts, true, "test", 100000);
    SongMemory m;
    m.reset(SongMemory::keyFor("a\x01new song", 100000), 100000); // brand new: nothing recorded
    m.analyze(&lyr);
    CHECK(!m.ready());
    int choruses = 0;
    for (auto const& e : m.events) choruses += e.kind == SongMemory::Event::Kind::Chorus;
    CHECK(choruses >= 1);
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.memory = &m;
    float holdMax = 0, imp = 0; bool hit = false, lifted = false;
    for (int64_t t = 0; t < 32000; t += 17) {
        in.posMs = t;
        c.update(in);
        auto const& s = c.state();
        if (t > 29000 && t < 30000) holdMax = std::max(holdMax, s.hold);
        if (s.chorusNow) { hit = true; imp = s.impact; }
        lifted |= s.lifted;
    }
    CHECK(hit);
    CHECK(holdMax > 0.8f);   // the full held breath, first time hearing the song
    CHECK(imp > 0.95f);
    CHECK(lifted);           // and the chorus look
}

TEST(kinetic_words) { // [BF] lyric-video staging: stacked short lines, growing hook words, letters slamming in
    std::vector<Timing>      times{{10000, -1}, {20000, -1}, {30000, -1}, {40000, -1}};
    std::vector<std::string> texts{"we rise", "the night is ours forever", "the night is ours forever", "drop it"};
    Lyrics lyr = Lyrics::build(times, texts, true, "test", 60000);
    Config c; c.lyricsStyle = LyricsStyle::Sky; c.varySizes = false;
    auto cam = testCam();
    auto wfn = [](std::string_view s, std::string_view) { return 0.5f * float(text::length(s)); };
    MusicState m; m.active = true; m.lifted = true; m.section = MusicSection::High;
    SceneInput in;
    in.lyrics = &lyr; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true; in.music = &m;
    Scene sc;
    in.t = 10100;
    sc.tick(in, c, cam, nullptr, wfn);
    CHECK(!sc.visible().empty());
    if (!sc.visible().empty()) {
        auto const& p = sc.visible().back();
        CHECK(p.stacked);
        CHECK_EQ(p.rowsN, 2);                          // one big word per row
        CHECK(p.wsize[0] > 1.4f);
    }
    in.t = 20100; m.lifted = false; m.section = MusicSection::Mid;
    sc.tick(in, c, cam, nullptr, wfn);
    if (!sc.visible().empty()) {
        auto const& p = sc.visible().back();
        CHECK(!p.stacked);
        float mx = 0; int at = -1;
        for (int j = 0; j < p.n(); ++j) if (p.wsize[size_t(j)] > mx) { mx = p.wsize[size_t(j)]; at = j; }
        CHECK(mx > 1.3f);                              // the hook's strongest word grows
        CHECK_EQ(at, 4);                               // "forever"
    }
    m.hold = 0.8f;                                     // the held breath right before the drop line
    in.t = 40020;
    sc.tick(in, c, cam, nullptr, wfn);
    m.hold = 0; m.impact = 1;
    in.t = 40060;
    sc.tick(in, c, cam, nullptr, wfn);
    auto words = sc.draw(in, c, cam, nullptr);
    int singles = 0;
    for (auto const& d : words) singles += text::length(d.text) == 1;
    CHECK(singles >= 2);                               // "drop" arrives as flying letters
    in.t = 41500;
    sc.tick(in, c, cam, nullptr, wfn);
    words = sc.draw(in, c, cam, nullptr);
    bool whole = false;
    for (auto const& d : words) whole |= d.text == "drop";
    CHECK(whole);                                      // ...then the word settles
    Config off = c; off.kineticWords = false;
    Scene s2; MusicState m2; m2.active = true; m2.lifted = true;
    in.music = &m2; in.t = 10100;
    s2.tick(in, off, cam, nullptr, wfn);
    if (!s2.visible().empty()) CHECK(!s2.visible().back().stacked);
}

TEST(showcase_director_cuts) {
    Config cfg; cfg.director = true; cfg.showcase = true;
    Cinema c;
    MusicState m; m.active = true; m.budget = 0.5f;
    for (int i = 0; i < 120; ++i) c.step(&m, cfg, 1.0f / 60, 10.0f);
    CHECK_EQ(c.shot(), 0);
    float const swayR = c.out().orbitRadius;
    m.dropNow = m.liftNow = true; m.lifted = true;
    c.step(&m, cfg, 1.0f / 60, 10.0f);
    m.dropNow = m.liftNow = false;
    CHECK_EQ(c.shot(), 1);                                    // the drop: instantly the low hero shot
    CHECK(std::fabs(c.out().orbitYaw - (10.0f + 160.0f)) < 2.0f); // in front of you, looking back
    CHECK(c.out().orbitRadius < swayR);
    for (int i = 0; i < 60 * 8; ++i) { m.beat = (i % 30) == 0 ? 0.9f : 0.0f; c.step(&m, cfg, 1.0f / 60, 10.0f); }
    CHECK(c.cuts() >= 2);                                     // keeps cutting on beats inside the chorus
    m.lifted = false; m.liftEnd = true; m.beat = 0;
    c.step(&m, cfg, 1.0f / 60, 10.0f); m.liftEnd = false;
    for (int i = 0; i < 40; ++i) c.step(&m, cfg, 1.0f / 60, 10.0f);
    CHECK_EQ(c.shot(), 0);                                    // back to the sway
    cfg.directorCuts = false;
    m.chorusNow = m.lifted = true;
    c.step(&m, cfg, 1.0f / 60, 10.0f);
    for (int i = 0; i < 40; ++i) c.step(&m, cfg, 1.0f / 60, 10.0f);
    CHECK_EQ(c.shot(), 0);                                    // cuts off: no cut
}

TEST(per_song_identity) {
    Config cfg; cfg.scene = "celestial_dream"; cfg.skyPreset = "aurora";
    CHECK_EQ(chorusLook(cfg, {}, 0).sky, std::string("astral"));
    CHECK_EQ(chorusLook(cfg, {}, 1).sky, std::string("prism"));
    for (auto const& s : builtinScenes()) { cfg.scene = s.id; cfg.skyPreset = s.sky; CHECK(sky::find(chorusLook(cfg, {}, 1).sky, cfg).has_value()); }
    int odd = 0;
    for (int i = 0; i < 20; ++i) odd += songVariant("artist\x01song " + std::to_string(i)) & 1;
    CHECK(odd > 3 && odd < 17);
    // the cover tints the grade: a red cover vs a blue cover, same scene
    Config c2; c2.director = true; c2.scene = "melancholy";
    MusicState m; m.active = true; m.budget = 0.5f;
    Cinema red, blue;
    for (int i = 0; i < 300; ++i) { red.step(&m, c2, 1.0f / 60, 0, 0xFF2020); blue.step(&m, c2, 1.0f / 60, 0, 0x2040FF); }
    CHECK(red.out().tint[0] > blue.out().tint[0] + 0.05f);
    CHECK(blue.out().tint[2] > red.out().tint[2] + 0.05f);
    c2.songTint = false;
    Cinema a, b;
    for (int i = 0; i < 300; ++i) { a.step(&m, c2, 1.0f / 60, 0, 0xFF2020); b.step(&m, c2, 1.0f / 60, 0, 0x2040FF); }
    CHECK_NEAR(a.out().tint[0], b.out().tint[0], 1e-4);
}

TEST(song_outro_credits) {
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.durationMs = 120000;
    float o100 = 1, o118 = 0;
    for (int64_t t = 0; t < 120000; t += 17) {
        in.posMs = t;
        c.update(in);
        if (std::llabs(t - 100000) < 9) o100 = c.state().outro;
        if (std::llabs(t - 118000) < 9) o118 = c.state().outro;
    }
    CHECK(o100 < 0.01f);
    CHECK(o118 > 0.8f);
    in.songSeq = 2; in.posMs = 0; c.update(in);
    CHECK_EQ(c.state().outro, 0.0f);                          // a new song starts clean
    Config cfg; cfg.director = true;
    Cinema cin;
    MusicState m; m.active = true; m.budget = 0.5f;
    for (int i = 0; i < 200; ++i) cin.step(&m, cfg, 1.0f / 60);
    float const v0 = cin.out().vignette;
    m.outro = 1;
    for (int i = 0; i < 200; ++i) cin.step(&m, cfg, 1.0f / 60);
    CHECK(cin.out().vignette > v0 + 0.2f);
    CHECK(cin.out().letterbox > 0.05f);
}

TEST(instrumentals_take_the_stage) { // [BF] no singing: the world performs the beat
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.hasLyrics = true; in.vocals = true;
    for (int64_t t = 0; t < 10000; t += 17) { in.posMs = t; c.update(in); }
    CHECK(c.state().instrumental < 0.01f);                       // singing: the words lead
    in.vocals = false;
    for (int64_t t = 10000; t < 12000; t += 17) { in.posMs = t; c.update(in); }
    CHECK(c.state().instrumental < 0.05f);                       // a short breath between lines changes nothing
    for (int64_t t = 12000; t < 20000; t += 17) { in.posMs = t; c.update(in); }
    CHECK(c.state().instrumental > 0.8f);                        // a beat break: the beat takes the stage
    in.vocals = true;
    for (int64_t t = 20000; t < 21500; t += 17) { in.posMs = t; c.update(in); }
    CHECK(c.state().instrumental < 0.1f);                        // the voice is back, quickly
    Conductor c2; in.songSeq = 2; in.hasLyrics = false;
    for (int64_t t = 0; t < 5000; t += 17) { in.posMs = t; c2.update(in); }
    CHECK(c2.state().instrumental > 0.9f);                       // no lyrics at all: from the start
    // Pulse: a ring per kick, the horizon equaliser
    Pulse p;
    MusicState m; m.active = true; m.instrumental = 1; m.bass = 0.8f; m.energy = 0.7f;
    Pulse::In pi; pi.music = &m;
    for (int i = 0; i < 120; ++i) { m.beat = (i % 30) < 3 ? 0.9f : 0.1f; pi.now = i / 60.0; p.step(pi); }
    CHECK(p.rings() >= 1 && p.rings() <= 8);
    CHECK(p.level() > 0.5f);
    std::vector<BfGlyphV1> out;
    p.emit(pi, out);
    CHECK(out.empty());                                          // [LyricsForge] no ground rings (they cut through terrain)
    CHECK(p.barEl().size() >= 40 && p.barAlpha() > 0.1f);         // the equaliser goes to the sky (behind the terrain)
    { float lo = 99, hi = 0; for (float e : p.barEl()) { lo = std::min(lo, e); hi = std::max(hi, e); }
      CHECK(lo > 2.5f && hi < 32.0f && hi > lo); }
    m.instrumental = 0;
    for (int i = 0; i < 240; ++i) p.step(pi);
    out.clear(); p.emit(pi, out);
    CHECK(out.empty());                                          // the voice came back: it all fades away
    CHECK(p.barEl().empty());
}

TEST(spectrum_16_bands) {
    auto run = [](double hz) {
        BandAnalyzer a(48000);
        std::vector<float> buf(4800);
        for (int blk = 0; blk < 10; ++blk) {
            for (size_t i = 0; i < buf.size(); ++i) buf[i] = float(0.5 * std::sin(6.283185307 * hz * double(blk * 4800 + int(i)) / 48000.0));
            a.feed(buf.data(), buf.size(), int64_t(blk) * 100000000);
        }
        BandFrame f = a.latest();
        int best = 0;
        for (int b = 1; b < 16; ++b) if (f.spec[b] > f.spec[best]) best = b;
        return std::pair<int, BandFrame>{best, f};
    };
    auto [lowB, lowF] = run(60);
    auto [midB, midF] = run(1000);
    auto [hiB, hiF] = run(8000);
    std::printf("spectrum peaks: 60 Hz -> %d, 1 kHz -> %d, 8 kHz -> %d\n", lowB, midB, hiB);
    CHECK(lowF.hasSpec);
    CHECK(lowB <= 2);
    CHECK(midB >= 7 && midB <= 10);
    CHECK(hiB >= 12);
    CHECK(midF.spec[midB] > midF.spec[2] + 0.2f);
}

TEST(moments_fit_the_sky) { // [BF] chorus deck, song arc, lyric keywords
    // every sky the panel shows belongs to a family; every deck has 3 moments, smallest first
    Config cfg;
    for (auto id : sky::order()) { auto const& d = chorusDeck(skyFamily(id)); CHECK_EQ(d.size(), size_t(3)); }
    CHECK(skyFamily("black_hole") == SkyFamily::Space);
    CHECK(skyFamily("sakura") == SkyFamily::Warm);
    // nothing that doesn't belong: no petals or rain in space, no snow in a storm
    for (auto x : chorusDeck(SkyFamily::Space)) CHECK(x != Moment::Petals && x != Moment::Rain);
    CHECK(keywordMoment("rain", SkyFamily::Space) == Moment::Starfall);   // rain in deep space falls as starlight
    CHECK(keywordMoment("Rain,", SkyFamily::Warm) == Moment::Rain);
    CHECK(keywordMoment("fire", SkyFamily::Cold) == Moment::Embers);
    CHECK(keywordMoment("roses", SkyFamily::Space) == Moment::Fireflies);
    CHECK(keywordMoment("heart", SkyFamily::Neon) == Moment::Heartbeat);
    CHECK(keywordMoment("the", SkyFamily::Warm) == Moment::None);
    // three choruses: different moments, the last one plays the whole deck
    Moments mo;
    MusicState m; m.active = true; m.chorusTotal = 3;
    MomentsIn in; in.music = &m; in.sky = "sakura"; in.songSeq = 1; in.variant = 0;
    std::vector<std::string> events;
    for (int k = 1; k <= 3; ++k) {
        m.choruses = k; m.liftNow = true; m.lifted = true; m.finalHit = k == 3;
        mo.step(in);
        events.push_back(mo.out().event);
        m.liftNow = false; m.finalHit = false;
        for (int i = 0; i < 120; ++i) mo.step(in);
        if (k == 1) CHECK(mo.out().weather == Weather::Fireflies && mo.out().weatherAmt > 0.4f);
        m.lifted = false; m.liftEnd = true; mo.step(in); m.liftEnd = false;
        for (int i = 0; i < 300; ++i) mo.step(in);
        CHECK(mo.active() == 0);                                     // each eases out after its chorus
    }
    CHECK(events[0] != events[1]);
    CHECK(events[2].find("final chorus") != std::string::npos);
    CHECK(std::count(events[2].begin(), events[2].end(), ',') >= 2); // everything at once
    // keywords: "fire" sung -> embers, then a cooldown
    Moments k2;
    std::vector<Timing>      times{{1000, -1}, {3000, -1}, {5000, -1}};
    std::vector<std::string> texts{"we set the fire", "fire again", "under the stars"};
    Lyrics lyr = Lyrics::build(times, texts, true, "test", 10000);
    MomentsIn ki; MusicState km; km.active = true;
    ki.music = &km; ki.lyrics = &lyr; ki.sky = "aquarell"; ki.songSeq = 1;
    int embers = 0, stars = 0;
    for (int64_t t = 0; t < 9000; t += 17) {
        ki.t = t; k2.step(ki);
        auto const& e = k2.out().event;
        embers += e.find("embers") != std::string::npos;
        stars += e.find("fireflies") != std::string::npos;
    }
    CHECK_EQ(embers, 1);                                              // the second "fire" is inside the cooldown
    CHECK_EQ(stars, 1);                                               // "stars" under a warm sky: golden fireflies
    // line-timed sources (one "word" = the whole line, seen in game with NetEase): the keyword still lands in time
    Lyrics one = lyr;
    for (auto& l : one.lines) { if (l.words.empty()) continue; l.words.resize(1); l.words[0].text = l.text; l.words[0].end = l.end; }
    Moments k3; int hits = 0;
    for (int64_t t = 0; t < 9000; t += 17) { ki.t = t; ki.lyrics = &one; k3.step(ki); hits += !k3.out().event.empty(); }
    CHECK(hits >= 2);
}

TEST(song_arc_total_final_bridge) {
    SongMemory m;
    m.reset(SongMemory::keyFor("arc\x01song", 200000), 200000);
    for (int64_t at : {40000, 80000, 150000}) m.events.push_back({at, -1, SongMemory::Event::Kind::Chorus, 0.7f});
    Conductor c;
    ConductorIn in;
    in.playing = true; in.songSeq = 1; in.dt = 1.0f / 60.0f; in.memory = &m; in.durationMs = 200000;
    int finals = 0, hits = 0; float bridgeMax = 0; int total = 0;
    for (int64_t t = 0; t < 160000; t += 17) {
        in.posMs = t;
        c.update(in);
        auto const& s = c.state();
        total = s.chorusTotal;
        hits += s.chorusNow; finals += s.finalHit;
        if (t > 110000 && t < 145000) bridgeMax = std::max(bridgeMax, s.bridge);
    }
    CHECK_EQ(total, 3);
    CHECK_EQ(hits, 3);
    CHECK_EQ(finals, 1);                                              // only the last one
    CHECK(bridgeMax > 0.6f);                                          // between chorus 2 and the last
}

TEST(normalize_words_from_line_timed_sources) { // [BF] NetEase "words" can be one per line, with NBSP spaces (seen in game)
    std::string const j = "{\"v\":1,\"source\":\"netease words\",\"synced\":true,\"lines\":[{\"t\":\"I\xC2\xA0was\xC2\xA0wandering in the rain\",\"s\":1000,\"e\":4600,\"est\":false,\"w\":[[\"I\xC2\xA0was\xC2\xA0wandering in the rain\",1000,4600]]}]}";
    auto l = fromCacheJson(j);
    CHECK(l.has_value());
    if (!l) return;
    auto const& ln = l->lines[0];
    CHECK_EQ(ln.text, std::string("I was wandering in the rain"));
    CHECK_EQ(ln.words.size(), size_t(6));
    CHECK_EQ(ln.words.back().text, std::string("rain"));
    CHECK(ln.words.back().start > 3500 && ln.words.back().end == 4600);
    for (size_t i = 1; i < ln.words.size(); ++i) CHECK(ln.words[i].start >= ln.words[i - 1].start);
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

TEST(tempo_from_lyrics) { // [LyricsForge] the backup groove locks to the words' beat grid
    Lyrics ly;
    double const beat = 60000.0 / 96.0, phase = 140;
    uint32_t r = 12345;
    for (int li = 0; li < 24; ++li) {
        Line l; l.index = li; l.estimated = false;
        for (int w = 0; w < 6; ++w) {
            r = r * 1103515245u + 12345u;
            int const slot = li * 8 + w + int((r >> 16) % 2);               // on beats, sometimes skipping one
            double const half = ((r >> 20) & 3) == 0 ? beat / 2 : 0;          // a few syllables on the off-beat
            int64_t const t = int64_t(phase + slot * beat + half + double(int((r >> 8) % 41) - 20)); // +-20 ms sloppy
            l.words.push_back(Word::of("w", t, t + 200));
        }
        l.start = l.words.front().start; l.end = l.words.back().end;
        ly.lines.push_back(l);
    }
    auto g = guessTempo(ly);
    CHECK_NEAR(float(g.bpm), 96.0f, 0.6f);
    double d = std::fmod(std::fabs(g.phaseMs - phase), beat); d = std::min(d, beat - d);
    CHECK(d < 40.0);
    CHECK(g.confidence > 0.3f);
    Lyrics few; few.lines.resize(3);
    CHECK(guessTempo(few).bpm == 0.0);
}
