// LyricsForge - see TempoGuess.h. Original code for Bedrock Forge.
#include "lyrics/core/TempoGuess.h"

#include "lyrics/core/Lyrics.h"

#include <cmath>
#include <vector>

namespace bf::lyrics {

TempoGuess guessTempo(Lyrics const& ly) {
    std::vector<double> on;
    bool realWords = false;
    for (auto const& l : ly.lines) if (l.index >= 0 && !l.estimated && !l.words.empty()) { realWords = true; break; }
    for (auto const& l : ly.lines) {
        if (l.index < 0) continue;
        if (realWords && !l.estimated) for (auto const& w : l.words) on.push_back(double(w.origStart));
        else on.push_back(double(l.start));
    }
    TempoGuess g;
    if (on.size() < 16) return g;
    constexpr double kTau = 6.283185307179586;
    double best = -1, bestP = 0, bestC = 0, bestS = 0;
    for (double bpm = 70.0; bpm <= 160.0; bpm += 0.1) {
        double const p = 60000.0 / bpm;
        double c = 0, s = 0;
        for (double t : on) { double const a = kTau * t / p; c += std::cos(a); s += std::sin(a); }
        double r = std::sqrt(c * c + s * s) / double(on.size());
        // syllables also sit on the half beats: a little credit for the 8th-note grid of the same tempo
        double c2 = 0, s2 = 0;
        for (double t : on) { double const a = 2 * kTau * t / p; c2 += std::cos(a); s2 += std::sin(a); }
        r += 0.25 * std::sqrt(c2 * c2 + s2 * s2) / double(on.size());
        if (r > best) { best = r; bestP = p; bestC = c; bestS = s; }
    }
    double ph = std::atan2(bestS, bestC) / kTau * bestP;
    if (ph < 0) ph += bestP;
    g.bpm = 60000.0 / bestP;
    g.phaseMs = ph;
    g.confidence = float(std::min(1.0, best / 1.25));
    return g;
}
} // namespace bf::lyrics
