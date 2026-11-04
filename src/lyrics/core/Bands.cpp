// LyricsForge - [BF] multi-band music analyser, see Bands.h. Original code for Bedrock Forge.
#include "lyrics/core/Bands.h"

#include <algorithm>
#include <cmath>

namespace bf::lyrics {

namespace {
float onePole(float fc, float rate) { return 1.0f - std::exp(-6.2831853f * fc / std::max(1.0f, rate)); }
} // namespace

float levelOf(double meanSquare) {
    if (meanSquare <= 1e-12) return 0.0f;
    double db = 10.0 * std::log10(meanSquare); // = 20 log10(rms)
    return float(std::clamp((db + 60.0) / 60.0, 0.0, 1.0));
}

void BandAnalyzer::setRate(float sampleRate) {
    mRate  = std::max(8000.0f, sampleRate);
    aBass  = onePole(150.0f, mRate);
    a250   = onePole(250.0f, mRate);
    a2500  = onePole(2500.0f, mRate);
    a5000  = onePole(5000.0f, mRate);
    mBlock = std::max(1, int(mRate / 100.0f)); // 10 ms
    mCount = 0;
    sB = sM = sT = sL = 0;
}

void BandAnalyzer::feed(float const* mono, size_t n, int64_t endNs) {
    if (!mono || n == 0) return;
    double const nsPerSample = 1e9 / double(mRate);
    for (size_t i = 0; i < n; ++i) {
        float const x = mono[i];
        lpB1 += aBass * (x - lpB1);
        lpB2 += aBass * (lpB1 - lpB2);
        lp250 += a250 * (x - lp250);
        lp250b += a250 * (lp250 - lp250b);
        lp2500 += a2500 * (x - lp2500);
        lp2500b += a2500 * (lp2500 - lp2500b); // two poles each side: hats / cymbals stay out of the mids
        lp5000 += a5000 * (x - lp5000);
        float const mid = lp2500b - lp250b, hi = x - lp5000;
        sB += double(lpB2) * lpB2;
        sM += double(mid) * mid;
        sT += double(hi) * hi;
        sL += double(x) * x;
        if (++mCount >= mBlock) blockDone(endNs - int64_t(double(n - 1 - i) * nsPerSample));
    }
}

void BandAnalyzer::blockDone(int64_t tNs) {
    double const k = 1.0 / double(std::max(1, mCount));
    double b = sB * k, m = sM * k, t = sT * k, l = sL * k;
    // onset: how far this block's loudness jumped over the last one (dB), squashed to 0..1
    float const db = 10.0f * float(std::log10(std::max(1e-12, l)));
    float const rise = std::max(0.0f, db - std::max(-60.0f, mPrevBlockDb));
    mPrevBlockDb = db;
    mOnset       = std::max(mOnset, std::clamp(rise / 9.0f, 0.0f, 1.0f));
    fB += b; fM += m; fT += t; fL += l;
    sB = sM = sT = sL = 0;
    mCount = 0;
    if (++mBlocks < kFrameMs / 10) return;
    double const f = 1.0 / double(mBlocks);
    BandFrame fr;
    fr.bass   = levelOf(fB * f);
    fr.mid    = levelOf(fM * f);
    fr.treble = levelOf(fT * f);
    fr.loud   = levelOf(fL * f);
    fr.onset  = mOnset;
    fr.tNs    = tNs;
    mLatest   = fr;
    if (mOut.size() >= 200) mOut.erase(mOut.begin(), mOut.begin() + 100); // nobody drained for ~10 s
    mOut.push_back(fr);
    fB = fM = fT = fL = 0;
    mBlocks = 0;
    mOnset  = 0;
}

void BandAnalyzer::drain(std::vector<BandFrame>& out) {
    out.insert(out.end(), mOut.begin(), mOut.end());
    mOut.clear();
}

} // namespace bf::lyrics
