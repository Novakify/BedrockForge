// Renderer Foundation - LyricsFlow port: find Bedrock's sky pixel shaders inside a RenderDragon .material.bin.
// Original code for Renderer Foundation. Header-only, no Windows dependencies (unit-tested on Linux against the
// game's files, see tools/sky_scan_test.cpp).
//
// Why: RF tags the game's sky pipelines by the FNV-1a 64 of their pixel shader. Hard-coded hashes break on every
// game update (and when a resource pack / shaderpack replaces a sky material), so RF reads the sky materials the
// game actually loads and learns the hashes itself. Same rules as tools/vv_sky_hashes.py:
//   * every DXBC container in the file is a shader; its context is the length-prefixed strings in front of it
//     (last "Direct3D_*" = platform, last Vertex/Fragment/Compute = stage, last known pass name = pass)
//   * the hash is FNV-1a 64 over the whole container (what CreateGraphicsPipelineState receives)
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace rf::skyscan {

inline uint64_t fnv1a(uint8_t const* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

struct Shader {
    std::string pass;     // "" when the file names no pass (Fancy Stars / SunMoon / Clouds)
    std::string platform; // Direct3D_SM60, Direct3D_SM65, ...
    std::string stage;    // Vertex / Fragment / Compute
    uint64_t    hash = 0;
};

inline bool identChar(uint8_t c, bool first) {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (!first && ((c >= '0' && c <= '9') || c == '.'));
}

inline std::vector<Shader> scan(uint8_t const* d, size_t n) {
    static constexpr std::string_view kPasses[] = {"ForwardPBRTransparent", "ForwardPBRTransparentSkyProbe", "DepthOnlyAlphaTest",
                                                   "MotionOnly",            "Opaque",                        "DoDeferredShading"};
    struct Str { size_t at; std::string_view s; };
    std::vector<Str> strs;
    for (size_t i = 0; i + 4 < n;) {
        uint32_t L;
        std::memcpy(&L, d + i, 4);
        if (L >= 3 && L <= 80 && i + 4 + L <= n && identChar(d[i + 4], true)) {
            bool ok = true;
            for (uint32_t k = 1; k < L && ok; ++k) ok = identChar(d[i + 4 + k], false);
            if (ok) {
                strs.push_back({i, std::string_view(reinterpret_cast<char const*>(d + i + 4), L)});
                i += 4 + L;
                continue;
            }
        }
        ++i;
    }
    std::vector<Shader> out;
    std::string_view plat, stage, pass;
    size_t           si = 0;
    for (size_t o = 0; o + 28 <= n; ++o) {
        if (std::memcmp(d + o, "DXBC", 4) != 0) continue;
        for (; si < strs.size() && strs[si].at < o; ++si) {
            auto s = strs[si].s;
            if (s.substr(0, 9) == "Direct3D_") plat = s;
            else if (s == "Vertex" || s == "Fragment" || s == "Compute") stage = s;
            else for (auto p : kPasses) if (s == p) { pass = s; break; }
        }
        uint32_t size;
        std::memcpy(&size, d + o + 24, 4);
        if (size < 32 || o + size > n) continue;
        out.push_back({std::string(pass), std::string(plat), std::string(stage), fnv1a(d + o, size)});
    }
    return out;
}

/// Which RF sky kind a shader of material `name` (file name without ".material.bin") gets; 0 = none.
/// Kinds match rf::sky::Kind: 1 Dome, 2 Hide, 3 Clouds, 4 VvSky.
inline int kindFor(std::string_view name, Shader const& s) {
    if (s.stage != "Fragment" || (s.platform != "Direct3D_SM60" && s.platform != "Direct3D_SM65")) return 0;
    if (name == "Sky") return s.pass == "Opaque" || s.pass.empty() ? 1 : 0;
    if (name == "Stars" || name == "SunMoon") return 2;
    if (name == "Clouds") return 3;
    if (name == "RenderChunk") return 5;   // the world's blocks: World FX knows when the world has been drawn
    if (s.pass != "ForwardPBRTransparent") return 0; // SkyProbe (reflections), depth-only, motion: stay vanilla
    if (name == "StarsForwardPBR" || name == "SunMoonForwardPBR") return 4;
    if (name == "CloudsForwardPBR") return 3;
    return 0;
}

/// The sky materials RF learns from (file names without ".material.bin").
inline constexpr std::string_view kMaterials[] = {"Sky", "Stars", "SunMoon", "Clouds", "StarsForwardPBR", "SunMoonForwardPBR",
                                                  "CloudsForwardPBR", "RenderChunk"};

} // namespace rf::skyscan
