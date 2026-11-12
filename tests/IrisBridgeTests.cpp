// IrisBridge off-Windows tests: bundle loader + uniform VM parity with tools/IrisBridge/expr.py.
//   g++ -std=c++20 -O1 -Isrc tests/IrisBridgeTests.cpp -o /tmp/ibt && /tmp/ibt <bundle>/runtime.txt
// Prints "<frame> <slot name> <value>" for frames 0, 10, 99 with a scripted input sequence (same as the Python side).
#include <cstdio>
#include <cmath>
#include "IrisBundle.h"

int main(int argc, char** argv) {
    rf::iris::Bundle b;
    if (argc < 2 || !b.load(argv[1])) { std::printf("load failed: %s\n", b.error.c_str()); return 1; }
    std::printf("passes %zu slots %zu inputs %zu\n", b.passes.size(), b.vm.slots.size(), b.vm.inputs.size());
    for (int f = 0; f < 100; ++f) {
        std::vector<double> in(b.vm.inputs.size(), 0.0);
        for (size_t i = 0; i < in.size(); ++i) {
            auto const& n = b.vm.inputs[i];
            double v = 0;
            if (n == "frameCounter") v = f;
            else if (n == "rainStrength") v = f < 50 ? 1.0 : 0.0;
            else if (n == "eyeBrightness.y") v = f < 30 ? 240 : 60;
            else if (n == "eyeAltitude") v = 70;
            else if (n == "biome_precipitation") v = f < 40 ? 1 : 2;
            else if (n == "frameTime") v = 0.016 + 0.001 * (f % 3);
            else if (n == "cameraPosition.x") v = 0.05 * f;
            else if (n == "previousCameraPosition.x") v = 0.05 * (f - 1);
            else if (n == "blindness") v = 0.25;
            else if (n == "darknessFactor") v = f % 2 ? 0.5 : 0.1;
            in[i] = v;
        }
        b.vm.run(in, 0.05);
        if (f == 0 || f == 10 || f == 99)
            for (size_t s = 0; s < b.vm.slots.size(); ++s)
                if (b.vm.slots[s].exported) std::printf("%d %s %.9g\n", f, b.vm.slots[s].name.c_str(), b.vm.value(s));
    }
    return 0;
}
