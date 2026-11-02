// Prints what RF learns from the sky materials: g++ -std=c++20 -Isrc tools/sky_scan_test.cpp && ./a.out <materials dir>
#include "SkyMaterialScan.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    int total = 0;
    for (auto name : rf::skyscan::kMaterials) {
        std::ifstream f(std::string(argv[1]) + "/" + std::string(name) + ".material.bin", std::ios::binary);
        if (!f) { std::printf("missing %.*s\n", int(name.size()), name.data()); continue; }
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        std::set<uint64_t> seen;
        for (auto& s : rf::skyscan::scan(d.data(), d.size()))
            if (int k = rf::skyscan::kindFor(name, s); k && seen.insert(s.hash).second) {
                std::printf("0x%016llxull kind %d  %.*s/%s %s\n", (unsigned long long)s.hash, k, int(name.size()), name.data(), s.pass.c_str(), s.platform.c_str());
                ++total;
            }
    }
    std::printf("%d sky shaders\n", total);
}
