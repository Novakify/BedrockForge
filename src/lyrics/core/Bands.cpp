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
        if (mRing.size() != size_t(kFft)) { mRing.assign(size_t(kFft), 0.0f); mRingPos = 0; }
        mRing[mRingPos] = x;
        mRingPos = (mRingPos + 1) % size_t(kFft);
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
    spectrum(fr);
    mLatest   = fr;
    if (mOut.size() >= 200) mOut.erase(mOut.begin(), mOut.begin() + 100); // nobody drained for ~10 s
    mOut.push_back(fr);
    fB = fM = fT = fL = 0;
    mBlocks = 0;
    mOnset  = 0;
}

// [BF] 16-band spectrum: Hann-windowed radix-2 FFT of the last kFft samples (20 times a second, ~0.1 ms)
void BandAnalyzer::spectrum(BandFrame& fr) {
    if (mRing.size() != size_t(kFft)) return;
    static thread_local std::vector<float> re, im;
    re.assign(size_t(kFft), 0.0f); im.assign(size_t(kFft), 0.0f);
    for (int i = 0; i < kFft; ++i) {
        float const w = 0.5f - 0.5f * std::cos(6.2831853f * float(i) / float(kFft - 1));
        re[size_t(i)] = mRing[(mRingPos + size_t(i)) % size_t(kFft)] * w;
    }
    for (int i = 1, j = 0; i < kFft; ++i) { // bit reversal
        int bit = kFft >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[size_t(i)], re[size_t(j)]); std::swap(im[size_t(i)], im[size_t(j)]); }
    }
    for (int len = 2; len <= kFft; len <<= 1) {
        double const ang = -6.283185307179586 / len;
        float const wr = float(std::cos(ang)), wi = float(std::sin(ang));
        for (int i = 0; i < kFft; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                size_t const a = size_t(i + k), b = size_t(i + k + len / 2);
                float const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                float const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
    double const binHz = double(mRate) / kFft, lo = 40.0, hi = std::min(16000.0, double(mRate) * 0.45);
    for (int b = 0; b < BandFrame::kSpec; ++b) {
        double const f0 = lo * std::pow(hi / lo, double(b) / BandFrame::kSpec), f1 = lo * std::pow(hi / lo, double(b + 1) / BandFrame::kSpec);
        int const k0 = std::max(1, int(f0 / binHz)), k1 = std::max(k0 + 1, int(f1 / binHz));
        double sum = 0;
        for (int k = k0; k < k1 && k < kFft / 2; ++k) sum += double(re[size_t(k)]) * re[size_t(k)] + double(im[size_t(k)]) * im[size_t(k)];
        double const ms = sum / double(k1 - k0) * 4.0 / (double(kFft) * kFft); // per-bin power, Hann-corrected-ish
        // music falls ~3 dB/octave: tilt the highs up so a balanced mix looks level across the bars
        double const tiltDb = 3.0 * std::log2(std::max(1.0, f0 / 120.0));
        fr.spec[b] = std::clamp(levelOf(ms * 30.0) + float(tiltDb / 60.0), 0.0f, 1.0f);
    }
    fr.hasSpec = true;
}

void BandAnalyzer::drain(std::vector<BandFrame>& out) {
    out.insert(out.end(), mOut.begin(), mOut.end());
    mOut.clear();
}

} // namespace bf::lyrics
