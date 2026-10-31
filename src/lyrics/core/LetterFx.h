// LyricsFlow port - [BF] exit effects that break words apart (new; LyricsFlow has none of these). Original code for
// Bedrock Forge. When a word leaves, its letter quads are cut into fragments (sub-rectangles of the same glyph, so
// the SDF outline / glow still work) that fly, spin, burn or dissolve, then fade. No world collisions (they live a
// second or two), so it works for sky text, floating words and screen text alike; speeds scale with the letter size.
#pragma once
#include <cstdint>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

class LetterFx {
public:
    /// quads: one word's main-pass glyphs (positions relative to `origin`, as buildWordGlyphs makes them).
    void spawn(std::vector<BfGlyphV1> const& quads, Vec3 origin, ExitAnim mode, int64_t seed, float intensity = 1.0f);
    void step(float dt);
    void emit(Vec3 origin, std::vector<BfGlyphV1>& out) const;
    void clear() { mFrags.clear(); }
    size_t size() const { return mFrags.size(); }
    static bool handles(ExitAnim m); // modes this class plays (the others are word transforms or physics)

private:
    struct Frag {
        BfGlyphV1 tmpl{};
        Vec3      pos, vel, angVel; // world (or screen units for BF_GLYPH_SCREEN)
        Vec3      right, up;        // half extents at spawn
        Quat      orient;           // rotation since spawn
        Vec3      upDir;            // "up" for this fragment (screen space: -y)
        float     size = 0.1f;      // letter height, for scaling motion
        float     age = 0, delay = 0, life = 1.2f;
        float     scale = 1, jitter = 0, jitterT = 0;
        uint32_t  baseFill = 0;
        ExitAnim  mode = ExitAnim::Shatter;
        float     seedF = 0;
        bool      screen = false;
    };
    std::vector<Frag> mFrags;
    static constexpr size_t kMaxFrags = 6000;
};

} // namespace bf::lyrics
