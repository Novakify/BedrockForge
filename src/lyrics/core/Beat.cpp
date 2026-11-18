// LyricsFlow port - [BF] bass hit detector. Original code for Bedrock Forge; see Beat.h.
#include "lyrics/core/Beat.h"

#include <algorithm>
#include <cmath>

namespace bf::lyrics {

void BeatDetector::setRate(float sampleRate) {
    mRate  = std::max(8000.0f, sampleRate);
    mA     = 1.0f - std::exp(-2.0f * 3.14159265f * 150.0f / mRate); // one-pole, twice: ~150 Hz bass band
    mBlock = std::max(1, int(mRate / 100.0f));
}

void BeatDetector::feed(float const* mono, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        mLp1 += mA * (mono[i] - mLp1);
        mLp2 += mA * (mLp1 - mLp2);
        mAcc += mLp2 * mLp2;
        if (++mCount >= mBlock) block();
    }
}

void BeatDetector::block() {
    float const rms = std::sqrt(mAcc / float(std::max(1, mCount)));
    mAcc = 0;
    mCount = 0;
    ++mSinceHit;
    ++mBlocks;
    // a hit: a sharp rise over the recent bass level, loud in absolute terms, not right after the last one
    float const floorLvl = 0.035f; // about -29 dBFS in the bass band: quiet music never thumps
    bool const  rise     = rms > mSlowRms * 1.45f + 0.004f && rms > mPrev * 1.08f;
    if (rise && rms > floorLvl && mSinceHit >= 18 && mBlocks > 30) { // >= 180 ms apart (up to ~330 bpm of kicks)
        float const strength = std::clamp((rms / std::max(mSlowRms, 1e-4f) - 1.0f) * 0.9f, 0.35f, 1.0f) *
                               std::clamp((rms - floorLvl) / 0.08f + 0.4f, 0.0f, 1.0f);
        mPending = std::max(mPending, strength);
        mSinceHit = 0;
        ++mHits;
    }
    mSlowRms += (rms - mSlowRms) * (rms > mSlowRms ? 0.02f : 0.008f); // ~0.5 s up, ~1.3 s down
    mPrev = rms;
}

float BeatDetector::level(float dt) {
    mEnv = std::max(mEnv * std::exp(-std::max(0.0f, dt) / 0.11f), mPending);
    mPending = 0;
    return mEnv;
}

} // namespace bf::lyrics
