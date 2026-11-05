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
    mSinceDrop = mSinceChorus = mSinceCalm = 1e9f;
    mBudgetTarget = 0.5f;
    mWasBuilding = mWasHook = false;
    mLiftAge = mLiftFade = 0;
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
    s.liftNow = s.liftEnd = s.bigDrop = false;
    s.impact *= 1.0f - ease(dt, 0.35f);
    s.beat = in.beat;

    if (!in.enabled || in.posMs < 0) {
        s.active = false;
        s.budget += (0.5f - s.budget) * ease(dt, 1.0f);
        s.hold = 0; s.toDrop = -1;
        if (s.lifted) { s.lifted = false; s.liftEnd = true; }
        s.lift += (0.0f - s.lift) * ease(dt, 1.5f);
        s.outro += (0.0f - s.outro) * ease(dt, 0.5f);
        return;
    }
    if (in.songSeq != mSeq) { // new song: per-song state starts over
        mSeq = in.songSeq;
        s.choruses = 0;
        s.songAge  = 0;
        s.songStart = true;
        s.build = 0;
        s.inHook = 0;
        mSinceDrop = mSinceChorus = mSinceCalm = 1e9f;
        mWasBuilding = mWasHook = false;
        mLastPos = -1;
        mHold = 0;
        s.hold = 0; s.toDrop = -1; s.outro = 0;
        if (s.lifted) { s.lifted = false; s.liftEnd = true; }
        mLiftAge = 0;
    }
    s.active = in.playing;
    s.songAge += dt;
    mSinceDrop += dt;
    mSinceChorus += dt;
    if (!in.playing) { // paused: hold the picture, let the budget settle low
        s.budget += (0.3f - s.budget) * ease(dt, 1.5f);
        s.hold = 0; s.toDrop = -1;
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
        if (want != sec && mHold <= 0) { sec = want; mHold = 6.0f; }
    }
    mSinceCalm += dt;
    if (sec == MusicSection::Calm && mPrevSection != MusicSection::Calm && mSinceCalm > 20.0f) { s.calmNow = true; mSinceCalm = 0; }
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
    bool const manual = in.marks && !in.marks->empty();
    // [BF] the next known hit (held breath). Real songs: Song Memory finds 0..6 "drops" per song, many of them small
    // (strength ~0.5). Only the song's BIG drops get the hush and the hardest landing - at most its top three, and
    // within 85% of its strongest - so the moment stays special; the rest are ordinary hits.
    int64_t nextHit = -1; bool nextIsDrop = false, nextIsChorus = false;
    float bigAt = 2;
    if (mem) {
        float top[3] = {0, 0, 0};
        for (auto const& e : mem->events)
            if (e.kind == SongMemory::Event::Kind::Drop) {
                float v = e.strength;
                for (auto& t : top) if (v > t) std::swap(v, t);
            }
        if (top[0] > 0) bigAt = std::max({kBigDrop, 0.85f * top[0], top[2]});
    }
    if (mem && !manual)
        for (auto const& e : mem->events)
            if (e.at > in.posMs) {
                if (e.at - in.posMs < 3000) {
                    nextHit = e.at;
                    nextIsDrop = e.kind == SongMemory::Event::Kind::Drop && e.strength >= bigAt;
                    nextIsChorus = e.kind == SongMemory::Event::Kind::Chorus;
                }
                break;
            }
    bool bigNow = false;
    if (mem && !manual && contiguous)
        for (auto const& e : mem->events)
            if (e.at > mLastPos && e.at <= in.posMs && e.kind == SongMemory::Event::Kind::Drop && e.strength >= bigAt) bigNow = true;
    if (manual) { // [BF] your own marks: they decide the moments, the guesses stand down
        drop = chorus = false;
        build = 0;
        int64_t nextDrop = -1, buildFrom = -1;
        for (auto const& [t, k] : *in.marks) {
            if (contiguous && t > mLastPos && t <= in.posMs) {
                if (k == 'd') { drop = true; bigNow = true; } // your own drop marks are always the big ones
                else if (k == 'c') chorus = true;
                else if (k == 'q') { s.calmNow = true; mSinceCalm = 0; }
            }
            if (k == 'b' && t <= in.posMs) buildFrom = t;
            if (k == 'd' && t > in.posMs && nextDrop < 0) nextDrop = t;
            if ((k == 'd' || k == 'c') && t > in.posMs && nextHit < 0 && t - in.posMs < 3000) { nextHit = t; nextIsDrop = k == 'd'; nextIsChorus = k == 'c'; }
        }
        if (buildFrom >= 0 && nextDrop > buildFrom) build = clamp01(float(in.posMs - buildFrom) / float(nextDrop - buildFrom));
        mSinceDrop = drop ? 1e9f : mSinceDrop;     // manual hits ignore the cooldowns
        mSinceChorus = chorus ? 1e9f : mSinceChorus;
    }
    if (!manual && !s.known && in.bandsLive && !drop) { // live guess: a sudden, loud lift above the recent average
        if (liveE - liveL > 0.40f && liveE > 0.72f && mSinceDrop > kLiveDropCooldown) drop = true;
        if (liveE - liveL > 0.12f && liveE > 0.45f) build = std::max(build, clamp01((liveE - liveL - 0.12f) / 0.25f) * 0.6f);
    }
    if (drop && mSinceDrop < kDropCooldown) drop = false;
    if (chorus && (mSinceChorus < kChorusCooldown || mSinceDrop < 3.0f)) chorus = false;
    if (drop) { s.dropNow = true; s.bigDrop = bigNow; mSinceDrop = 0; mSinceChorus = 0; s.impact = 1.0f; s.choruses++; }
    if (chorus) { s.chorusNow = true; mSinceChorus = 0; s.impact = std::max(s.impact, s.known ? 0.75f : 1.0f); s.choruses++; }
    if (s.dropNow) build = 0;
    // ---- held breath: a known drop (or chorus, softer) right ahead - dim, slow, quiet; released on the hit
    s.toDrop = nextHit >= 0 ? float(nextHit - in.posMs) / 1000.0f : -1.0f;
    float holdT = 0;
    if (s.toDrop >= 0 && s.toDrop <= kHoldLead && in.enabled && (nextIsDrop || nextIsChorus) && (nextIsDrop || mSinceChorus > kChorusCooldown)) {
        float const x = 1.0f - s.toDrop / kHoldLead;
        // [BF] first listen: no energy map yet, so the song's repeated lyric blocks (its choruses, known the moment
        // the lyrics load) are its big moments - they get the full held breath
        bool const firstListen = nextIsChorus && !s.known;
        holdT = x * x * (3 - 2 * x) * (nextIsDrop || firstListen ? 1.0f : 0.55f);
    }
    if (s.dropNow || s.chorusNow) s.hold = 0; // the release: it all lands at once
    else s.hold += (holdT - s.hold) * ease(dt, holdT > s.hold ? 0.06f : 0.25f);
    // ---- chorus look: in on a hit, held while the energy stays up, out on calm (min / max time)
    if (s.dropNow || s.chorusNow) {
        if (!s.lifted) s.liftNow = true;
        s.lifted = true; mLiftAge = 0; mLiftFade = 0;
    } else if (s.lifted) {
        mLiftAge += dt;
        // the energy has to stay down for a few seconds (in game: a drop in a quiet intro let go just as the
        // loud part started - a short dip must not end it)
        bool const fading = sec != MusicSection::High && s.inHook < 0.25f && s.energy < 0.6f;
        mLiftFade = fading ? mLiftFade + dt : 0.0f;
        if (s.calmNow || (mLiftAge > kLiftMin && mLiftFade > kLiftFade) || mLiftAge > kLiftMax) { s.lifted = false; s.liftEnd = true; }
    }
    s.lift += ((s.lifted ? 1.0f : 0.0f) - s.lift) * ease(dt, s.lifted ? 0.12f : 2.2f);
    s.build += (build - s.build) * ease(dt, build > s.build ? 0.2f : 0.6f);
    // a build is announced once the eased value is real (> 0.4), re-armed only after it has faded (< 0.05)
    if (s.build > 0.4f && !mWasBuilding) { s.buildNow = true; mWasBuilding = true; }
    if (s.build < 0.05f) mWasBuilding = false;

    // ---- budget: what the section allows, plus hooks, anticipation and escalation; drops open it fully
    float t = sec == MusicSection::Calm ? 0.3f : sec == MusicSection::High ? 0.8f : 0.55f;
    t += 0.15f * s.inHook + 0.25f * s.build;
    if (sec == MusicSection::High || s.inHook > 0.5f) t += std::min(0.15f, 0.04f * float(s.choruses));
    if (mSinceDrop < 2.0f) t = 1.0f;
    t += 0.08f * s.lift;
    mBudgetTarget = std::clamp(t, 0.15f, 1.0f);
    s.budget += (mBudgetTarget - s.budget) * ease(dt, mBudgetTarget > s.budget ? 0.25f : 1.5f);
    // ---- speed ramp: fragments / sky motion hang in the air while it builds, then burst on the hit
    float const ramp = (1.0f - 0.6f * s.build) * (1.0f + 1.6f * s.impact) * (1.0f - 0.75f * s.hold);
    s.timeScale += (ramp - s.timeScale) * ease(dt, ramp > s.timeScale ? 0.05f : s.hold > 0.05f ? 0.08f : 0.35f);
    // ---- outro: the last ~9 s of a song with a known length (end credits, the world winds down)
    float outroT = 0;
    if (in.durationMs > 30000 && in.posMs > in.durationMs - 9000)
        outroT = clamp01(float(in.posMs - (in.durationMs - 9000)) / 3000.0f);
    s.outro += (outroT - s.outro) * ease(dt, outroT > s.outro ? 0.8f : 0.3f);
    mLastPos = in.posMs;
}

} // namespace bf::lyrics
