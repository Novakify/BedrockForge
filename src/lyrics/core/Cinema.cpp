// LyricsForge - [BF] Cinema. Original code for Bedrock Forge; see Cinema.h.
#include "lyrics/core/Cinema.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstring>

#include "lyrics/core/Conductor.h"
#include "lyrics/core/Config.h"

namespace bf::lyrics {

namespace {
float ease(float dt, float tau) { return tau <= 0 ? 1.0f : 1.0f - std::exp(-dt / tau); }
SceneGrade g(float r, float gg, float b, float amount, float sat, float con, float vig, float fr, float fg, float fb) {
    SceneGrade x;
    x.tint[0] = r; x.tint[1] = gg; x.tint[2] = b;
    x.amount = amount; x.saturation = sat; x.contrast = con; x.vignette = vig;
    x.flash[0] = fr; x.flash[1] = fg; x.flash[2] = fb;
    return x;
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }
SceneGrade mix(SceneGrade const& a, SceneGrade const& b, float t) {
    SceneGrade o;
    for (int k = 0; k < 3; ++k) { o.tint[k] = lerp(a.tint[k], b.tint[k], t); o.flash[k] = lerp(a.flash[k], b.flash[k], t); }
    o.amount = lerp(a.amount, b.amount, t); o.saturation = lerp(a.saturation, b.saturation, t);
    o.contrast = lerp(a.contrast, b.contrast, t); o.vignette = lerp(a.vignette, b.vignette, t);
    return o;
}
// per reactivity level: how much of the grade / motion we apply
constexpr float kGrade[4] = {0.55f, 0.8f, 1.0f, 1.15f};
constexpr float kMove[4]  = {0.35f, 0.7f, 1.0f, 1.3f};
} // namespace

SceneGrade gradeForScene(std::string_view id) {
    //                                   tint r g b         amt   sat   con   vig   flash r g b
    if (id == "celestial_dream")   return g(0.92f, 0.94f, 1.10f, 0.35f, 1.05f, 1.02f, 0.18f, 0.85f, 0.80f, 1.00f);
    if (id == "cosmic_collapse")   return g(0.95f, 0.85f, 1.12f, 0.45f, 1.10f, 1.12f, 0.32f, 0.90f, 0.70f, 1.00f);
    if (id == "liquid_paradise")   return g(0.82f, 1.00f, 1.10f, 0.40f, 1.05f, 1.00f, 0.20f, 0.60f, 0.95f, 1.00f);
    if (id == "neon_afterlife")    return g(1.08f, 0.85f, 1.12f, 0.40f, 1.22f, 1.10f, 0.26f, 1.00f, 0.40f, 0.95f);
    if (id == "dreamcore")         return g(1.05f, 0.97f, 1.04f, 0.35f, 0.86f, 0.92f, 0.10f, 1.00f, 0.85f, 0.95f);
    if (id == "anime_impact")      return g(1.00f, 0.96f, 0.92f, 0.30f, 1.20f, 1.15f, 0.24f, 1.00f, 0.95f, 0.85f);
    if (id == "cinematic_romance") return g(1.10f, 0.98f, 0.84f, 0.45f, 1.04f, 1.04f, 0.22f, 1.00f, 0.85f, 0.60f);
    if (id == "melancholy")        return g(0.86f, 0.94f, 1.10f, 0.45f, 0.72f, 0.96f, 0.30f, 0.70f, 0.80f, 1.00f);
    if (id == "ethereal_fantasy")  return g(1.00f, 0.98f, 1.06f, 0.25f, 1.08f, 0.96f, 0.12f, 1.00f, 1.00f, 1.00f);
    if (id == "dark_cosmic")       return g(0.90f, 0.86f, 1.08f, 0.45f, 0.95f, 1.12f, 0.36f, 0.80f, 0.60f, 1.00f);
    if (id == "cyber_edit")        return g(0.86f, 1.08f, 0.92f, 0.40f, 1.10f, 1.14f, 0.28f, 0.50f, 1.00f, 0.60f);
    if (id == "blood_moon")        return g(1.14f, 0.82f, 0.80f, 0.50f, 1.00f, 1.12f, 0.34f, 1.00f, 0.45f, 0.40f);
    if (id == "astral_symphony")   return g(1.02f, 0.96f, 1.08f, 0.30f, 1.10f, 1.04f, 0.22f, 1.00f, 0.85f, 0.55f);
    if (id == "eclipse_heartbeat") return g(1.10f, 0.84f, 0.84f, 0.45f, 1.00f, 1.16f, 0.38f, 1.00f, 0.40f, 0.30f);
    if (id == "dream_ocean")       return g(1.04f, 0.98f, 1.04f, 0.30f, 0.92f, 0.95f, 0.10f, 0.80f, 1.00f, 0.95f);
    if (id == "meteor_chorus")     return g(0.92f, 0.96f, 1.10f, 0.35f, 1.06f, 1.08f, 0.26f, 0.80f, 0.90f, 1.00f);
    if (id == "album_glow")        return g(1.00f, 1.00f, 1.00f, 0.00f, 1.10f, 1.04f, 0.18f, 1.00f, 1.00f, 1.00f);
    return g(1, 1, 1, 0, 1.0f, 1.0f, 0.10f, 1, 1, 1);
}

void Cinema::step(MusicState const* m, Config const& cfg, float dt, float camYaw, uint32_t accent) {
    dt = std::clamp(dt, 0.0f, 0.25f);
    int const  r      = std::clamp(cfg.reactivity, 0, 3);
    bool const on     = m && m->active && cfg.director;
    // scene crossfade
    if (!mHave || std::strncmp(mScene, cfg.scene.c_str(), sizeof mScene - 1) != 0) {
        SceneGrade const t = gradeForScene(cfg.scene);
        if (!mHave) { mCur = mFrom = mTo = t; mBlend = 1; mHave = true; }
        else { mFrom = mCur; mTo = t; mBlend = 0; mPendingFade = true; mWaitBeat = 0; } // starts on the next downbeat
        std::strncpy(mScene, cfg.scene.c_str(), sizeof mScene - 1);
        mScene[sizeof mScene - 1] = 0;
    }
    if (mPendingFade) { // [BF] synced transition: the new look lands on a beat (at most 0.8 s later)
        mWaitBeat += dt;
        if (!m || !m->active || m->beat > 0.55f || m->impact > 0.5f || mWaitBeat > 0.8f) mPendingFade = false;
    }
    if (mBlend < 1 && !mPendingFade) {
        mBlend = std::min(1.0f, mBlend + dt / 1.6f);
        float const s = mBlend * mBlend * (3 - 2 * mBlend);
        mCur = mix(mFrom, mTo, s);
    }
    mOn += ((on ? 1.0f : 0.0f) - mOn) * ease(dt, 0.8f); // the whole look fades in / out with the music

    // impact frames
    mSinceFlash += dt;
    float const mv = kMove[r] * std::clamp(cfg.intensity, 0.3f, 1.6f); // [BF] Cinema home: one intensity dial
    if (on && cfg.impactFrames && mSinceFlash > kFlashGap && (m->dropNow || m->chorusNow)) {
        mFlash = m->dropNow ? 1.0f : 0.55f;
        // a drop after a held breath lands hardest: the contrast between the hush and the hit is the moment
        mFlashCap = m->dropNow ? (m->bigDrop || mHold > 0.3f ? kDropFlashCap : 0.09f) : kFlashCap;
        mKick  = m->dropNow ? 1.0f : 0.6f;
        if (m->finalHit && cfg.songArc) { mKick = 1.35f; mFlashCap = kDropFlashCap; mFlash = 1.0f; } // [BF] the last one is the biggest
        mSinceFlash = 0;
    }
    if (on && cfg.fovBreath && (m->dropNow || m->chorusNow)) mFovPunch = m->dropNow ? 1.0f : 0.5f;
    mFlash *= std::exp(-dt / 0.45f); // [LyricsForge] a soft swell of light, not a pop
    mHeart += (mHeartIn - mHeart) * ease(dt, 0.35f); // [LyricsForge] heartbeat as a slow breath, never a blink
    mFovPunch *= std::exp(-dt / 0.45f);
    mKick  *= std::exp(-dt / 0.5f);
    // [LyricsForge] the hits reach bloom / contrast / saturation as a slow swell (0.35 s in, 1 s out), at half strength:
    // the raw kick pumped the whole floor's glow on every hit, which read as flicker on dark maps
    float const impRaw = on && cfg.impactFrames ? std::max(mKick, m->impact * 0.6f) : 0.0f;
    mImpS += (0.5f * impRaw - mImpS) * ease(dt, 0.5f * impRaw > mImpS ? 0.35f : 1.0f);
    float const imp = mImpS;
    // held breath (Conductor.hold) and the chorus look (lift), both gated by the Drop moment / Chorus looks toggles
    float const hold = on && cfg.dropMoment ? m->hold : 0.0f;
    float const lift = on && cfg.chorusLooks ? m->lift : 0.0f;
    mHold = std::max(hold, mHold * std::exp(-dt / 0.3f)); // remembered a moment so the release sees it

    // letterbox + vignette breathe with the build and big hooks
    float box = 0, vig = 0;
    if (on && cfg.letterbox) {
        box = m->build * 0.85f;
        if (r >= 2) box = std::max(box, m->inHook * (m->section == MusicSection::High ? 0.6f : 0.3f));
        if (m->section == MusicSection::Calm) box *= 0.5f;
        box = std::clamp(box * mv, 0.0f, 1.0f) * kLetterMax;
    }
    if (on) vig = 0.18f * m->build + 0.02f * m->beat * m->budget + 0.02f * m->bass * m->budget; // [LyricsForge] a faint breath, not a per-beat blink
    vig += 0.30f * hold;                                         // the edges close in while it holds its breath
    float const outro = on && cfg.songOutro ? m->outro : 0.0f;   // [BF] end credits: the world winds down
    vig += 0.40f * outro;
    float const bridge = on && cfg.songArc ? m->bridge : 0.0f;  // [BF] the bridge: quiet, cooler, a little strange
    vig += 0.12f * bridge + 0.07f * mHeart;
    if (on && cfg.letterbox) box = std::max(box, kLetterMax * outro);
    if (on && cfg.letterbox) box = std::max(box, kLetterMax * 0.9f * hold);
    mBox += (box - mBox) * ease(dt, box > mBox ? 0.9f : 0.6f);
    mVig += (vig - mVig) * ease(dt, hold > 0.05f ? 0.3f : 0.8f);

    float const ga = cfg.cinemaGrade ? kGrade[r] * mOn * std::clamp(cfg.intensity, 0.3f, 1.3f) : 0.0f;
    CinemaOut o;
    o.active = mOn > 0.01f || mBox > 0.001f || mFlash > 0.01f;
    { // [BF] per-song identity: the cover's colour pulls the scene's tint a little (two songs in one scene differ)
        float want[3] = {1, 1, 1};
        if (cfg.songTint && accent) {
            float const r = float((accent >> 16) & 255) / 255.0f, g = float((accent >> 8) & 255) / 255.0f, b = float(accent & 255) / 255.0f;
            float const mx = std::max({r, g, b, 0.05f});
            want[0] = r / mx; want[1] = g / mx; want[2] = b / mx; // hue only, full brightness
        }
        for (int k = 0; k < 3; ++k) mAcc[k] += (want[k] - mAcc[k]) * ease(dt, 1.2f);
    }
    float const accMax = std::max({mAcc[0], mAcc[1], mAcc[2]});
    for (int k = 0; k < 3; ++k) {
        o.tint[k] = mCur.tint[k] * (0.82f + 0.18f * mAcc[k]) / (0.82f + 0.18f * accMax);
        o.flashColor[k] = mCur.flash[k] * (0.7f + 0.3f * mAcc[k]);
    }
    if (bridge > 0.001f) { o.tint[0] *= 1.0f - 0.05f * bridge; o.tint[2] *= 1.0f + 0.06f * bridge; } // cooler
    // music lifts saturation / contrast a touch with the budget, calm sections stay softer
    float const up = on ? (m->budget - 0.5f) * 0.12f + imp * 0.10f + lift * 0.07f : 0.0f;
    o.amount     = std::clamp(mCur.amount * ga * (1.0f + 0.35f * lift), 0.0f, 1.0f);
    // the hush drains colour (down to ~60%) and lifts contrast a touch; the chorus look is richer
    o.saturation = (1.0f + (mCur.saturation - 1.0f) * ga + up * mv) * (1.0f - 0.4f * hold) * (1.0f - 0.3f * outro) * (1.0f - 0.28f * bridge);
    o.contrast   = 1.0f + (mCur.contrast - 1.0f) * ga + up * 0.6f * mv + 0.06f * hold;
    o.vignette   = std::clamp(mCur.vignette * ga + mVig * mv, 0.0f, 0.65f);
    o.flash      = std::min(mFlashCap, mFlash * mFlashCap * std::min(1.0f, mv));
    o.letterbox  = mBox;
    o.bloomBoost = imp * 0.10f * mv + 0.04f * lift;
    o.caBoost    = imp * 0.0022f * mv;
    // camera breath: a little tighter while a build holds its breath, a quick push out on the drop (max +-5%)
    float const fovT = on && cfg.fovBreath ? 1.0f - 0.035f * m->build * mv - 0.02f * hold + 0.05f * mFovPunch * std::min(1.0f, mv) : 1.0f;
    mFov += (fovT - mFov) * ease(dt, fovT > mFov ? 0.08f : 0.5f);
    o.fovScale = std::clamp(mFov, 0.94f, 1.06f);
    // Showcase: the camera leaves your eyes and sways in a slow arc behind you, soft focus at the edges, bars
    if (cfg.showcase && mShow < 0.001f) { mShowAge = 0; mShowYaw = camYaw; }
    mShow += ((cfg.showcase ? 1.0f : 0.0f) - mShow) * ease(dt, 0.7f);
    if (mShow > 0.001f) {
        mShowAge += dt;
        mShowYaw += (camYaw - mShowYaw) * ease(dt, 2.5f);              // follows where you look, lazily
        float const sway = 48.0f * std::sin(mShowAge * 0.13f);          // a slow arc around your back
        float const breath = on ? 0.5f * m->budget + 0.4f * m->impact : 0.0f;
        // [BF] Director cuts: a hard angle change on the hit - low hero shot on a drop, side / wide on a chorus,
        // a calm wide shot when it calms down, back to the sway when the chorus look ends. Cut on a beat.
        mCutT += dt;
        if (on && cfg.directorCuts) {
            int want = -1;
            if (m->dropNow) want = (mCutSeq++ % 2) ? 2 : 1;
            else if (m->chorusNow) want = (mCutSeq++ % 2) ? 3 : 4 + (mCutSeq % 2);
            else if (m->calmNow) want = 6;
            else if (m->liftEnd) want = 0;
            else if (m->lifted && mCutT > 7.0f && m->beat > 0.6f) want = (mShot + 1 + (mCutSeq++ % 3)) % 6; // keep cutting in the chorus
            else if (cfg.instrumentalFx && m->instrumental > 0.6f && mCutT > 5.5f && m->beat > 0.6f)
                want = (mShot + 1 + (mCutSeq++ % 4)) % 7;                 // instrumentals: the camera is the dancer
            if (want >= 0 && want != mShot) { mNextShot = want; mCutWait = 0; }
        } else if (mShot != 0 && !(on && m->lifted)) { mNextShot = 0; mCutWait = 0; }
        if (mCutWait >= 0) { // drops / chorus hits are already on the beat; other cuts wait for the next one (<= 0.5 s)
            mCutWait += dt;
            bool const onHit = on && (m->dropNow || m->chorusNow || m->beat > 0.55f);
            if (onHit || mCutWait > 0.5f) { mShot = mNextShot; mCutT = 0; mCutWait = -1; mCuts++; }
        }
        Shot const& sh = kShots[std::clamp(mShot, 0, int(std::size(kShots)) - 1)];
        float const dolly = std::min(1.0f, mCutT / 6.0f);              // each shot creeps in and around a little
        o.showcase    = mShow;
        o.orbitYaw    = mShowYaw + (mShot == 0 ? sway : sh.yaw + 7.0f * dolly);
        o.orbitPitch  = mShot == 0 ? 13.0f + 5.0f * std::sin(mShowAge * 0.21f) : sh.pitch;
        o.orbitRadius = mShot == 0 ? 3.6f + breath : sh.radius * (1.0f - 0.08f * dolly) + 0.3f * breath;
        o.focus       = 0.55f * mShow;
        o.letterbox   = std::max(o.letterbox, 0.06f * mShow);
        o.active      = true;
    }
    if (float const k = cfg.combatCalm ? std::clamp(mCombat, 0.0f, 1.0f) : 0.0f; k > 0.001f) {
        // [LyricsForge] PvP calm: the camera is back in your eyes at once, no bars / flashes / FOV pulses / dark edges,
        // the colour look stays at half strength so the world still looks like the song
        float const off = 1.0f - k;
        o.showcase *= off; o.focus *= off;
        o.letterbox *= off; o.vignette *= off; o.flash *= off;
        o.bloomBoost *= off; o.caBoost *= off;
        o.fovScale = 1.0f + (o.fovScale - 1.0f) * off;
        o.amount *= 1.0f - 0.5f * k;
        o.saturation = 1.0f + (o.saturation - 1.0f) * (1.0f - 0.5f * k);
        o.contrast = 1.0f + (o.contrast - 1.0f) * off;
    }
    mOut = o;
}

} // namespace bf::lyrics
