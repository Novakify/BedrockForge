// Offline checks for src/GlassBodyMath.h (no game, no D3D):  g++ -std=c++20 -I src tools/glassbody_test.cpp && ./a.out
#include "GlassBodyMath.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <utility>

using namespace rf::glassbody;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); ++fails; } } while (0)

// a skin painted like a real one: every face of every base cube opaque, overlay faces partly painted
static std::vector<uint8_t> makeSkin(bool slim, uint32_t seed) {
    std::vector<uint8_t> s(64 * 64 * 4, 0);
    std::mt19937         r(seed);
    for (auto const& c : playerCubes(slim))
        for (auto const& f : cubeFaces(c))
            for (int y = f.y; y < f.y + f.h; ++y)
                for (int x = f.x; x < f.x + f.w; ++x) {
                    if (c.overlay && (r() & 3)) continue;   // the second layer: a quarter painted
                    uint8_t* p = &s[(y * 64 + x) * 4];
                    p[0] = uint8_t(r()), p[1] = uint8_t(r()), p[2] = uint8_t(r()), p[3] = 255;
                }
    return s;
}

int main() {
    // the layout: no two faces overlap, everything inside 64x64
    size_t classicN = 0, slimN = 0;
    for (bool slim : {false, true}) {
        std::set<std::pair<int, int>> seen;
        for (auto const& c : playerCubes(slim))
            for (auto const& f : cubeFaces(c))
                for (int y = f.y; y < f.y + f.h; ++y)
                    for (int x = f.x; x < f.x + f.w; ++x) {
                        CHECK(x >= 0 && x < 64 && y >= 0 && y < 64);
                        CHECK(seen.insert({x, y}).second);
                    }
        (slim ? slimN : classicN) = seen.size();
    }
    CHECK(classicN == 3264 && slimN < classicN);   // classic: head 384 + hat 384 + body 352 + jacket 352 + 8 limb parts x 224
    // slim detection
    auto classic = makeSkin(false, 1), slim = makeSkin(true, 2);
    CHECK(!isSlim(classic.data()));
    CHECK(isSlim(slim.data()));

    // the same skin matches itself (64x64, RGBA and BGRA, and a 128x128 upload)
    SkinScore s = scoreSkin(classic.data(), 256, 64, 64, false, classic.data());
    CHECK(sameSkin(s) && s.alphaOff == 0 && s.colourOff == 0.0);
    std::vector<uint8_t> bgra = classic;
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    CHECK(sameSkin(scoreSkin(bgra.data(), 256, 64, 64, true, classic.data())));
    std::vector<uint8_t> big(128 * 128 * 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) std::memcpy(&big[(y * 128 + x) * 4], &classic[((y / 2) * 64 + x / 2) * 4], 4);
    CHECK(sameSkin(scoreSkin(big.data(), 512, 128, 128, false, classic.data())));
    // a few texels off (a pack / the game's upload) still matches
    auto tweaked = classic;
    for (int k = 0; k < 10; ++k) tweaked[(5 * 64 + 40 + k) * 4 + 3] ^= 255;
    CHECK(sameSkin(scoreSkin(tweaked.data(), 256, 64, 64, false, classic.data())));
    // another skin (a mob's texture, another player) does not
    auto other = makeSkin(false, 99);
    CHECK(!sameSkin(scoreSkin(other.data(), 256, 64, 64, false, classic.data())));
    // same shape, different colours (a recolour of your skin) does not
    auto recol = classic;
    for (size_t i = 0; i < recol.size(); i += 4) recol[i] = uint8_t(recol[i] + 60);
    CHECK(!sameSkin(scoreSkin(recol.data(), 256, 64, 64, false, classic.data())));
    // an empty / unpainted texture never matches
    std::vector<uint8_t> empty(64 * 64 * 4, 0);
    CHECK(!sameSkin(scoreSkin(empty.data(), 256, 64, 64, false, empty.data())));
    // wrong sizes are refused
    CHECK(!sameSkin(scoreSkin(classic.data(), 256, 64, 32, false, classic.data())));

    std::printf(fails ? "%d check(s) failed\n" : "all glass body checks passed\n", fails);
    return fails ? 1 : 0;
}
