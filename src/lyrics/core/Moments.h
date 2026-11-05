// LyricsForge - [BF] Moments: what the world DOES at a song's big beats, chosen to fit the sky it happens in.
// Original code for Bedrock Forge.
//
// Three sources, one vocabulary:
//  - chorus variety: every sky family has its own deck of chorus transformations, smallest to biggest (a space
//    sky gets a ring of light, then a meteor fan, then a starfall; a storm sky gets rain, then lightning, then
//    embers...). Each chorus of a song draws the next one (offset per song), so choruses differ but stay in the
//    sky's language - nothing that doesn't belong there (no petals in a black hole, no rain in deep space).
//  - song arc: the first chorus is a taste, later ones grow, the FINAL chorus plays the whole deck at full size.
//  - lyric keywords: a sung word about rain / fire / snow / stars / love / flowers / storms / darkness brings a
//    short, soft moment - rendered in the current sky's language (rain in deep space falls as starlight).
// Pure logic; the runtime feeds the outputs to the sky (weather, strength, lightning, meteors, light ring, dim)
// and the Cinema (heartbeat).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lyrics/core/Sky.h"

namespace bf::lyrics {

struct MusicState;
struct Lyrics;
using sky::Weather;

enum class SkyFamily : uint8_t { Space, Aurora, Warm, Storm, Neon, Water, Cold };
SkyFamily   skyFamily(std::string_view skyId);
char const* familyName(SkyFamily f);

enum class Moment : uint8_t { None, Wave, Meteors, Starfall, AuroraBurst, Lightning, Embers, Petals, Snow, Rain, Fireflies, Blizzard, Heartbeat, Dim };
char const* momentName(Moment m);
Moment      momentByName(std::string_view name); // "petals", "light ring" ... (None = unknown)
/// the chorus deck of a sky family, smallest moment first
std::vector<Moment> const& chorusDeck(SkyFamily f);
/// a sung word -> its moment in this sky's language (None = not a keyword)
Moment keywordMoment(std::string_view word, SkyFamily f);

struct MomentsIn {
    MusicState const* music = nullptr;
    Lyrics const*     lyrics = nullptr;
    int64_t           t = -1;       // song position (ms)
    int64_t           songSeq = 0;
    std::string_view  sky;          // the sky showing now (chorus sky while lifted)
    float             dt = 1.0f / 60;
    uint32_t          variant = 0;  // per-song offset into the deck
    bool              chorusVariety = true, keywords = true;
};

struct MomentsOut {
    Weather  weather = Weather::None;
    float    weatherAmt = 0;   // 0..1
    float    skyBoost = 0;     // 0..1: aurora / sky layers stronger and faster
    float    lightning = 0;    // 0..1: storm flashes in view
    int      meteors = 0;      // one-shot: this many big meteors now
    bool     starfall = false; // one-shot: a long, slow shower (many small meteors over seconds)
    bool     wave = false;     // one-shot: the light ring rolls down the sky
    float    heartbeat = 0;    // 0..1 lub-dub pulse (Cinema vignette)
    float    dim = 0;          // 0..1 the sky darkens a little
    std::string event;         // what just started (log), "" = nothing new
};

class Moments {
public:
    void              step(MomentsIn const& in);
    MomentsOut const& out() const { return mOut; }
    int               active() const { return int(mFx.size()); }
    /// [BF] visual audit (demo file "moments"): play one moment now, as a chorus would
    void              force(Moment m, float amount = 1.0f) { start(m, amount, 6.0f, false, "audit"); }

    static constexpr float kKeyLife = 4.0f, kKeyCooldown = 14.0f, kKeyGap = 4.0f;

private:
    struct Fx { Moment m; float age = 0, life = 4, amount = 1; bool untilUnlift = false; };
    void start(Moment m, float amount, float life, bool untilUnlift, std::string const& why);
    std::vector<Fx> mFx;
    MomentsOut      mOut;
    int64_t         mSeq = -1, mLastT = -1;
    float           mSinceKey = 99, mSinceDeck = 99;
    float           mCool[16] = {};
    float           mStarfall = -1; // s left of a running starfall
    float           mStarAcc = 0;
};

} // namespace bf::lyrics
