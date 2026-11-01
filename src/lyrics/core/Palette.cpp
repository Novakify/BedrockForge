// LyricsFlow port - [BF] colours from cover art. Original code for Bedrock Forge; see Palette.h.
#include "lyrics/core/Palette.h"

#include <algorithm>
#include <cmath>

namespace bf::lyrics {
namespace {
void toHsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 1e-5f ? d / mx : 0.0f;
    if (d < 1e-5f) { h = 0; return; }
    if (mx == r) h = std::fmod((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h /= 6.0f;
}
uint32_t fromHsv(float h, float s, float v) {
    h = h - std::floor(h);
    float i = std::floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    float r, g, b;
    switch (int(i) % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    auto c = [](float x) { return uint32_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}
} // namespace

uint32_t coverAccent(uint8_t const* rgba, int w, int h, uint32_t fallback) {
    if (!rgba || w <= 0 || h <= 0) return fallback;
    constexpr int kBins = 24;
    float         wt[kBins]{}, sx[kBins]{}, sy[kBins]{}, ss[kBins]{}, total = 0;
    int const     step = std::max(1, std::max(w, h) / 48); // ~48x48 samples is plenty
    for (int y = 0; y < h; y += step)
        for (int x = 0; x < w; x += step) {
            uint8_t const* p = rgba + (size_t(y) * size_t(w) + size_t(x)) * 4;
            if (p[3] < 32) continue;
            float hh, s, v;
            toHsv(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, hh, s, v);
            float k = s * s * std::sqrt(v); // vivid and not too dark
            if (k < 0.02f) continue;
            int b = std::min(kBins - 1, int(hh * kBins));
            wt[b] += k;
            sx[b] += k * std::cos(hh * 6.2831853f); // average hue on the circle
            sy[b] += k * std::sin(hh * 6.2831853f);
            ss[b] += k * s;
            total += k;
        }
    if (total < 0.5f) return fallback;
    int best = 0;
    float bestW = -1;
    for (int b = 0; b < kBins; ++b) { // a band and its neighbours, so a hue split across two bins still wins
        float v = wt[b] + 0.5f * (wt[(b + 1) % kBins] + wt[(b + kBins - 1) % kBins]);
        if (v > bestW) { bestW = v; best = b; }
    }
    float hue = std::atan2(sy[best], sx[best]) / 6.2831853f;
    float sat = ss[best] / std::max(1e-5f, wt[best]);
    return fromHsv(hue, std::clamp(sat, 0.7f, 0.95f), 1.0f); // neon: full brightness, strong colour
}

} // namespace bf::lyrics
