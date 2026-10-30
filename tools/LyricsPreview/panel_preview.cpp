// LyricsFlow port - offline preview of the world panel (Panel.cpp, LyricsFlow 2.1 WorldPanel) with lyrics around it in
// a fake world. Dumps frames (same JSON as preview.cpp) for render_clip.py. Original code for Bedrock Forge.
// usage: panel_preview <repo> <out dir> <tab 0..4> [aim_px_x aim_px_y] [cover.png] [ms...]
//   aim = where the crosshair points on the panel (design px); ms = panel ages to dump (default 1700)
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "lyrics/core/Engine.h"
#include "lyrics/core/Json.h"
#include "lyrics/core/Lyrics.h"
#include "lyrics/core/Panel.h"

using namespace bf::lyrics;

namespace {
std::string readFile(std::string const& p) {
    std::ifstream     f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
struct FlatWorld : IWorld {
    RayHit clip(Vec3 from, Vec3 to, RayShape, bool) override {
        if (to.y >= 64 || from.y < 64) return {};
        double t = (from.y - 64) / (from.y - to.y);
        return {true, from + (to - from) * t, Face::Up};
    }
    bool isAir(BlockPos p) override { return p.y >= 64; }
    void collisionBoxes(BlockPos p, std::vector<Aabb>& out) override { if (p.y < 64) out.push_back(Aabb{}); }
    bool hasFluid(BlockPos) override { return false; }
    bool canSeeSky(BlockPos p) override { return p.y >= 64; }
    int  skyLight(BlockPos) override { return 15; }
};
Vec3 lookOf(float yaw, float pitch) {
    double y = yaw * 3.14159265358979 / 180, p = pitch * 3.14159265358979 / 180;
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::string repo = argv[1], out = argv[2];
    int         tab  = std::atoi(argv[3]);
    bool        aim  = argc > 5 && std::string(argv[4]) != "-";
    float       ax = aim ? float(std::atof(argv[4])) : 0, ay = aim ? float(std::atof(argv[5])) : 0;
    std::string cover = argc > 6 ? argv[6] : "";
    std::vector<int> ages;
    for (int i = 7; i < argc; ++i) ages.push_back(std::atoi(argv[i]));
    if (ages.empty()) ages.push_back(1700);
    std::filesystem::create_directories(out);

    Engine e;
    std::string fdir = repo + "/resources/lyricsflow/fonts/";
    std::vector<std::string> files;
    std::vector<std::filesystem::path> fj;
    for (auto const& entry : std::filesystem::directory_iterator(fdir))
        if (entry.path().extension() == ".json") fj.push_back(entry.path());
    std::sort(fj.begin(), fj.end());
    for (auto const& p : fj) {
        std::string id = p.stem().string();
        if (e.fonts().loadFromJson(id, readFile(p.string()), fdir + id + ".png")) files.push_back(fdir + id + ".png");
    }
    PanelAssets as;
    as.uiAtlas = int(files.size());
    files.push_back(repo + "/resources/lyricsflow/ui/ui_atlas.png");
    if (auto j = json::parse(readFile(repo + "/resources/lyricsflow/ui/ui_atlas.json")))
        for (auto const& [k, v] : (*j)["rects"].members())
            as.ui[k] = UiRect{float(v[0].asNumber()), float(v[1].asNumber()), float(v[2].asNumber()), float(v[3].asNumber())};
    if (!cover.empty() && cover != "-") { as.cover = int(files.size()); files.push_back(cover); }

    if (auto pj = json::parse(readFile(repo + "/resources/lyricsflow/presets/neon_arena.json"))) applyJson(e.config(), (*pj)["settings"]);
    e.config().skyPreset = "inferno";
    FlatWorld  w;
    Lyrics     l = *parseLrc(readFile(repo + "/tests/lyrics/fixtures/song.lrc"), 200000);
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
    in.title = "Green Room"; in.artist = "Ken Carson"; in.guiW = 480; in.guiH = 270; in.dt = 1.0f / 30;

    // the camera looks slightly down at the panel, like the teaser shot
    CameraState cam;
    cam.pos = {0.5, 65.62, 0.5}; cam.yaw = 0; cam.pitch = 12; cam.screenAspect = 16.0f / 9.0f;
    Panel p;
    p.setTab(Panel::Tab(tab));
    p.open(cam.pos, lookOf(0, 0), 0);
    PanelMedia media;
    media.title = "Green Room"; media.artist = "Ken Carson"; media.sourceApp = "Spotify"; media.hasTrack = true;
    media.positionMs = 109000; media.durationMs = 188000; media.playing = true;

    int dumped = 0;
    std::vector<BfGlyphV1> panelGlyphs;
    for (int ms = 0, i = 0; ms <= ages.back(); ms += 33, ++i) {
        in.t = 12400 + ms; in.nowSeconds = ms / 1000.0;
        e.frame(in, cam, &w);
        PanelInput pin;
        pin.eye = cam.pos; pin.nowMs = ms; pin.dt = 1.0f / 30;
        pin.look = lookOf(cam.yaw, cam.pitch);
        if (aim) { // point the crosshair at panel px (ax, ay)
            Vec3 t = p.toWorld(ax, ay) - cam.pos;
            pin.look = t.normalized();
        }
        pin.up = Vec3{0, 1, 0};
        panelGlyphs.clear();
        p.frame(pin, cam.pos, e.config(), e.fonts(), as, media, panelGlyphs);
        bool want = false;
        for (int a : ages) want |= ms <= a && a < ms + 33;
        if (!want) continue;
        char name[64];
        std::snprintf(name, sizeof name, "/f%04d.json", dumped++);
        std::ofstream o(out + name);
        o << "{\"yaw\":" << cam.yaw << ",\"pitch\":" << cam.pitch << ",\"fov\":70,\"aspect\":1.7777778,\"eye\":[0.5,65.62,0.5],\"ground\":64,\"boxes\":[],\"atlases\":[";
        for (size_t a = 0; a < files.size(); ++a) o << (a ? "," : "") << "\"" << files[a] << "\"";
        o << "],\"glyphs\":[";
        bool first = true;
        auto dump  = [&](BfGlyphV1 const& g) {
            o << (first ? "" : ",") << "[" << g.pos[0] << "," << g.pos[1] << "," << g.pos[2] << "," << g.right[0] << "," << g.right[1] << ","
              << g.right[2] << "," << g.up[0] << "," << g.up[1] << "," << g.up[2] << "," << g.uv[0] << "," << g.uv[1] << "," << g.uv[2] << ","
              << g.uv[3] << "," << g.fill << "," << g.fill2 << "," << g.outline << "," << g.glow << "," << g.alpha << "," << g.outlineW << ","
              << g.glowW << "," << g.glowK << "," << g.atlas << "," << g.flags << "," << g.pxRange << "]";
            first = false;
        };
        for (auto const& g : e.glyphs()) dump(g);
        for (auto const& g : panelGlyphs) dump(g);
        o << "]}\n";
        std::printf("age %d ms: %zu panel glyphs, hovered %d hot '%s'\n", ms, panelGlyphs.size(), int(p.hovered()), p.hot().c_str());
    }
}
