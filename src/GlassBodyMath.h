// Renderer Foundation - Glass Body (Purxiy "Glass": You / Other players / Armour): the pure parts, no D3D / Windows,
// so tools/glassbody_test.cpp can check them on any machine.
//   * scoreSkin: a texture read back from the GPU against one of the skins Bedrock Forge grabbed (64x64 RGBA8,
//     classic layout): how many texels disagree on painted / empty, and how far the painted colours are apart
//   * isSlim: Alex-style 3-pixel arms (the classic arm's back columns 54-55 are empty)
//   * the 64x64 player layout as cube faces (base layer + the second "hat / jacket / sleeves / trousers" layer)
#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace rf::glassbody {

struct SkinScore { long alphaOff = 0; double colourOff = 255.0; long painted = 0; };

/// px: the read-back texture (64x64 or 128x128; 128 is read every second texel), rows of `pitch` bytes, RGBA8 or
/// BGRA8. skin: BF's 64x64 RGBA8 copy (pitch 256).
inline SkinScore scoreSkin(uint8_t const* px, unsigned pitch, unsigned w, unsigned h, bool bgra, uint8_t const* skin) {
    SkinScore s;
    if (!px || !skin || w < 64 || h < 64 || w != h || (w != 64 && w != 128)) return s;
    unsigned const sc = w / 64;
    long           n  = 0;
    double         d  = 0;
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x) {
            uint8_t const* p = px + size_t(y * sc) * pitch + size_t(x * sc) * 4;
            uint8_t const* q = skin + (y * 64 + x) * 4;
            bool const     a = p[3] != 0, b = q[3] != 0;
            if (a) ++s.painted;
            if (a != b) { ++s.alphaOff; continue; }
            if (!a) continue;
            for (int c = 0; c < 3; ++c) d += std::abs(int(p[bgra ? 2 - c : c]) - int(q[c]));
            ++n;
        }
    s.colourOff = n ? d / (3.0 * double(n)) : 255.0;
    return s;
}
/// The same skin (packs and the game's upload can differ by a few texels; a mob's texture is far off).
inline bool sameSkin(SkinScore const& s) { return s.painted >= 256 && s.alphaOff <= 24 && s.colourOff <= 8.0; }

/// 3-pixel (slim) arms: the classic right arm's back face (x 52-55, y 20-31) is only 3 wide there - 54/55 empty.
inline bool isSlim(uint8_t const* skin /* 64x64 RGBA8 */) {
    for (int y = 20; y < 32; ++y)
        for (int x = 54; x < 56; ++x)
            if (skin[(y * 64 + x) * 4 + 3] != 0) return false;
    return true;
}

/// One cube of the player model in texture space: (u, v) top-left of its net, w x h x d.
struct Cube { int u, v, w, h, d; bool overlay; };
inline std::vector<Cube> playerCubes(bool slim) {
    int const a = slim ? 3 : 4;
    return {{0, 0, 8, 8, 8, false},   {32, 0, 8, 8, 8, true},    // head, hat
            {16, 16, 8, 12, 4, false}, {16, 32, 8, 12, 4, true}, // body, jacket
            {40, 16, a, 12, 4, false}, {40, 32, a, 12, 4, true}, // right arm, sleeve
            {32, 48, a, 12, 4, false}, {48, 48, a, 12, 4, true}, // left arm, sleeve
            {0, 16, 4, 12, 4, false},  {0, 32, 4, 12, 4, true},  // right leg, trousers
            {16, 48, 4, 12, 4, false}, {0, 48, 4, 12, 4, true}}; // left leg, trousers
}
/// The six faces of each cube as texel rects (x, y, w, h), in the order top, bottom, right, front, left, back.
struct Rect { int x, y, w, h; };
inline std::vector<Rect> cubeFaces(Cube const& c) {
    return {{c.u + c.d, c.v, c.w, c.d},           {c.u + c.d + c.w, c.v, c.w, c.d},  {c.u, c.v + c.d, c.d, c.h},
            {c.u + c.d, c.v + c.d, c.w, c.h},     {c.u + c.d + c.w, c.v + c.d, c.d, c.h},
            {c.u + 2 * c.d + c.w, c.v + c.d, c.w, c.h}};
}

} // namespace rf::glassbody
