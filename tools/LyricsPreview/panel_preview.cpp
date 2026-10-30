// LyricsFlow port - offline preview of the settings panel (Panel.cpp) over the lyrics Engine in a fake world.
// Dumps one frame JSON (same format as preview.cpp) for render_clip.py --still 0. Original code for Bedrock Forge.
// usage: panel_preview <repo> <out dir> <tab 0..4> [mouse_px_x mouse_px_y] [cover.png] [preset]
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
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::string repo = argv[1], out = argv[2];
    int         tab  = std::atoi(argv[3]);
    bool        haveMouse = argc > 5;
    float       mpx = haveMouse ? float(std::atof(argv[4])) : 0, mpy = haveMouse ? float(std::atof(argv[5])) : 0;
    std::string cover  = argc > 6 ? argv[6] : "";
    std::string preset = argc > 7 ? argv[7] : "neon_arena";
    std::filesystem::create_directories(out);

    Engine e;
    std::string fdir = repo + "/resources/lyricsflow/fonts/";
    std::vector<std::string> files;
    for (auto const& entry : std::filesystem::directory_iterator(fdir)) {
        if (entry.path().extension() != ".json") continue;
        std::string id = entry.path().stem().string();
        if (e.fonts().loadFromJson(id, readFile(entry.path().string()), fdir + id + ".png")) files.push_back(fdir + id + ".png");
    }
    PanelAssets as;
    as.uiAtlas = int(files.size());
    files.push_back(repo + "/resources/lyricsflow/ui/ui_atlas.png");
    if (auto j = json::parse(readFile(repo + "/resources/lyricsflow/ui/ui_atlas.json")))
        for (auto const& [k, v] : (*j)["rects"].members())
            as.ui[k] = UiRect{float(v[0].asNumber()), float(v[1].asNumber()), float(v[2].asNumber()), float(v[3].asNumber())};
    if (!cover.empty()) { as.cover = int(files.size()); files.push_back(cover); }

    if (auto pj = json::parse(readFile(repo + "/resources/lyricsflow/presets/" + preset + ".json"))) applyJson(e.config(), (*pj)["settings"]);
    e.config().skyPreset = "inferno";
    FlatWorld  w;
    Lyrics     l = *parseLrc(readFile(repo + "/tests/lyrics/fixtures/song.lrc"), 200000);
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
    in.title = "Green Room"; in.artist = "Ken Carson"; in.guiW = 480; in.guiH = 270; in.dt = 1.0f / 30;
    CameraState cam;
    cam.pos = {0.5, 65.62, 0.5}; cam.pitch = 2; cam.screenAspect = 16.0f / 9.0f;

    Panel p;
    p.setTab(Panel::Tab(tab));
    p.setOpen(true);
    PanelMedia media;
    media.title = "Green Room"; media.artist = "Ken Carson"; media.sourceApp = "Spotify"; media.hasTrack = true;
    media.positionMs = 109000; media.durationMs = 188000; media.playing = true;
    std::vector<PresetInfo> presets{{"ground_drop", "Ground Drop"}, {"aurora_cursive", "Aurora Cursive"}, {"neon_arena", "Neon Arena"},
                                    {"wall_coral", "Wall Coral"},   {"wall_gold", "Wall Gold"},           {"combat_safe", "Combat Safe"}};
    std::vector<BfGlyphV1> panelGlyphs;
    for (int i = 0; i < 40; ++i) {
        in.t = 12400 + i * 33; in.nowSeconds = i / 30.0;
        e.frame(in, cam, &w);
        PanelInput pin;
        pin.aspect = 16.0f / 9.0f; pin.dt = 1.0f / 30;
        if (haveMouse) { // design px -> ndc through the panel's own mapping (what a real cursor there would give)
            Vec3 v = p.toView(mpx, mpy);
            pin.mouseValid = true;
            pin.mouseNdcX  = float(v.x / v.z / pin.aspect);
            pin.mouseNdcY  = float(v.y / v.z);
        }
        panelGlyphs.clear();
        p.frame(pin, e.config(), e.fonts(), as, media, presets, panelGlyphs);
    }
    std::ofstream o(out + "/f0000.json");
    o << "{\"yaw\":0,\"pitch\":2,\"fov\":70,\"aspect\":1.7777778,\"eye\":[0.5,65.62,0.5],\"ground\":64,\"boxes\":[],\"atlases\":[";
    for (size_t a = 0; a < files.size(); ++a) o << (a ? "," : "") << "\"" << files[a] << "\"";
    o << "],\"glyphs\":[";
    bool first = true;
    auto dump  = [&](BfGlyphV1 const& g) {
        o << (first ? "" : ",") << "[" << g.pos[0] << "," << g.pos[1] << "," << g.pos[2] << "," << g.right[0] << "," << g.right[1] << "," << g.right[2]
          << "," << g.up[0] << "," << g.up[1] << "," << g.up[2] << "," << g.uv[0] << "," << g.uv[1] << "," << g.uv[2] << "," << g.uv[3] << ","
          << g.fill << "," << g.fill2 << "," << g.outline << "," << g.glow << "," << g.alpha << "," << g.outlineW << "," << g.glowW << ","
          << g.glowK << "," << g.atlas << "," << g.flags << "," << g.pxRange << "]";
        first = false;
    };
    for (auto const& g : e.glyphs()) dump(g);
    for (auto const& g : panelGlyphs) dump(g);
    o << "]}\n";
    std::printf("panel glyphs %zu, hovered '%s'\n", panelGlyphs.size(), p.hovered().c_str());
}
