// LyricsForge - [BF] word ribbon: a thin ribbon of light that runs under the lyric line with the singing, leaving a
// fading tail (the karaoke progress drawn as light). Original code for Bedrock Forge. BfGlyphV1 SOLID quads.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string_view>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

struct DrawWord;
class FontSet;

class Ribbon {
public:
    /// words: this frame's drawn words; `width(text, font)` in em
    using WidthFn = std::function<float(std::string_view text, std::string_view font)>;
    void step(std::vector<DrawWord> const& words, WidthFn const& width, Vec3 origin, double now, float energy, bool enabled);
    void emit(uint32_t argb, float glow, std::vector<BfGlyphV1>& out) const;
    void clear() { mPts.clear(); }
    size_t points() const { return mPts.size(); }

    static constexpr double kTail = 0.7; // s

private:
    struct Pt { Vec3 pos, up; double t; int64_t line; bool screen; float alpha; };
    std::deque<Pt> mPts;
    float          mEnergy = 0;
    double         mNow = 0;
    void push(Pt const& p);
};

} // namespace bf::lyrics
