// LyricsForge - [BF] Moments. Original code for Bedrock Forge; see Moments.h.
#include "lyrics/core/Moments.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "lyrics/core/Conductor.h"
#include "lyrics/core/Lyrics.h"

namespace bf::lyrics {

SkyFamily skyFamily(std::string_view id) {
    auto in = [&](std::initializer_list<std::string_view> l) { return std::find(l.begin(), l.end(), id) != l.end(); };
    if (in({"aurora", "polar", "prism", "silk", "purple"})) return SkyFamily::Aurora;
    if (in({"golden_hour", "sunset", "sakura", "cotton_candy", "aquarell", "cherry"})) return SkyFamily::Warm;
    if (in({"thunder", "storm", "inferno", "blood_moon", "crimson", "eclipse_heart"})) return SkyFamily::Storm;
    if (in({"synthwave", "neon_city", "matrix", "pulsar"})) return SkyFamily::Neon;
    if (in({"deep_sea", "ocean", "dream_ocean"})) return SkyFamily::Water;
    if (in({"frost", "ice", "midnight"})) return SkyFamily::Cold;
    return SkyFamily::Space; // nebula, black_hole, cosmos, galaxy, astral, rift, supernova, warp, eclipse, meteor, custom
}

char const* familyName(SkyFamily f) {
    static char const* const n[] = {"space", "aurora", "warm", "storm", "neon", "water", "cold"};
    return n[int(f)];
}
Moment momentByName(std::string_view n) {
    for (int i = 1; i <= int(Moment::Dim); ++i) if (n == momentName(Moment(i))) return Moment(i);
    return Moment::None;
}

char const* momentName(Moment m) {
    static char const* const n[] = {"none", "light ring", "meteors", "starfall", "aurora burst", "lightning", "embers", "petals",
                                    "snow", "rain", "fireflies", "blizzard", "heartbeat", "dim"};
    return n[int(m)];
}

std::vector<Moment> const& chorusDeck(SkyFamily f) {
    using M = Moment;
    // smallest -> biggest. Hand-picked so every moment belongs to the sky it plays in.
    static std::vector<Moment> const decks[] = {
        /* space  */ {M::Wave, M::Meteors, M::Starfall},
        /* aurora */ {M::AuroraBurst, M::Wave, M::Snow},
        /* warm   */ {M::Fireflies, M::Petals, M::AuroraBurst},
        /* storm  */ {M::Rain, M::Lightning, M::Embers},
        /* neon   */ {M::Wave, M::Meteors, M::AuroraBurst},
        /* water  */ {M::Fireflies, M::Wave, M::Rain},
        /* cold   */ {M::Snow, M::AuroraBurst, M::Blizzard},
    };
    return decks[int(f)];
}

namespace {
std::string lowerWord(std::string_view w) {
    std::string s;
    for (unsigned char c : w) if (std::isalpha(c)) s += char(std::tolower(c));
    return s;
}
bool anyOf(std::string const& w, std::initializer_list<char const*> l) {
    for (auto x : l) if (w == x) return true;
    return false;
}
int conceptSlot(Moment m) { return int(m) & 15; }
} // namespace

Moment keywordMoment(std::string_view word, SkyFamily f) {
    std::string const w = lowerWord(word);
    if (w.size() < 3) return Moment::None;
    bool const space = f == SkyFamily::Space || f == SkyFamily::Neon;
    // concept -> how this sky says it
    if (anyOf(w, {"rain", "raining", "rainy", "tears", "crying", "cry", "cried", "drown", "drowning"}))
        return space ? Moment::Starfall : f == SkyFamily::Cold ? Moment::Snow : Moment::Rain;
    if (anyOf(w, {"fire", "fires", "burn", "burning", "burned", "flame", "flames", "blaze", "ashes", "inferno"}))
        return Moment::Embers;
    if (anyOf(w, {"snow", "snowing", "cold", "ice", "icy", "winter", "freeze", "freezing", "frozen"}))
        return f == SkyFamily::Storm ? Moment::Rain : Moment::Snow;
    if (anyOf(w, {"star", "stars", "starlight", "shine", "shining", "heaven", "galaxy", "comet", "universe"}))
        return space || f == SkyFamily::Aurora || f == SkyFamily::Cold ? Moment::Meteors : Moment::Fireflies;
    if (anyOf(w, {"heart", "heartbeat", "love", "kiss"}))
        return Moment::Heartbeat;
    if (anyOf(w, {"flower", "flowers", "rose", "roses", "bloom", "blossom", "petals", "spring", "garden"}))
        return f == SkyFamily::Space || f == SkyFamily::Storm ? Moment::Fireflies : Moment::Petals;
    if (anyOf(w, {"thunder", "lightning", "storm", "storms"}))
        return Moment::Lightning;
    if (anyOf(w, {"dark", "darkness", "shadow", "shadows", "night", "midnight"}))
        return Moment::Dim;
    if (anyOf(w, {"ocean", "sea", "waves", "tide", "water"}))
        return space ? Moment::Wave : Moment::Fireflies;
    return Moment::None;
}

void Moments::start(Moment m, float amount, float life, bool untilUnlift, std::string const& why) {
    if (m == Moment::None) return;
    switch (m) { // one-shots
    case Moment::Wave: mOut.wave = true; break;
    case Moment::Meteors: mOut.meteors = std::max(mOut.meteors, int(std::lround(5 + 5 * amount))); break;
    case Moment::Starfall: mOut.starfall = true; mStarfall = std::max(mStarfall, 3.5f + 2.5f * amount); break;
    default: break;
    }
    if (m != Moment::Wave && m != Moment::Meteors) mFx.push_back({m, 0.0f, life, amount, untilUnlift});
    if (!mOut.event.empty()) mOut.event += ", ";
    mOut.event += std::string(momentName(m)) + " (" + why + ")";
}

void Moments::step(MomentsIn const& in) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    MusicState const* m = in.music;
    mOut.meteors = 0; mOut.wave = false; mOut.starfall = false; mOut.event.clear();
    if (in.songSeq != mSeq) { mSeq = in.songSeq; mFx.clear(); mSinceDeck = 99; mLastT = -1; std::fill(std::begin(mCool), std::end(mCool), 0.0f); mStarfall = -1; }
    mSinceKey += dt;
    for (auto& c : mCool) c = std::max(0.0f, c - dt);
    bool const on = m && m->active;
    SkyFamily const fam = skyFamily(in.sky);

