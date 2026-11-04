// LyricsFlow port - frame builder + shared frame store. Original code for Bedrock Forge; see Engine.h.
#include "lyrics/core/Engine.h"

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
        mFx.step(in.dt);
        mFx.emit(mOrigin, mGlyphs);
    }
    if (cfg.fallingLetters || cfg.exitAnim == ExitAnim::Fall || !mPhysics.letters().empty()) {
        mPhysics.step(in.dt, world, pp);
        std::vector<BfGlyphV1> letters;
        mPhysics.emit(mOrigin, letters);
        mGlyphs.insert(mGlyphs.begin(), letters.begin(), letters.end()); // letters on the ground sit behind live text
    }
    { // [BF] the sky writes the song: constellations sit behind everything else
        SkyWriter::In si;
        si.t = in.t; si.songSeq = in.songSeq; si.playing = in.playing; si.lyrics = in.lyrics; si.music = in.music;
        si.dt = in.dt; si.camYaw = cam.yaw; si.camPitch = cam.pitch; si.camPos = cam.pos; si.now = in.nowSeconds;
        si.enabled = cfg.constellations && cfg.showLyrics;
        Theme const th = currentTheme(cfg);
        si.color = cfg.letterFxColor != 0 ? uint32_t(cfg.letterFxColor) : th.a;
        mSkyWriter.step(si);
        std::vector<BfGlyphV1> stars;
        mSkyWriter.emit(si, mOrigin, stars);
        if (!stars.empty()) mGlyphs.insert(mGlyphs.begin(), stars.begin(), stars.end());
    }
    // the karaoke bar last: screen space, drawn over the world text
    mTopBar.build(in, cfg, mFonts, in.guiH > 0 ? in.guiW / in.guiH : 16.0f / 9.0f, mGlyphs);
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
