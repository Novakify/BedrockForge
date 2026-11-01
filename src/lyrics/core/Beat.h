// LyricsFlow port - [BF] bass hit detector (new feature). Original code for Bedrock Forge.
// Fed with the mono mix of what the PC is playing (Windows loopback capture, Audio_win.cpp). Low-passes it to the
// kick/bass band, follows the band's energy and fires a hit when the energy jumps well above its recent average and is
// loud enough in absolute terms - so quiet parts and soft songs don't thump, drops and hard bass do.
#pragma once
#include <cstddef>

namespace bf::lyrics {

class BeatDetector {
public:
    explicit BeatDetector(float sampleRate = 48000.0f) { setRate(sampleRate); }
    void setRate(float sampleRate);
    /// mono samples, -1..1
    void feed(float const* mono, size_t n);
    /// 1 right on a hit, decaying to 0 over ~0.25 s. Call with the time since the last call.
    float level(float dt);
    float bassRms() const { return mSlowRms; } // diagnostics
    int   hits() const { return mHits; }

private:
    void block();
    float mRate = 48000, mA = 0, mLp1 = 0, mLp2 = 0;
    float mAcc = 0;
    int   mCount = 0, mBlock = 480;     // 10 ms blocks
    float mSlowRms = 0, mPrev = 0;      // ~1.5 s average of the bass energy, last block
    float mPending = 0;                 // hit strength waiting for level()
    float mEnv = 0;
    int   mSinceHit = 1000, mHits = 0;  // blocks
    int   mBlocks = 0;                  // blocks seen (the average needs ~0.3 s before hits count)
};

} // namespace bf::lyrics