    // ---- chorus variety + song arc
    mSinceDeck += dt;
    // a new chorus look, or a new big hit while the look is still on (choruses back to back): the next moment
    bool const hit = m && (m->liftNow || ((m->dropNow || m->chorusNow) && m->lifted && mSinceDeck > 8.0f));
    if (on && in.chorusVariety && hit) {
        mSinceDeck = 0;
        auto const& deck = chorusDeck(fam);
        int const k = std::max(0, m->choruses - 1), total = std::max(1, m->chorusTotal);
        if (m->finalHit && total >= 2) { // the final chorus: everything this sky has, at full size
            for (Moment x : deck) start(x, 1.3f, 30.0f, true, "final chorus");
        } else {
            float const grow = total > 1 ? std::clamp(float(k) / float(total - 1), 0.0f, 1.0f) : 0.5f;
            Moment const x = deck[size_t((uint32_t(k) + in.variant) % deck.size())];
            start(x, 0.6f + 0.5f * grow, 30.0f, true, "chorus " + std::to_string(k + 1) + "/" + std::to_string(total));
        }
    }
    if (on && m->liftEnd)
        for (auto& f : mFx) if (f.untilUnlift) { f.untilUnlift = false; f.life = f.age + 2.0f; } // ease out after the chorus

    // ---- lyric keywords: a word starting now
    if (on && in.keywords && in.lyrics && in.t >= 0) {
        if (mLastT >= 0 && in.t > mLastT && in.t - mLastT < 1000)
            for (auto const& l : in.lyrics->lines) {
                if (l.end < mLastT - 2000) continue;
                if (l.start > in.t) break;
                for (auto const& w : l.words) {
                    if (w.end <= mLastT || w.start > in.t) continue;
                    // a timed "word" can be a whole line (some sources time lines only): each token gets its share
                    size_t const n = w.text.size();
                    for (size_t b = 0; b < n;) {
                        while (b < n && w.text[b] == ' ') ++b;
                        size_t e = b;
                        while (e < n && w.text[e] != ' ') ++e;
                        if (e == b) break;
                        int64_t const at = w.start + int64_t(double(w.end - w.start) * double(b) / double(std::max<size_t>(1, n)));
                        std::string_view const tok(w.text.data() + b, e - b);
                        b = e;
                        if (at <= mLastT || at > in.t) continue;
                        Moment const x = keywordMoment(tok, fam);
                        if (x == Moment::None || mSinceKey < kKeyGap || mCool[conceptSlot(x)] > 0) continue;
                        float const amt = m->lifted ? 0.35f : 0.6f; // inside a chorus look: a softer accent
                        start(x, amt, kKeyLife, false, "\"" + std::string(tok) + "\"");
                        mCool[conceptSlot(x)] = kKeyCooldown;
                        mSinceKey = 0;
                    }
                }
            }
        mLastT = in.t;
    } else if (in.t >= 0) mLastT = in.t;

