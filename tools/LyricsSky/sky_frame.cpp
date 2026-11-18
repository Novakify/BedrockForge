// Simulates the sky layers (SkyFx) for a preset and camera and dumps the quads of the last frame as JSON, for
// compose_sky.py (draws them over render_sky.js's dome). Args: preset yaw pitch seconds [seed]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "lyrics/core/Config.h"
#include "lyrics/core/Perf.h"
#include "lyrics/core/SkyFx.h"

using namespace bf::lyrics;

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: sky_frame preset yaw pitch seconds [seed]\n"); return 2; }
    Config cfg;
    auto   p = sky::find(argv[1], cfg);
    if (!p) return 1;
    double yaw = std::atof(argv[2]), pitch = std::atof(argv[3]), secs = std::atof(argv[4]);
    sky::Fx    fx;
    if (argc > 5) fx.seed(std::atoll(argv[5]));
    sky::Clock clk;
    AutoPerf   perf;
    sky::Clock::In ci;
    ci.dtSec = 1.0 / 60;
    sky::FxIn in;
    in.cam = sky::Cam::of(yaw, pitch);
    in.dtSec = ci.dtSec;
    std::vector<sky::Quad> out;
    sky::Frame f{};
    int frames = int(secs * 60);
    for (int i = 0; i < frames; ++i) {
        f = clk.tick(&*p, cfg, ci);
        in.nowMs += 16;
        out.clear();
        fx.frame(&*p, cfg, perf, f, in, out);
    }
    std::printf("{\"t\": %.4f, \"quads\": [", f.t);
    for (size_t i = 0; i < out.size(); ++i) {
        auto const& q = out[i];
        std::printf("%s\n[%d,%d", i ? "," : "", int(q.layer), int(q.tex));
        for (auto const& v : q.v) std::printf(",%g,%g,%g,%g,%g,%u,%g", v.x, v.y, v.z, v.u, v.v, v.rgb, v.a);
        std::printf("]");
    }
    std::printf("]}\n");
}
