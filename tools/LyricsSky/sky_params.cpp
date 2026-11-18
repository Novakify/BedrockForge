// Dumps the live-sky parameter rows (Sky::pack) for every preset as JSON, for the WebGL preview harness.
//   g++ -std=c++20 -Isrc src/lyrics/core/*.cpp tools/LyricsSky/sky_params.cpp -o sky_params && ./sky_params 12.5
#include <cstdio>
#include <cstdlib>

#include "lyrics/core/Config.h"
#include "lyrics/core/Sky.h"

using namespace bf::lyrics;

int main(int argc, char** argv) {
    double time = argc > 1 ? std::atof(argv[1]) : 1000.0;
    Config cfg;
    float  id3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::printf("{");
    bool first = true;
    for (auto const& p : sky::presets()) {
        auto pr = *sky::find(p.id, cfg);
        auto q  = sky::pack(pr, sky::aurora1(pr, cfg), sky::aurora2(pr, cfg), time, cfg, id3);
        auto b = sky::body(pr);
        std::printf("%s\n \"%s\": {\"az\": %g, \"el\": %g, \"p\": [", first ? "" : ",", p.id, b ? b->az : -1.0, b ? b->el : 0.0);
        first = false;
        for (int i = 0; i < 16; ++i)
            std::printf("%s%d,%d,%d,%d", i ? "," : "", q[i][0], q[i][1], q[i][2], q[i][3]);
        std::printf("]}");
    }
    std::printf("\n}\n");
}