    // ---- combine
    for (auto& f : mFx) f.age += dt;
    std::erase_if(mFx, [](Fx const& f) { return !f.untilUnlift && f.age > f.life; });
    Weather wx = Weather::None;
    float wAmt = 0, boost = 0, light = 0, heart = 0, dim = 0;
    for (auto const& f : mFx) {
        float const in1 = std::min(1.0f, f.age / 1.2f), out1 = f.untilUnlift ? 1.0f : std::clamp((f.life - f.age) / 1.5f, 0.0f, 1.0f);
        float const a = f.amount * in1 * out1;
        Weather w = Weather::None;
        switch (f.m) {
        case Moment::Rain: w = Weather::Rain; break;
        case Moment::Snow: w = Weather::Snow; break;
        case Moment::Blizzard: w = Weather::Blizzard; break;
        case Moment::Embers: w = Weather::Embers; break;
        case Moment::Petals: w = Weather::Petals; break;
        case Moment::Fireflies: w = Weather::Fireflies; break;
        case Moment::AuroraBurst: boost = std::max(boost, a); break;
        case Moment::Lightning: light = std::max(light, a); break;
        case Moment::Heartbeat: { // lub-dub, ~70 bpm
            float const ph = std::fmod(f.age, 0.86f);
            float const p = std::exp(-ph * ph / 0.004f) + 0.7f * std::exp(-(ph - 0.24f) * (ph - 0.24f) / 0.004f);
            heart = std::max(heart, a * p);
        } break;
        case Moment::Dim: dim = std::max(dim, a * 0.6f); break;
        default: break;
        }
        if (w != Weather::None && a > wAmt) { wAmt = a; wx = w; }
    }
    mOut.weather = wx;
    mOut.weatherAmt = std::clamp(wAmt, 0.0f, 1.0f);
    mOut.skyBoost = std::clamp(boost, 0.0f, 1.0f);
    mOut.lightning = std::clamp(light, 0.0f, 1.0f);
    mOut.heartbeat = std::clamp(heart, 0.0f, 1.0f);
    mOut.dim = dim;
    if (mStarfall > 0) { // a starfall: small meteors every few hundred ms
        mStarfall -= dt;
        mStarAcc += dt;
        if (mStarAcc > 0.28f) { mStarAcc = 0; mOut.meteors = std::max(mOut.meteors, 2); }
    }
}

} // namespace bf::lyrics
