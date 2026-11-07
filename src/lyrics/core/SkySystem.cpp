// LyricsFlow port - live sky per frame + shared store. See SkySystem.h.
#include "lyrics/core/SkySystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "lyrics/core/Config.h"
#include "lyrics/core/Perf.h"

namespace bf::lyrics::sky {

namespace {
uint32_t rgba(uint32_t rgb, float a) {
    uint32_t A = uint32_t(std::clamp(int(std::lround(a * 255.0f)), 0, 255));
    return (rgb >> 16 & 255) | (rgb >> 8 & 255) << 8 | (rgb & 255) << 16 | A << 24;
}
float chan(uint32_t c, int k) { return float((c >> (16 - 8 * k)) & 255) / 255.0f; }
float lumOf(float const* c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
float satOf(uint32_t c) {
    float mx = std::max({chan(c, 0), chan(c, 1), chan(c, 2)}), mn = std::min({chan(c, 0), chan(c, 1), chan(c, 2)});
    return mx > 0.01f ? (mx - mn) / mx : 0.0f;
}
float ease(float cur, float want, float dt, float tauUp, float tauDown) {
    float const tau = want > cur ? tauUp : tauDown;
    return cur + (want - cur) * (tau <= 0 ? 1.0f : 1.0f - std::exp(-std::max(0.0f, dt) / tau));
}
} // namespace

WorldLight worldLightFor(Preset const& p, float visible, float brightness, float energy, float kick, float lightning) {
    float const    kk  = std::clamp(kick, 0.0f, 1.0f), vis = std::clamp(visible, 0.0f, 1.0f);
    uint32_t const acc = satOf(p.aurora1) >= satOf(p.aurora2) ? p.aurora1 : p.aurora2; // the sky's most colourful accent
    float base[3], col[3];
    for (int k = 0; k < 3; ++k) {
        base[k] = 0.65f * chan(p.horizon, k) + 0.35f * chan(p.top, k); // the world mostly sees the horizon
        col[k]  = base[k] + (chan(acc, k) - base[k]) * 0.3f * kk;       // the drop floods it with the accent
    }
    float const l = std::max(0.04f, lumOf(col));
    WorldLight  w;
    for (int k = 0; k < 3; ++k) w.tint[k] = 1.0f + (std::clamp(col[k] / l, 0.25f, 1.7f) - 1.0f) * 0.85f; // hue, same brightness
    { // pastel skies (cotton candy, aquarell, dawn) still colour the world: at least ~0.3 of hue
        float dev = 0;
        for (float t : w.tint) dev = std::max(dev, std::fabs(t - 1.0f));
        if (dev > 0.01f && dev < 0.3f) { float const g = std::min(0.3f / dev, 4.0f); for (float& t : w.tint) t = 1.0f + (t - 1.0f) * g; }
    }
    w.amount = vis * std::clamp(0.32f + 0.06f * energy + 0.10f * kk, 0.0f, 0.6f);
    float e = 0.85f + 0.28f * lumOf(base);                                    // night skies a bit darker, bright days brighter
    e *= 1.0f + 0.35f * (std::clamp(brightness, 0.3f, 1.6f) - 1.0f);          // the sky's breathing / dimming reaches the ground
    e += 0.03f * kk + 0.45f * std::clamp(lightning, 0.0f, 1.0f);             // drops and lightning light it up
    w.exposure = 1.0f + (std::clamp(e, 0.6f, 1.5f) - 1.0f) * vis;            // never darker than 60%
    w.sat      = 1.0f + vis * (0.06f + 0.08f * kk);
    return w;
}

void toVerts(std::vector<Quad> const& quads, std::vector<BfSkyVertV1>& out) {
    out.clear();
    out.reserve(quads.size() * 4);
    for (auto const& q : quads) {
        uint32_t fl = (q.layer == Layer::World ? BF_SKY_WORLD : 0u) | (q.tex == Tex::Sprite ? BF_SKY_SPRITE : 0u);
        for (auto const& v : q.v) out.push_back({{v.x, v.y, v.z}, {v.u, v.v}, rgba(v.rgb, v.a), fl, 0});
    }
}

void System::step(bool on, Config const& cfg, AutoPerf const& perf, SystemIn const& in) {
    // [LyricsForge] Awaken: no music = the plain vanilla world; when a song starts the whole look comes alive over
    // ~5 s (the sky rises from the horizon, the light floods in, stars / meteors / motes bloom). A pause or a skip
    // shorter than 4 s doesn't count as silence.
    float const adt = float(std::clamp(in.dtSec, 0.0, 0.25));
    auto  sstep      = [](float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
    bool startedNow = false;
    if (cfg.awaken) {
        float const before = mAwakeLevel;
        if (in.playing) {
            mSilent = 0;
            if (!mAwake) { mAwake = true; mAwakeT = 0; startedNow = true; mAwakeMeteors = false; mChapterT = -1; }
            else if (mOutroT >= 0) { // [LyricsForge] the music came back during the outro: rise again from where it is
                mOutroT = -1;
                mAwakeT = std::clamp(before, 0.0f, 1.0f) * kAwakeSec;
                mFromNothing = true; mSweep = std::min(mSweep, std::clamp(before, 0.0f, 0.999f));
            } else if ((mAwakeT < 0 || mAwakeT >= kAwakeSec) && mSongSeq != 0 && in.songSeq != mSongSeq) { // [LyricsForge] next song, no gap: a chapter
                mChapterT = 0; mChapterMeteors = false; mChapterSweep = true;
            }
        } else if ((mSilent += adt) > 4.0f && mAwake && mOutroT < 0) {
            mOutroT = 0; mOutroFrom = std::max(0.05f, before); mOutroMeteors = false; mChapterT = -1;
            if (mSweep >= 1.0f) mSweep = 0.999f; // the outro sinks the sky: the startup sweep in reverse
            mFromNothing = true;
        }
        mSongSeq = in.songSeq;
        if (mAwake && mAwakeT >= 0) mAwakeT = std::min(mAwakeT + adt, kAwakeSec + 1.0f);
        if (mOutroT >= 0 && (mOutroT += adt) >= kOutroSec) {
            mAwake = false; mAwakeT = -1; mOutroT = -1; mFromNothing = false; mSweep = 1;
            mDomeFor.clear(); // when it wakes again, the sky's signature turns to wherever you're looking
        }
        if (mChapterT >= 0 && (mChapterT += adt) >= kChapterSec) mChapterT = -1;
    } else {
        mAwake = true; mAwakeT = -1; mOutroT = -1; mChapterT = -1;
    }
    float const outro = mOutroT >= 0 ? std::clamp(mOutroT / kOutroSec, 0.0f, 1.0f) : -1.0f;
    float const A = !mAwake ? 0.0f
                  : outro >= 0 ? mOutroFrom * (1.0f - sstep(0.0f, 1.0f, outro))
                  : mAwakeT < 0 ? 1.0f : std::clamp(mAwakeT / kAwakeSec, 0.0f, 1.0f);
    // the light floods in as the front passes overhead (softer as the world exhales on the way out)
    float flood = A > 0.0f && A < 1.0f ? std::sin(3.14159265f * std::clamp((A - 0.52f) / 0.32f, 0.0f, 1.0f)) : 0.0f;
    if (outro >= 0) flood *= 0.6f;
    float const chap = mChapterT >= 0 ? std::sin(3.14159265f * std::clamp(mChapterT / kChapterSec, 0.0f, 1.0f)) : 0.0f;
    flood = std::max(flood, 0.75f * chap); // [LyricsForge] chapter: the world takes a breath with the new song
    mAwakeLevel = A;
    if (startedNow) { mSweep = 0; mFromNothing = true; } // the startup sweep rises over the vanilla sky (no old sky)
    std::optional<Preset> p;
    if (on && in.overworld && mAwake) p = find(cfg.skyPreset, cfg);
    bool picture = on && in.overworld && cfg.skyImage && !in.pictureFile.empty();

    Clock::In ci;
    ci.dtSec = in.dtSec; ci.rate = in.rate; ci.night = in.night; ci.beat = in.beat; ci.lineActive = in.lineActive; ci.playing = in.playing;
    ci.skyOpen = in.skyOpen; ci.picture = picture;
    mFrame = mClock.tick(p ? &*p : nullptr, cfg, ci);
    mFrame.brightness *= 1.0f - 0.55f * std::clamp(in.dim, 0.0f, 1.0f); // [BF] the sky dims as the song ends
    mFrame.brightness *= 1.0f + 0.35f * flood;                            // [LyricsForge] Awaken: the sky blooms once
    mLuma  = 0;
    if (p) { // relative luminance of the preset's top + horizon, as visible as the sky is
        auto lum = [](uint32_t c) {
            return (0.2126f * float((c >> 16) & 255) + 0.7152f * float((c >> 8) & 255) + 0.0722f * float(c & 255)) / 255.0f;
        };
        mLuma = 0.5f * (lum(p->top) + lum(p->horizon)) * mFrame.visible * std::min(1.0f, mFrame.bright);
    }

    { // [LyricsForge] the kick the dome shader sees: eased (0.25 s in, 0.7 s out) and halved - raw, it blinked every beat
        float const want = 0.5f * std::max(in.kick, 0.8f * flood);
        float const dtk = float(std::clamp(in.dtSec, 0.0, 0.25));
        mKickSmooth = ease(mKickSmooth, want, dtk, 0.25f, 0.7f);
        mBeatSmooth = ease(mBeatSmooth, 0.6f * std::clamp(in.beat, 0.0f, 1.0f), dtk, 0.12f, 0.45f);
        mPulseSmooth = ease(mPulseSmooth, std::clamp(in.pulse, 0.0f, 1.6f), dtk, 0.6f, 1.2f);
    }
    { // [LyricsForge] world light: eased, fast in (drops, lightning) and slow out; underground it mostly fades
        WorldLight want;
        if (p) {
            float const aw = sstep(0.25f, 0.85f, A); // Awaken: the world takes the light after the sky has started rising
            want = worldLightFor(*p, aw * mFrame.visible * std::min(1.0f, mFrame.bright + 0.4f) * (in.skyOpen ? 1.0f : 0.3f),
                                 mFrame.brightness, std::clamp(in.pulse - in.kick, 0.0f, 1.0f), std::max(in.kick, 0.8f * flood), in.lightning);
        }
        float const dt = float(std::clamp(in.dtSec, 0.0, 0.25));
        // [LyricsForge] no more strobing: the ground used to follow every kick in 50-100 ms, which on fast songs looked
        // like the world glitching. Now it glides (~0.8 s in, ~1.5 s out); only lightning still flashes it quickly.
        float const tt = mSweep < 1.0f ? 0.9f : 0.8f;
        bool const  bolt = in.lightning > 0.05f;
        for (int k = 0; k < 3; ++k) mWorld.tint[k] = ease(mWorld.tint[k], want.tint[k], dt, tt, tt);
        mWorld.amount   = ease(mWorld.amount, want.amount, dt, 0.8f, 1.5f);
        mWorld.exposure = ease(mWorld.exposure, want.exposure, dt, bolt ? 0.05f : 0.8f, bolt ? 0.35f : 1.5f);
        mWorld.sat      = ease(mWorld.sat, want.sat, dt, 0.8f, 1.5f);
    }

    { // [LyricsForge] fog: the far terrain fades into the sky's own horizon instead of the game's blue
        float want[3] = {mFog[0], mFog[1], mFog[2]}, wantW = 0;
        if (p) {
            float const b = std::clamp(mFrame.brightness, 0.25f, 1.4f);
            for (int k = 0; k < 3; ++k) want[k] = std::clamp((0.8f * chan(p->horizon, k) + 0.2f * chan(p->top, k)) * b, 0.0f, 1.0f);
            wantW = std::clamp(mFrame.visible, 0.0f, 1.0f) * (in.skyOpen ? 1.0f : 0.25f) * sstep(0.3f, 0.9f, A);
        }
        float const dt = float(std::clamp(in.dtSec, 0.0, 0.25));
        float const tt = 0.9f; // [LyricsForge] was 0.35 s: the fog (and the ground light that reads it) pumped with the sky
        for (int k = 0; k < 3; ++k) mFog[k] = ease(mFog[k], want[k], dt, tt, tt);
        mFogW = ease(mFogW, wantW, dt, 0.4f, 0.6f);
    }

    FxIn fi;
    fi.nowMs = in.nowMs; fi.dtSec = in.dtSec; fi.cam = Cam::of(in.yaw, in.pitch);
    fi.camX = in.camX; fi.camY = in.camY; fi.camZ = in.camZ;
    fi.quality = perf.quality(cfg); fi.picture = picture;
    fi.wave = in.wave; fi.waveColor = in.waveColor; fi.meteors = in.meteors;
    fi.burst = in.burst; fi.burstAmt = in.burstAmt; fi.lightning = in.lightning;
    fi.eq = in.eq; fi.eqAlpha = in.eqAlpha; fi.eqColor = in.eqColor;
    if (p) { // [BF] fireflies take the sky's light: warm gold at dusk, pale green-gold at night
        std::string_view const id = p->id;
        bool const warm = id == "golden_hour" || id == "sunset" || id == "sakura" || id == "cotton_candy" || id == "aquarell" ||
                          id == "cherry" || id == "dream_ocean" || id == "inferno" || id == "eclipse_heart";
        fi.fireflyRgb = warm ? 0xFFD58A : 0xE2F2A0;
    }
    if (A < 1.0f && A > 0.62f && !mAwakeMeteors) { fi.meteors = std::max(fi.meteors, 8); mAwakeMeteors = true; } // Awaken: a shower
    if (outro > 0.08f && !mOutroMeteors) { fi.meteors = std::max(fi.meteors, 4); mOutroMeteors = true; }        // outro: a few falling stars
    if (mChapterT > 0.5f && !mChapterMeteors) { fi.meteors = std::max(fi.meteors, 6); mChapterMeteors = true; } // chapter: a shower
    mQuads.clear();
    if (p) {
        Frame ff = mFrame;
        ff.visible *= sstep(0.3f, 1.0f, A); // Awaken: stars, weather, effects bloom in behind the rising sky
        mFx.frame(&*p, cfg, perf, ff, fi, mQuads);
    }

    BfSkyFrameV1 h{};
    h.size    = sizeof h;
    h.version = 1;
    h.active  = (p && mFrame.visible > 0.01f) || mFrame.picture > 0.01f ? 1u : 0u;
    h.frame   = ++mSeq;
    if (p) {
        float id3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // the dome is drawn in world axes (RF builds rays from the camera)
        if (mDomeFor != p->id) { // [BF] a new sky: turn its signature (body / burst / eclipse, az ~180) to where you look
            if (mHaveQ && !mDomeFor.empty() && !mFromNothing) { mPrevQ = mCurQ; mSweep = 0; } // [LyricsForge] sweep the old one away
            mDomeFor = p->id;
            float const front = frontAz(*p);
            mDomeYaw = float(in.yaw) - front;
        }
        auto  q      = pack(*p, aurora1(*p, cfg), aurora2(*p, cfg), mFrame.time, cfg, id3, mDomeYaw);
        // [LyricsForge] texel 13: what the music is doing right now, for every sky (the dome reacts, not just the quads)
        auto b8 = [](float v) { return uint8_t(std::clamp(int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)), 0, 255)); };
        q[13] = {b8(in.lightning), b8(mKickSmooth), b8(mBeatSmooth), in.wave < 0 ? uint8_t(0) : uint8_t(1 + int(std::clamp(in.wave, 0.0f, 1.0f) * 254.0f))};
        mCurQ = q; mHaveQ = true;
        if (outro >= 0) { // [LyricsForge] outro: the front sinks back to the horizon, taking the sky with it
            mSweep = std::min(mSweep, 0.999f * (1.0f - sstep(0.0f, 1.0f, outro)));
        } else {
            float const secs = mFromNothing ? kAwakeSec * 0.9f : (mChapterSweep ? 3.0f : 1.6f); // a chapter sweeps slower
            mSweep = std::min(1.0f, mSweep + float(std::clamp(in.dtSec, 0.0, 0.1)) / secs);
            if (mSweep >= 1.0f) mFromNothing = false;
        }
        if (mChapterT < 0 && mSweep >= 1.0f) mChapterSweep = false;
        // texel 14: r = sweep progress (0 none), g = over the vanilla sky (startup / outro), b = chapter (brighter, wider ring)
        q[14] = {mSweep < 1.0f ? uint8_t(1 + int(mSweep * 254.0f)) : uint8_t(0), uint8_t(mFromNothing && mSweep < 1.0f ? 255 : 0),
                 uint8_t(mChapterSweep && mSweep < 1.0f ? 255 : 0), 0};
        if (mSweep < 1.0f && !mFromNothing) for (int i = 0; i < 16; ++i) std::memcpy(h.prevParams[i], mPrevQ[size_t(i)].data(), 4);
        for (int i = 0; i < 16; ++i) std::memcpy(h.params[i], q[size_t(i)].data(), 4);
    }
    h.info[0]       = mFrame.t;
    h.info[1]       = mFrame.brightness;
    h.info[2]       = picture ? 0.0f : mFrame.alpha;
    h.info[3]       = mPulseSmooth; // [LyricsForge] eased (see the kick block): raw, the sky's colour pumped every beat
    h.picture       = picture ? 1u : 0u;
    h.pictureAlpha  = mFrame.picture;
    h.pictureBright = mFrame.picBright;
    h.camYaw = float(in.yaw); h.camPitch = float(in.pitch);
    std::strncpy(h.spritesFile, in.spritesFile.c_str(), sizeof h.spritesFile - 1);
    std::strncpy(h.pictureFile, picture ? in.pictureFile.c_str() : "", sizeof h.pictureFile - 1);
    std::vector<BfSkyVertV1> v;
    toVerts(mQuads, v);
    SkyStore::get().publish(h, std::move(v));
}

SkyStore& SkyStore::get() {
    static SkyStore s;
    return s;
}
void SkyStore::publish(BfSkyFrameV1 const& hdr, std::vector<BfSkyVertV1>&& verts) {
    std::lock_guard l(mMutex);
    bool images = std::strcmp(hdr.spritesFile, mHdr.spritesFile) != 0 || std::strcmp(hdr.pictureFile, mHdr.pictureFile) != 0;
    uint32_t gen = mHdr.imageGeneration + (images ? 1 : 0);
    mHdr         = hdr;
    mHdr.imageGeneration = gen;
    mVerts       = std::move(verts);
}
uint32_t SkyStore::copy(BfSkyFrameV1* hdr, BfSkyVertV1* dst, uint32_t cap) {
    std::lock_guard l(mMutex);
    if (!hdr || hdr->size < sizeof(BfSkyFrameV1)) return 0;
    BfSkyFrameV1 h = mHdr;
    h.size      = sizeof h;
    h.version   = 1;
    h.vertTotal = uint32_t(mVerts.size());
    uint32_t n  = dst ? std::min<uint32_t>(cap / 4 * 4, uint32_t(mVerts.size())) : 0;
    if (n) std::memcpy(dst, mVerts.data(), size_t(n) * sizeof(BfSkyVertV1));
    h.vertCount = n;
    *hdr        = h;
    return n;
}

} // namespace bf::lyrics::sky

extern "C" BF_LYRICS_EXPORT uint32_t BF_CopySkyFrame(BfSkyFrameV1* hdr, BfSkyVertV1* dst, uint32_t capacity) {
    return bf::lyrics::sky::SkyStore::get().copy(hdr, dst, capacity);
}
