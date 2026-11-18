// LyricsForge - [BF] multi-band music analyser (original code for Bedrock Forge).
// Fed with the mono mix of what the PC is playing (Windows loopback, Audio_win.cpp), next to the bass-hit
// BeatDetector. Splits the signal with cheap one-pole filters (no FFT) into
//   bass   < ~150 Hz      kick, sub, bass line
//   mid    ~250-2500 Hz   vocals, snare body, chords
//   treble > ~5000 Hz     hats, cymbals, air
// plus the overall loudness and an onset strength (how sharply the energy just rose), and emits one BandFrame per
// 50 ms. Values are loudness on a log scale (0 = -60 dBFS or quieter, 1 = 0 dBFS), so they mean the same thing on
// every play of a song - that's what Song Memory stores. Nothing is recorded or kept here; only these numbers leave.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bf::lyrics {

struct BandFrame {
    float   bass = 0, mid = 0, treble = 0, loud = 0; // 0..1 (log loudness, -60..0 dBFS)
    float   onset = 0;                              // 0..1, energy rise inside the frame
    // [BF] 16 log-spaced bands, ~40 Hz .. 16 kHz (FFT of the last 2048 samples at the frame end), 0..1 on the same
    // -60..0 dB scale with a per-band tilt so a normal mix reads flat. Live only (Song Memory keeps the 3 above).
    static constexpr int kSpec = 16;
    float   spec[kSpec] = {};
    bool    hasSpec = false;
    int64_t tNs   = 0;                              // steady-clock time of the frame's last sample
};

class BandAnalyzer {
public:
    static constexpr int kFft = 2048;
    static constexpr int kFrameMs = 50;
    explicit BandAnalyzer(float sampleRate = 48000.0f) { setRate(sampleRate); }
    void setRate(float sampleRate);
    /// mono samples -1..1; endNs = steady-clock ns when the last sample was captured (about now)
    void feed(float const* mono, size_t n, int64_t endNs);
    /// moves the finished frames out (oldest first); keeps at most ~10 s if nobody drains
    void drain(std::vector<BandFrame>& out);
    BandFrame latest() const { return mLatest; }

private:
    void  blockDone(int64_t tNs);
    float mRate = 48000;
    float aBass = 0, a250 = 0, a2500 = 0, a5000 = 0;
    float lpB1 = 0, lpB2 = 0, lp250 = 0, lp250b = 0, lp2500 = 0, lp2500b = 0, lp5000 = 0;
    double sB = 0, sM = 0, sT = 0, sL = 0;  // sums of squares, current 10 ms block
    int    mCount = 0, mBlock = 480, mBlocks = 0;
    double fB = 0, fM = 0, fT = 0, fL = 0;  // sums over the frame's blocks
    float  mPrevBlockDb = -60, mOnset = 0;
    std::vector<float> mRing;   // [BF] last kFft samples (spectrum)
    size_t mRingPos = 0;
    void   spectrum(BandFrame& fr);
    BandFrame              mLatest;
    std::vector<BandFrame> mOut;
};

/// 0..1 from a mean square: 20*log10(rms) mapped -60..0 dBFS
float levelOf(double meanSquare);

} // namespace bf::lyrics
