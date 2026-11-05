// LyricsForge - [BF] the Conductor (original code for Bedrock Forge).
//
// One music state per frame that every cinematic visual reads, instead of each effect listening to the audio on its
// own: live band energy, where we are in the song (calm / mid / high section, building towards a drop), and an
// energy BUDGET - how much visual energy is allowed right now, so a verse can never out-shine a chorus. It also fires
// the big moments (build start, drop, chorus, calm) with cooldowns, and makes each chorus a little bigger than the
// last.
//
// Sources, most trusted first: Song Memory of earlier plays (sees the drop coming), the song's repeated lyric lines
// (choruses, even on the first play), then a live guess from the loudness. Pure logic: no Windows, no game types.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "lyrics/core/Bands.h"

namespace bf::lyrics {

class SongMemory;

enum class MusicSection : uint8_t { Calm, Mid, High };

struct MusicState {
    bool    active = false;     // a song is playing and the director is on
    float   bass = 0, mid = 0, treble = 0, loud = 0, onset = 0; // live, smoothed 0..1
    float   energy  = 0;        // 0..1 song-normalised energy (memory when known, else live)
    float   beat    = 0;        // 0..1 bass hit envelope
    MusicSection section = MusicSection::Mid;
    float   budget  = 0.5f;     // 0..1 visual energy allowed now (eased)
    float   build   = 0;        // 0..1 progress of a build towards the next drop (0 = none)
    float   impact  = 0;        // 1 right on a drop / chorus hit, decaying (~0.6 s) - punches, flashes
    float   inHook  = 0;        // 0..1 eased: a repeated (hook / chorus) lyric line is being sung
    int     choruses = 0;       // chorus / drop moments so far this song (escalation)
    bool    known   = false;    // Song Memory drives it (else live guesses)
    // events, true on the frame they happen
    bool    dropNow = false, chorusNow = false, buildNow = false, calmNow = false, songStart = false;
    float   songAge = 0;        // seconds since the song started (title card)
    float   timeScale = 1;      // speed-ramp for cosmetic motion: < 1 suspended in a build, > 1 bursting after a drop
    // [BF] the held breath: 0..1 in the last ~0.6 s before a KNOWN drop (memory / your marks) - the world dims,
    // slows and goes quiet, then everything lands on the hit. toDrop = s to that drop (-1 = none in sight).
    float   hold = 0;
    float   toDrop = -1;
    bool    bigDrop = false;    // [BF] with dropNow: one of the song's big drops (held breath + the hardest landing)
    // [BF] chorus transformation: the world changes into its chorus look on a chorus / drop and eases back after
    float   lift = 0;           // 0..1 eased
    bool    lifted = false;     // in the chorus look now
    bool    liftNow = false, liftEnd = false; // events: entered / left the chorus look this frame
};

struct ConductorIn {
    int64_t          posMs   = -1;     // song position incl. offsets
    bool             playing = false;
    int64_t          songSeq = -1;
    float            dt      = 1.0f / 60.0f;
    BandFrame        bands;            // latest analyser frame (zeros when the feed is off)
    bool             bandsLive = false;
    float            beat    = 0;      // BeatDetector level
    SongMemory const* memory = nullptr; // this song's memory (may be empty / not ready)
    std::vector<std::pair<int64_t, char>> const* marks = nullptr; // [BF] manual sections (win over the guesses)
    bool             enabled = true;
};

class Conductor {
public:
    void              update(ConductorIn const& in);
    MusicState const& state() const { return mS; }
    void              reset();

    // tuning (public for tests)
    static constexpr float kDropCooldown   = 20.0f; // s between drops
    static constexpr float kChorusCooldown = 12.0f;
    static constexpr float kLiveDropCooldown = 30.0f; // live guesses are less sure: rarer
    static constexpr float kHoldLead = 0.6f;  // s of held breath before a known drop
    static constexpr float kBigDrop  = 0.5f;  // floor: a drop needs at least this Song Memory strength to be a big one
    static constexpr float kLiftMin  = 10.0f; // the chorus look stays at least this long...
    static constexpr float kLiftMax  = 45.0f; // ...and never longer than this without a new hit
    static constexpr float kLiftFade = 4.0f;  // s the energy must stay down before it lets go

private:
    MusicState mS;
    int64_t    mSeq = -2;
    int64_t    mLastPos = -1;
    float      mShort = 0, mLong = 0;   // live loudness followers (~0.4 s, ~6 s)
    float      mLo = 0.3f, mHi = 0.7f;  // live normalisation range (slow percentile followers)
    float      mHold = 0;               // s the live section must hold before it can change
    float      mSinceDrop = 1e9f, mSinceChorus = 1e9f, mSinceCalm = 1e9f;
    float      mBudgetTarget = 0.5f;
    bool       mWasBuilding = false, mWasHook = false;
    float      mLiftAge = 0, mLiftFade = 0;
    MusicSection mPrevSection = MusicSection::Mid;
};

/// how big an effect should be: base scaled by the budget and topped up by impacts (helpers for the visuals)
float directed(MusicState const& m, float calm, float full);

} // namespace bf::lyrics
