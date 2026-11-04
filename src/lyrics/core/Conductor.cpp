// LyricsForge - [BF] the Conductor. Original code for Bedrock Forge; see Conductor.h.
#include "lyrics/core/Conductor.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/SongMemory.h"

namespace bf::lyrics {
namespace {
float ease(float dt, float tau) { return tau <= 0 ? 1.0f : 1.0f - std::exp(-dt / tau); }
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
} // namespace

void Conductor::reset() {
    mS          = MusicState{};
    mSeq        = -2;
    mLastPos    = -1;
    mShort = mLong = 0;
    mLo = 0.3f; mHi = 0.7f;
    mHold = 0;
    mSinceDrop = mSinceChorus = 1e9f;
    mBudgetTarget = 0.5f;
    mWasBuilding = mWasHook = false;
    mPrevSection = MusicSection::Mid;
}

float directed(MusicState const& m, float calm, float full) {
    if (!m.active) return calm + (full - calm) * 0.5f;
    return calm + (full - calm) * (m.budget + 0.5f * m.impact);
}

void Conductor::update(ConductorIn const& in) {
    float const dt = std::clamp(in.dt, 0.0f, 0.25f);
    auto&       s  = mS;
    s.dropNow = s.chorusNow = s.buildNow = s.calmNow = s.songStart = false;
    s.impact *= 1.0f - ease(dt, 0.35f);
    s.beat = in.beat;

    if (!in.enabled || in.posMs < 0) {
        s.active = false;
        s.budget += (0.5f - s.budget) * ease(dt, 1.0f);
        return;
    }
    if (in.songSeq != mSeq) { // new song: per-song state starts over
        mSeq = in.songSeq;
        s.choruses = 0;
        s.songAge  = 0;
        s.songStart = true;
        s.build = 0;
        s.inHook = 0;
        mSinceDrop = mSinceChorus = 1e9f;
        mWasBuilding = mWasHook = false;
        mLastPos = -1;
        mHold = 0;
    }
    s.active = in.playing;
    s.songAge += dt;
    mSinceDrop += dt;
    mSinceChorus += dt;
    if (!in.playing) { // paused: hold the picture, let the budget settle low
        s.budget += (0.3f - s.budget) * ease(dt, 1.5f);
        mLastPos = in.posMs;
        return;
    }

    // ---- live bands (smoothed) + loudness followers
    BandFrame const& b = in.bands;
    float const k = ease(dt, 0.15f);
    if (in.bandsLive) {
        s.bass += (b.bass - s.bass) * k;
        s.mid += (b.mid - s.mid) * k;
        s.treble += (b.treble - s.treble) * k;
        s.loud += (b.loud - s.loud) * k;
        s.onset = std::max(b.onset, s.onset * (1.0f - ease(dt, 0.12f)));
        mShort += (b.loud - mShort) * ease(dt, 0.4f);
        mLong += (b.loud - mLong) * ease(dt, 6.0f);
        // slow percentile followers: snap out quickly, creep back in, so the range fits this song
        if (mShort > mHi) mHi += (mShort - mHi) * ease(dt, 0.5f); else mHi -= 0.015f * dt;
        if (mShort < mLo) mLo += (mShort - mLo) * ease(dt, 0.5f); else mLo += 0.015f * dt;
        if (mHi - mLo < 0.15f) { float c = 0.5f * (mHi + mLo); mHi = c + 0.075f; mLo = c - 0.075f; }
    }
    float const range  = std::max(0.15f, mHi - mLo);
    float const liveE  = in.bandsLive ? clamp01((mShort - mLo) / range) : 0.5f;
    float const liveL  = in.bandsLive ? clamp01((mLong - mLo) / range) : 0.5f;

    SongMemory const* mem = in.memory;
    float memE = -1;
    if (mem && mem->ready()) memE = mem->energyAt(in.posMs);
    s.known = memE >= 0;
    s.energy = s.known ? clamp01(0.7f * memE + 0.3f * liveE) : liveE;

    // ---- section
    MusicSection sec = s.section;
    if (s.known) {
        for (auto const& x : mem->sections)
            if (in.posMs >= x.start && in.posMs < x.end) {
                sec = x.kind == SongMemory::Section::Kind::Calm ? MusicSection::Calm
                    : x.kind == SongMemory::Section::Kind::High ? MusicSection::High : MusicSection::Mid;
                break;
            }
    } else if (in.bandsLive) {
        mHold -= dt;
        MusicSection want = liveE < 0.33f ? MusicSection::Calm : liveE < 0.66f ? MusicSection::Mid : MusicSection::High;
        if (want != sec && mHold <= 0) { sec = want; mHold = 4.0f; }
    }
    if (sec == MusicSection::Calm && mPrevSection != MusicSection::Calm) s.calmNow = true;
    mPrevSection = s.section = sec;

    // ---- events: scan (lastPos, pos]; a jump (seek) skips them
    bool const contiguous = mLastPos >= 0 && in.posMs >= mLastPos && in.posMs - mLastPos < 1000;
    bool drop = false, chorus = false;
    float build = 0;
    if (mem) {
        if (contiguous)
            for (auto const& e : mem->events)
                if (e.at > mLastPos && e.at <= in.posMs) (e.kind == SongMemory::Event::Kind::Drop ? drop : chorus) = true;
        if (auto const* e = mem->nextEvent(in.posMs, 16000); e && e->buildFrom >= 0 && in.posMs >= e->buildFrom && e->at > e->buildFrom)
            build = clamp01(float(in.posMs - e->buildFrom) / float(e->at - e->buildFrom));
        bool hook = false;
        for (auto const& h : mem->hooks)
            if (in.posMs >= h.first && in.posMs < h.second) { hook = true; break; }
        if (hook && !mWasHook && contiguous) {
            // a hook line starting = a chorus, unless the saved map already marks one right here
            bool marked = mem->nextEvent(in.posMs - 3000, 6000) != nullptr;
            if (!marked) chorus = true;
        }
        mWasHook = hook;
        s.inHook += ((hook ? 1.0f : 0.0f) - s.inHook) * ease(dt, hook ? 0.3f : 1.2f);
    }
    if (!s.known && in.bandsLive && !drop) { // live guess: a sudden, loud lift above the recent average
        if (liveE - liveL > 0.35f && liveE > 0.7f) drop = true;
        if (liveE - liveL > 0.12f && liveE > 0.45f) build = std::max(build, clamp01((liveE - liveL - 0.12f) / 0.25f) * 0.6f);
    }
    if (drop && mSinceDrop < kDropCooldown) drop = false;
    if (chorus && (mSinceChorus < kChorusCooldown || mSinceDrop < 3.0f)) chorus = false;
    if (drop) { s.dropNow = true; mSinceDrop = 0; mSinceChorus = 0; s.impact = 1.0f; s.choruses++; }
    if (chorus) { s.chorusNow = true; mSinceChorus = 0; s.impact = std::max(s.impact, 0.75f); s.choruses++; }
    if (build > 0.05f && !mWasBuilding) s.buildNow = true;
    mWasBuilding = build > 0.05f;
    if (s.dropNow) { mWasBuilding = false; build = 0; }
    s.build += (build - s.build) * ease(dt, build > s.build ? 0.2f : 0.6f);

    // ---- budget: what the section allows, plus hooks, anticipation and escalation; drops open it fully
    float t = sec == MusicSection::Calm ? 0.3f : sec == MusicSection::High ? 0.8f : 0.55f;
    t += 0.15f * s.inHook + 0.25f * s.build;
    if (sec == MusicSection::High || s.inHook > 0.5f) t += std::min(0.15f, 0.04f * float(s.choruses));
    if (mSinceDrop < 2.0f) t = 1.0f;
    mBudgetTarget = std::clamp(t, 0.15f, 1.0f);
    s.budget += (mBudgetTarget - s.budget) * ease(dt, mBudgetTarget > s.budget ? 0.25f : 1.5f);
    mLastPos = in.posMs;
}

} // namespace bf::lyrics
