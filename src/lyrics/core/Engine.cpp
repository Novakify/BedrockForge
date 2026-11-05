// LyricsFlow port - frame builder + shared frame store. Original code for Bedrock Forge; see Engine.h.
#include "lyrics/core/Engine.h"
#include "lyrics/core/Moments.h"
#include "lyrics/core/Text.h"

#include <cstddef>

#include <algorithm>
#include <cstring>

namespace bf::lyrics {

void Engine::reset() {
    mScene.clear();
    mPhysics.clear();
    mTopBar.reset();
    mGlyphs.clear();
    mSkyWriter.clear();
}

void Engine::frame(SceneInput const& in, CameraState const& cam, IWorld* world) {
    mCam    = cam;
    mOrigin = cam.pos; // glyphs are camera-relative: small floats, and RF's view matrix is camera-relative too
    if (in.songSeq != mSongSeq) {
        mSongSeq = in.songSeq;
        mPhysics.clear();
        mFx.clear();
    }
    // quality + Auto FPS: the frame renders with the costly effects dropped (the saved config is untouched)
    Config cfg = effectiveConfig(mCfg, mPerf);
    MusicState const* m = cfg.director && in.music && in.music->active ? in.music : nullptr;
    // [BF] reactivity preset: how far apart calm and full are (Soft .. Cinematic)
    static constexpr float kCalm[4] = {0.75f, 0.55f, 0.45f, 0.35f}, kFull[4] = {1.05f, 1.25f, 1.4f, 1.5f}, kImp[4] = {0.5f, 1.0f, 1.3f, 1.5f};
    int const rx = std::clamp(cfg.reactivity, 0, 3);
    if (m) cfg.letterFxIntensity *= kCalm[rx] + (kFull[rx] - kCalm[rx]) * m->budget + 0.5f * kImp[rx] * m->impact; // calm verses simmer, choruses burn
    { // [BF] shot language: calm sections frame the words wide (further, smaller), the big moments go close-up
        float want = 1.0f;
        if (m && cfg.shotLanguage && rx >= 1 && !cfg.pinned)
            want = m->section == MusicSection::Calm ? 1.16f : m->section == MusicSection::High ? 0.87f - 0.05f * m->impact : 1.0f;
        mShot += (want - mShot) * std::min(1.0f, in.dt / 2.0f);
        cfg.lyricsDistance *= mShot;
        cfg.lyricsScale *= 1.0f + (1.0f - mShot) * 0.5f;
    }
    if (m && cfg.chorusLooks) { // [BF] chorus transformation: the letters change material and break apart differently
        if (m->lifted) {
            if (in.chorusExit >= 0 && !cfg.fallingLetters) cfg.exitAnim = ExitAnim(std::clamp(in.chorusExit, 0, int(ExitAnim::Vortex)));
            if (in.chorusFx >= 0) cfg.letterFx = std::clamp(in.chorusFx, 0, 10);
        }
        cfg.letterFxIntensity *= 1.0f + 0.3f * m->lift;
        cfg.glowStrength = std::min(1.0f, cfg.glowStrength * (1.0f + 0.25f * m->lift));
    }
    if (m && cfg.dropMoment && m->hold > 0.01f) { // [BF] held breath: the letters go quiet too
        cfg.letterFxIntensity *= 1.0f - 0.6f * m->hold;
        cfg.glowStrength *= 1.0f - 0.5f * m->hold;
    }
    if (cfg.albumColors && in.albumAccent != 0) { // [BF] album-art palette: colours ease to the new cover over ~1 s
        float const k = std::min(1.0f, in.dt * 2.5f);
        for (int c = 0; c < 3; ++c) {
            float const want = float((in.albumAccent >> (16 - 8 * c)) & 0xFF);
            mAccentF[c] = mAccentF[c] < 0 ? want : mAccentF[c] + (want - mAccentF[c]) * k;
        }
        mAccent = 0xFF000000u | uint32_t(std::lround(mAccentF[0])) << 16 | uint32_t(std::lround(mAccentF[1])) << 8 | uint32_t(std::lround(mAccentF[2]));
        cfg.letterFxColor = int32_t(mAccent | 0xFF000000u);
        cfg.glowColor     = int32_t(mAccent | 0xFF000000u);
    }
    if (cfg.contrastGuard && in.skyLuma > 0.4f) { // [BF] light text on a light sky: give it a dark edge it can stand on
        float const k = std::clamp((in.skyLuma - 0.4f) / 0.35f, 0.0f, 1.0f);
        cfg.outlineWidth = std::max(cfg.outlineWidth, 0.08f + 0.14f * k);
        if (cfg.outlineColor == 0) cfg.outlineColor = int64_t(0xFF1A0E24u);
        cfg.textShadow = std::max(cfg.textShadow, k > 0.5f ? 2 : 1);
    }
    auto width = [this](std::string_view s, std::string_view f) { return mFonts.width(s, f); };
    mScene.tick(in, cfg, cam, world, width);
    auto words = mScene.draw(in, cfg, cam, world);
    if (m) direct(words, *m, cfg);
    if (cfg.titleCard && in.music && in.music->songAge < kTitleCardSec) titleCard(words, in, cfg, cam);
    if (cfg.songOutro && in.music && in.music->outro > 0.01f) endCredits(words, in, cfg, cam);

    // painter's order: far words first (we draw without depth test, like the Java mod's SEE_THROUGH text)
    std::stable_sort(words.begin(), words.end(), [&](DrawWord const& a, DrawWord const& b) {
        if (a.screen != b.screen) return !a.screen; // screen text on top
        return a.origin.dist(cam.pos) > b.origin.dist(cam.pos);
    });

    mGlyphs.clear();
    GlyphStyle const    st = makeStyle(cfg, float(in.nowSeconds));
    PhysicsParams const pp = PhysicsParams::from(cfg);
    std::vector<BfGlyphV1> drop;
    for (auto const& w : words) {
        Font const* f = mFonts.get(w.font);
        if (!f) continue;
        if (w.dropNow) {
            drop.clear();
            buildWordGlyphs(w, *f, cfg, st, mOrigin, drop, /*mainPassOnly*/ true);
            if (LetterFx::handles(w.dropFx)) mFx.spawn(drop, mOrigin, w.dropFx, w.key ^ (mSongSeq << 20), cfg.fxIntensity);
            else mPhysics.spawn(drop, mOrigin, w.key ^ (mSongSeq << 20), pp);
            continue;
        }
        buildWordGlyphs(w, *f, cfg, st, mOrigin, mGlyphs);
    }
    if (mFx.size()) { // [BF] fragments of words that broke apart
        mFx.step(in.dt * (m ? std::clamp(m->timeScale, 0.25f, 2.5f) : 1.0f)); // [BF] speed ramp
        mFx.emit(mOrigin, mGlyphs);
    }
    // [BF] everything that sits BEHIND the live words, back to front: motes, portal, constellations, fallen letters +
    // their impact bits, the ribbon. Built into one reused buffer and put in front of the words with ONE insert
    // (was five front-inserts of temporary vectors per frame).
    Theme const th  = currentTheme(cfg);
    uint32_t const tint = cfg.letterFxColor != 0 ? uint32_t(cfg.letterFxColor) : th.a;
    mBack.clear();
    {
        Atmos::In ai; // atmosphere: motes in the air, same tint as the stars
        ai.music = in.music; ai.dt = in.dt; ai.camPos = cam.pos; ai.camYaw = cam.yaw; ai.camPitch = cam.pitch;
        ai.color = tint; ai.now = in.nowSeconds; ai.enabled = cfg.atmosphere && cfg.director && cfg.showLyrics;
        ai.density = 1.0f - 0.25f * float(std::clamp(mPerf.level(), 0, 3));
        mAtmos.step(ai);
        mAtmos.emit(ai, mOrigin, mBack);
        Pulse::In ui; // instrumentals: ground rings on the kicks, the horizon equaliser
        ui.music = in.music; ui.dt = in.dt; ui.camYaw = cam.yaw; ui.now = in.nowSeconds; ui.color = tint;
        ui.enabled = cfg.instrumentalFx && cfg.director;
        { // [BF] what fits the sky: a neon equaliser belongs to night / space / neon skies, not a pastel or storm sky
            SkyFamily const f = skyFamily(in.skyShown.empty() ? std::string_view(cfg.skyPreset) : std::string_view(in.skyShown));
            ui.bars = f != SkyFamily::Warm && f != SkyFamily::Storm && f != SkyFamily::Day;
        }
        ui.density = 1.0f - 0.25f * float(std::clamp(mPerf.level(), 0, 3));
        mPulse.step(ui);
        mPulse.emit(ui, mBack);
        Portal::In pi; // musical portal
        pi.music = in.music; pi.dt = in.dt; pi.camYaw = cam.yaw; pi.camPitch = cam.pitch; pi.now = in.nowSeconds; pi.color = tint;
        pi.enabled = cfg.portals && cfg.director && cfg.showLyrics;
        mPortal.step(pi);
        mPortal.emit(pi, mBack);
        SkyWriter::In si; // the sky writes the song
        si.t = in.t; si.songSeq = in.songSeq; si.playing = in.playing; si.lyrics = in.lyrics; si.music = in.music;
        si.dt = in.dt; si.camYaw = cam.yaw; si.camPitch = cam.pitch; si.camPos = cam.pos; si.now = in.nowSeconds; si.skyLuma = in.skyLuma;
        { // [BF] the sky writes in stars only where there are stars to write with (night / space / aurora / cold)
            SkyFamily const f = skyFamily(in.skyShown.empty() ? std::string_view(cfg.skyPreset) : std::string_view(in.skyShown));
            bool const starry = f == SkyFamily::Space || f == SkyFamily::Aurora || f == SkyFamily::Cold || f == SkyFamily::Neon;
            si.enabled = cfg.constellations && cfg.showLyrics && starry;
        }
        si.color = tint;
        mSkyWriter.step(si);
        mSkyWriter.emit(si, mOrigin, mBack);
    }
    if (cfg.fallingLetters || cfg.exitAnim == ExitAnim::Fall || !mPhysics.letters().empty() || mBursts.size()) {
        mPhysics.step(in.dt, world, pp);
        mPhysics.emit(mOrigin, mBack); // letters on the ground sit behind live text
        if (cfg.materialImpacts) { // stone chips, sand puffs, water splashes ... where letters land
            uint32_t k = 0;
            for (auto const& ld : mPhysics.takeLandings()) mBursts.spawn(ld, uint32_t(uint64_t(in.nowSeconds * 1000.0)) + 7919u * ++k);
        } else
            mPhysics.takeLandings();
        mBursts.step(in.dt);
        mBursts.emit(mOrigin, cam.yaw, cam.pitch, mBack);
    }
    mRibbon.step(words, width, mOrigin, in.nowSeconds, m ? m->energy : 0.0f, cfg.wordRibbon && cfg.showLyrics);
    mRibbon.emit(tint, m ? std::clamp(m->budget, 0.0f, 1.0f) : 0.5f, mBack);
    if (!mBack.empty()) mGlyphs.insert(mGlyphs.begin(), mBack.begin(), mBack.end());
    // the karaoke bar last: screen space, drawn over the world text
    mTopBar.build(in, cfg, mFonts, in.guiH > 0 ? in.guiW / in.guiH : 16.0f / 9.0f, mGlyphs);
    PlayerHud::Extra px; // [BF] corner music player
    px.durationMs = in.durationMs; px.coverAtlas = in.coverAtlas; px.coverAccent = in.albumAccent;
    mPlayer.build(in, px, cfg, mFonts, mGlyphs);
}

// [BF] the Director's touch on the lyric words: glow follows the energy budget, held notes become hero words,
// drops / choruses punch the sung words for a moment. Calm verses stay readable (never below ~55 % glow).
void Engine::direct(std::vector<DrawWord>& words, MusicState const& m, Config const& cfg) {
    static constexpr float kCalm[4] = {0.75f, 0.55f, 0.45f, 0.35f}, kFull[4] = {1.05f, 1.25f, 1.4f, 1.5f}, kImp[4] = {0.5f, 1.0f, 1.3f, 1.5f};
    int const   rx    = std::clamp(cfg.reactivity, 0, 3);
    float const glowK = directed(m, kCalm[rx], kFull[rx]);
    float const imp   = m.impact * kImp[rx];
    for (auto& w : words) {
        if (w.plain) continue;
        w.glow = w.glow * glowK + 0.25f * m.inHook;
        if (w.sung) {
            w.glow += 0.45f * imp;
            w.scale *= 1.0f + 0.10f * imp;
            if (cfg.heroWords && w.hero) { w.scale *= 1.08f; w.glow += 0.5f; }
        }
    }
    // [BF] echo: held notes ring out - two transparent copies swell and fade behind the word, like reverb
    if (cfg.heroWords) {
        size_t const n = words.size();
        words.reserve(n * 3); // push_back below must never reallocate under `w` (it is a reference into words)
        for (size_t i = 0; i < n; ++i) {
            DrawWord const& w = words[i];
            if (!w.hero || w.sungMs < 0 || w.sungMs > 2600 || w.plain || w.dropNow) continue;
            for (int e = 0; e < 2; ++e) {
                float const ph = std::fmod(float(w.sungMs) / 900.0f + 0.5f * float(e), 1.0f);
                DrawWord c = w;
                c.scale *= 1.0f + 0.45f * ph;
                c.alpha *= 0.32f * (1.0f - ph) * std::min(1.0f, float(w.sungMs) / 200.0f);
                c.glow  *= 0.6f;
                c.y     += (w.screen ? 0.25f : -0.25f) * ph * w.scale;
                c.key    = w.key ^ (int64_t(0x5EC0) << (8 * (e + 1)));
                c.hero   = false;
                if (c.alpha > 0.01f) words.push_back(std::move(c));
            }
        }
    }
}

// [BF] title card: the song's title and artist, centred in the upper third, fading in and out over ~4 s with a
// slow push-in - every song starts like a scene.
void Engine::titleCard(std::vector<DrawWord>& words, SceneInput const& in, Config const& cfg, CameraState const& cam) {
    if (in.title.empty() || !in.playing) return;
    float const age = in.music->songAge;
    float const a   = std::min({1.0f, age / 0.6f, std::max(0.0f, (kTitleCardSec - age) / 0.9f)});
    if (a <= 0.01f) return;
    float const push   = 1.0f + 0.04f * (age / kTitleCardSec);
    float const aspect = cam.screenAspect > 0 ? cam.screenAspect : 16.0f / 9.0f;
    auto card = [&](std::string const& text, float y, float size, float alpha) {
        DrawWord d;
        d.text   = text;
        d.font   = cfg.font;
        d.screen = true;
        d.origin = {double(aspect) * 0.5, double(y), 0};
        d.orient = Quat::rotationZ(0);
        d.scale  = size * push;
        d.alpha  = alpha;
        d.glow   = 0.6f;
        d.sung   = true;
        d.key    = -77 - int64_t(y * 1000);
        words.push_back(std::move(d));
    };
    card(in.title, 0.27f, 0.085f, a);
    if (!in.artist.empty()) card(in.artist, 0.36f, 0.042f, a * 0.8f);
}

// [BF] end credits: over the song's last seconds the title, the artist and its last lyric line rise slowly up the
// screen and fade, like a film's credits; the Cinema dims and frames the world around them.
void Engine::endCredits(std::vector<DrawWord>& words, SceneInput const& in, Config const& cfg, CameraState const& cam) {
    if (in.title.empty()) return;
    float const o = in.music->outro;
    float const a = std::min(1.0f, o * 1.4f);
    float const aspect = cam.screenAspect > 0 ? cam.screenAspect : 16.0f / 9.0f;
    float const rise = 0.05f * o;                                  // drifting up as it ends
    auto card = [&](std::string const& text, float y, float size, float alpha, uint32_t argb) {
        DrawWord d;
        d.text = text; d.font = cfg.font; d.screen = true;
        d.origin = {double(aspect) * 0.5, double(y - rise), 0};
        d.orient = Quat::rotationZ(0);
        d.scale = size; d.alpha = alpha; d.glow = 0.5f; d.sung = true; d.argb = argb;
        d.key = -177 - int64_t(y * 1000);
        words.push_back(std::move(d));
    };
    std::string last; // the song's last lyric line lingers above the credits
    if (in.lyrics)
        for (auto it = in.lyrics->lines.rbegin(); it != in.lyrics->lines.rend(); ++it)
            if (it->index >= 0 && !it->text.empty()) { last = it->text; break; }
    if (!last.empty() && text::length(last) <= 48) card("\xE2\x80\x9C" + last + "\xE2\x80\x9D", 0.30f, 0.034f, a * 0.7f, 0xFFE8E4F0u);
    card(in.title, 0.40f, 0.075f, a, 0);
    if (!in.artist.empty()) card(in.artist, 0.48f, 0.038f, a * 0.8f, 0);
}

void Engine::publish(bool active, float fovY, std::vector<BfGlyphV1> const* extra) {
    BfLyricFrameV1 h{};
    bool const lyricsOn = active && mCfg.enabled && mCfg.showLyrics;
    bool const panelOn  = extra && !extra->empty();
    h.active    = lyricsOn || panelOn ? 1u : 0u;
    h.origin[0] = mOrigin.x;
    h.origin[1] = mOrigin.y;
    h.origin[2] = mOrigin.z;
    h.camYaw    = mCam.yaw;
    h.camPitch  = mCam.pitch;
    h.camFovY   = fovY;
    if (!panelOn) { FrameStore::get().publish(h, lyricsOn ? mGlyphs : std::vector<BfGlyphV1>{}); return; }
    std::vector<BfGlyphV1> all;
    all.reserve((lyricsOn ? mGlyphs.size() : 0) + extra->size());
    if (lyricsOn) all = mGlyphs;
    all.insert(all.end(), extra->begin(), extra->end()); // the panel draws last = on top
    FrameStore::get().publish(h, all);
}

// ---------------------------------------------------------------- store
FrameStore& FrameStore::get() {
    static FrameStore s;
    return s;
}
void FrameStore::publish(BfLyricFrameV1 const& hdr, std::vector<BfGlyphV1> const& glyphs) {
    std::lock_guard l(mMutex);
    uint32_t frame = mHdr.frame + 1, gen = mHdr.atlasGeneration, n = mHdr.atlasCount;
    char     files[kBfLyricsMaxAtlases][260];
    std::memcpy(files, mHdr.atlasFiles, sizeof files);
    mHdr                 = hdr;
    mHdr.size            = sizeof(BfLyricFrameV1);
    mHdr.version         = 1;
    mHdr.frame           = frame;
    mHdr.atlasGeneration = gen;
    mHdr.atlasCount      = n;
    std::memcpy(mHdr.atlasFiles, files, sizeof files);
    mGlyphs = glyphs;
    mInit   = true;
}
void FrameStore::setAtlases(std::vector<std::string> const& files) {
    std::lock_guard l(mMutex);
    std::memset(mHdr.atlasFiles, 0, sizeof mHdr.atlasFiles);
    mHdr.atlasCount = uint32_t(std::min<size_t>(files.size(), kBfLyricsMaxAtlases));
    for (uint32_t i = 0; i < mHdr.atlasCount; ++i) std::strncpy(mHdr.atlasFiles[i], files[i].c_str(), 259);
    ++mHdr.atlasGeneration;
}
void FrameStore::setOverlay(std::vector<BfGlyphV1> glyphs) {
    std::lock_guard l(mMutex);
    if (glyphs.empty() && mOverlay.empty()) return;
    mOverlay = std::move(glyphs);
    ++mOverlaySeq;
}
uint32_t FrameStore::copy(BfLyricFrameV1* hdr, BfGlyphV1* dst, uint32_t cap) {
    std::lock_guard l(mMutex);
    if (!hdr) return 0;
    // The caller says how big its header is (hdr->size). RF builds from before the 16-atlas header zero it: they have
    // room for 8 atlas paths only, so never write past that (old RF + new BF must not corrupt RF's memory).
    size_t const legacy = offsetof(BfLyricFrameV1, atlasFiles) + size_t(8) * sizeof(mHdr.atlasFiles[0]);
    size_t const room   = hdr->size >= sizeof(BfLyricFrameV1) ? sizeof(BfLyricFrameV1) : hdr->size >= legacy ? size_t(hdr->size) : legacy;
    BfLyricFrameV1 h = mHdr;
    h.size           = sizeof(BfLyricFrameV1);
    h.version        = 1;
    bool const lyr   = mInit && mHdr.active;      // lyric glyphs only while the lyricsflow frame is live
    size_t const nl  = lyr ? mGlyphs.size() : 0;
    h.total          = uint32_t(nl + mOverlay.size());
    if (room < sizeof(BfLyricFrameV1)) h.atlasCount = std::min<uint32_t>(h.atlasCount, 8);
    uint32_t n = dst ? std::min<uint32_t>(cap, uint32_t(nl)) : 0;
    if (n) std::memcpy(dst, mGlyphs.data(), size_t(n) * sizeof(BfGlyphV1));
    if (dst && !mOverlay.empty()) { // the menu goes last (on top); it keeps room even when the lyrics fill the buffer
        uint32_t m = std::min<uint32_t>(cap, uint32_t(mOverlay.size()));
        n          = std::min<uint32_t>(n, cap - m);
        std::memcpy(dst + n, mOverlay.data(), size_t(m) * sizeof(BfGlyphV1));
        n += m;
    }
    h.count = n;
    if (!mInit) h.active = 0;
    if (!mOverlay.empty()) {
        h.active = 1;
        h.frame  = mHdr.frame * 7919u + mOverlaySeq; // keeps moving while the menu redraws (RF's freshness check)
    }
    std::memcpy(hdr, &h, room);
    return n;
}

} // namespace bf::lyrics

extern "C" BF_LYRICS_EXPORT uint32_t BF_CopyLyricFrame(BfLyricFrameV1* hdr, BfGlyphV1* dst, uint32_t capacity) {
    return bf::lyrics::FrameStore::get().copy(hdr, dst, capacity);
}
