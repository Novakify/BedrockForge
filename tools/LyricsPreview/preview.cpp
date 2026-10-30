// LyricsFlow port - offline preview: runs the real core Engine (placement, animation, glyph building, falling-letter
// physics) through a short scripted walk in a fake voxel world and dumps every frame as JSON for render_clip.py.
// Lets presets be tuned against the reference videos without the game. Original code for Bedrock Forge.
//
// build: g++ -std=c++20 -O2 -Isrc src/lyrics/core/*.cpp tools/LyricsPreview/preview.cpp -o lyrics_preview
// usage: lyrics_preview <repo root> <preset id[,overlay id]> <scene: field|arena> <out dir> [seconds=6] [fps=20]
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "lyrics/core/Engine.h"
#include "lyrics/core/Json.h"
#include "lyrics/core/Lyrics.h"

using namespace bf::lyrics;

namespace {
std::string readFile(std::string const& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Same fake world as the unit tests: full blocks below groundTop, plus extra boxes.
struct FakeWorld : IWorld {
    std::map<std::tuple<int, int, int>, Aabb> extra;
    int groundTop = 64;
    bool box(BlockPos p, Aabb& b) const {
        auto it = extra.find({p.x, p.y, p.z});
        if (it != extra.end()) { b = it->second; return true; }
        if (p.y < groundTop) { b = Aabb{}; return true; }
        return false;
    }
    RayHit clip(Vec3 from, Vec3 to, RayShape, bool) override {
        Vec3 d = to - from;
        double len = d.length();
        if (len < 1e-9) return {};
        int  steps = int(len / 0.05) + 1;
        Vec3 prev  = from;
        for (int i = 1; i <= steps; ++i) {
            Vec3 p = from + d * (double(i) / steps);
            BlockPos bp = BlockPos::of(p);
            Aabb b;
            if (box(bp, b)) {
                double lx = p.x - bp.x, ly = p.y - bp.y, lz = p.z - bp.z;
                if (lx >= b.x0 && lx <= b.x1 && ly >= b.y0 && ly <= b.y1 && lz >= b.z0 && lz <= b.z1) {
                    BlockPos pb = BlockPos::of(prev);
                    Face f = Face::Up;
                    if (pb.y > bp.y) f = Face::Up;
                    else if (pb.y < bp.y) f = Face::Down;
                    else if (pb.x < bp.x) f = Face::West;
                    else if (pb.x > bp.x) f = Face::East;
                    else if (pb.z < bp.z) f = Face::North;
                    else if (pb.z > bp.z) f = Face::South;
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
};

void addBox(FakeWorld& w, int x0, int x1, int y0, int y1, int z0, int z1, Aabb b = Aabb{}) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) w.extra[{x, y, z}] = b;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <repo> <preset[,overlay]> <field|arena> <outdir> [seconds] [fps]\n", argv[0]);
        return 2;
    }
    std::string repo = argv[1], presets = argv[2], scene = argv[3], out = argv[4];
    double seconds = argc > 5 ? std::atof(argv[5]) : 6.0;
    int    fps     = argc > 6 ? std::atoi(argv[6]) : 20;
    std::filesystem::create_directories(out);

    Engine e;
    std::string fdir = repo + "/resources/lyricsflow/fonts/";
    for (auto const& entry : std::filesystem::directory_iterator(fdir)) {
        if (entry.path().extension() != ".json") continue;
        std::string id = entry.path().stem().string();
        e.fonts().loadFromJson(id, readFile(entry.path().string()), fdir + id + ".png");
    }
    std::stringstream ps(presets);
    for (std::string id; std::getline(ps, id, ',');) {
        auto pj = json::parse(readFile(repo + "/resources/lyricsflow/presets/" + id + ".json"));
        if (!pj) { std::fprintf(stderr, "bad preset %s\n", id.c_str()); return 1; }
        applyJson(e.config(), (*pj)["settings"]);
    }

    // World: flat ground (y < 64). arena = walls on three sides + a few steps/slabs to land on.
    FakeWorld w;
    std::vector<std::array<int, 6>> boxes; // for the renderer (inclusive block ranges)
    auto wall = [&](int x0, int x1, int y0, int y1, int z0, int z1) { addBox(w, x0, x1, y0, y1, z0, z1); boxes.push_back({x0, x1, y0, y1, z0, z1}); };
    if (scene == "arena") {
        wall(-9, 9, 64, 69, 9, 9);   // front wall
        wall(-9, -9, 64, 69, -2, 8); // left
        wall(9, 9, 64, 69, -2, 8);   // right
        wall(-3, -2, 64, 64, 4, 5);  // a low block step
    } else {
        wall(3, 4, 64, 64, 4, 6);    // a couple of blocks to land on
        wall(-5, -5, 64, 65, 6, 6);
    }

    std::string lrc = readFile(repo + "/tests/lyrics/fixtures/song.lrc");
    Lyrics l = *parseLrc(lrc, 200000);
    SceneInput in;
    in.lyrics = &l; in.lyricsState = LyricsState::Found; in.songSeq = 1; in.playing = true;
    in.title = "Test Song"; in.artist = "Test Artist"; in.guiW = 480; in.guiH = 270;
    in.dt = 1.0f / fps;

    float const fov = 70.0f, aspect = 16.0f / 9.0f;
    int const   n   = int(seconds * fps);
    // warm-up: run 1.5 s of song before the first dumped frame so a line is already up
    int64_t const t0 = 12000;
    for (int i = 0; i < n; ++i) {
        double s = double(i) / fps;
        CameraState cam;
        cam.pos   = {0.5 + 0.6 * std::sin(s * 0.7), 65.62, 0.5 + 0.25 * s}; // slow walk forward + strafe
        cam.yaw   = float(12.0 * std::sin(s * 0.5));
        cam.pitch = float(e.config().lyricsStyle == LyricsStyle::Sky ? -18.0 : 4.0 + 3.0 * std::sin(s * 0.9));
        cam.screenAspect = aspect;
        in.t          = t0 + int64_t(s * 1000.0);
        in.nowSeconds = s;
        e.frame(in, cam, &w);

        char name[64];
        std::snprintf(name, sizeof name, "/f%04d.json", i);
        std::ofstream o(out + name);
        o << "{\"yaw\":" << cam.yaw << ",\"pitch\":" << cam.pitch << ",\"fov\":" << fov << ",\"aspect\":" << aspect
          << ",\"eye\":[" << cam.pos.x << "," << cam.pos.y << "," << cam.pos.z << "],\"ground\":" << w.groundTop << ",\"scene\":\"" << scene
          << "\",\"boxes\":[";
        for (size_t b = 0; b < boxes.size(); ++b) {
            o << (b ? "," : "") << "[";
            for (int k = 0; k < 6; ++k) o << (k ? "," : "") << boxes[b][k];
            o << "]";
        }
        o << "],\"atlases\":[";
        for (size_t a = 0; a < e.fonts().all().size(); ++a) o << (a ? "," : "") << "\"" << e.fonts().all()[a].atlasPath << "\"";
        o << "],\"glyphs\":[";
        bool first = true;
        for (auto const& g : e.glyphs()) {
            o << (first ? "" : ",") << "[" << g.pos[0] << "," << g.pos[1] << "," << g.pos[2] << "," << g.right[0] << "," << g.right[1] << ","
              << g.right[2] << "," << g.up[0] << "," << g.up[1] << "," << g.up[2] << "," << g.uv[0] << "," << g.uv[1] << "," << g.uv[2] << ","
              << g.uv[3] << "," << g.fill << "," << g.fill2 << "," << g.outline << "," << g.glow << "," << g.alpha << "," << g.outlineW << ","
              << g.glowW << "," << g.glowK << "," << g.atlas << "," << g.flags << "," << g.pxRange << "]";
            first = false;
        }
        o << "]}\n";
    }
    std::printf("%d frames -> %s (letters alive at end: %zu)\n", n, out.c_str(), e.physics().letters().size());
    return 0;
}
