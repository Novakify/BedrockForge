// LyricsFlow port - frame builder + shared frame store. Original code for Bedrock Forge; see Engine.h.
#include "lyrics/core/Engine.h"
#include "lyrics/core/Moments.h"
#include "lyrics/core/Text.h"

#include <cstddef>

#include <algorithm>
#include <string_view>
#include <cctype>
#include <cmath>
#include <cstring>

namespace bf::lyrics {

namespace {
// [LyricsForge] the same colour turned around the hue wheel (degrees), saturation and brightness kept
uint32_t hueTurn(uint32_t c, float deg) {
    float r = float((c >> 16) & 255) / 255.0f, g = float((c >> 8) & 255) / 255.0f, b = float(c & 255) / 255.0f;
    float const mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    if (d < 1e-4f) return c;
    float h = mx == r ? 60.0f * std::fmod((g - b) / d + 6.0f, 6.0f) : mx == g ? 60.0f * ((b - r) / d + 2.0f) : 60.0f * ((r - g) / d + 4.0f);
    h = std::fmod(h + deg + 360.0f, 360.0f);
    float const s = d / mx, v = mx, x = v * s * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f)), m = v - v * s;
    float rr = 0, gg = 0, bb = 0;
    int const sec = int(h / 60.0f) % 6;
    if (sec == 0) { rr = v * s; gg = x; } else if (sec == 1) { rr = x; gg = v * s; } else if (sec == 2) { gg = v * s; bb = x; }
    else if (sec == 3) { gg = x; bb = v * s; } else if (sec == 4) { rr = x; bb = v * s; } else { rr = v * s; bb = x; }
    auto q = [&](float f) { return uint32_t(std::clamp(int(std::lround((f + m) * 255.0f)), 0, 255)); };
    return (c & 0xFF000000u) | q(rr) << 16 | q(gg) << 8 | q(bb);
}
} // namespace

/// [LyricsForge] the colour a lyric word names ("Pink!", "blue", "golden", "flamingo" -> pink), 0 = not a colour word
uint32_t colorWord(std::string_view text) {
    std::string w;
    for (char ch : text) {
        unsigned char const u = static_cast<unsigned char>(ch);
        if (std::isalpha(u)) w.push_back(char(std::tolower(u)));
        else if (!w.empty() && u != '\'') break; // "pink," / "blue-eyed" -> the colour part
    }
    struct C { char const* n; uint32_t c; };
    static constexpr C kColors[] = {
        {"pink", 0xFF5FB4}, {"flamingo", 0xFF6FAE}, {"magenta", 0xFF2FD0}, {"fuchsia", 0xFF2FD0},
        {"red", 0xFF3B3B}, {"crimson", 0xE8203C}, {"scarlet", 0xFF2A1A}, {"cherry", 0xE8183C}, {"ruby", 0xE0115F},
        {"orange", 0xFF8A1E}, {"coral", 0xFF7F6A}, {"peach", 0xFFB08A}, {"amber", 0xFFB020},
        {"yellow", 0xFFE53B}, {"gold", 0xFFC93C}, {"golden", 0xFFC93C}, {"lemon", 0xFFF04A},
        {"green", 0x3BE26A}, {"lime", 0x9CFF3B}, {"emerald", 0x2ECC71}, {"mint", 0x7CFFC4},
        {"teal", 0x1FC8C0}, {"cyan", 0x2FE8FF}, {"turquoise", 0x30E0D0}, {"aqua", 0x3FF0FF},
        {"blue", 0x3B8BFF}, {"navy", 0x2A4BB8}, {"azure", 0x4FA8FF}, {"sapphire", 0x2F5FFF}, {"indigo", 0x5B4BFF},
        {"purple", 0xA45BFF}, {"violet", 0x9B5BFF}, {"lilac", 0xC8A2FF}, {"lavender", 0xC4B0FF}, {"plum", 0xB04FC8},
        {"white", 0xFFFFFF}, {"silver", 0xD8E0EA}, {"grey", 0xA8B0BC}, {"gray", 0xA8B0BC}, {"black", 0x2A2A34},
        {"brown", 0xB07040}, {"rainbow", 0xFF5FD0},
    };
    auto look = [&](std::string_view k) -> uint32_t {
        for (auto const& c : kColors) if (k == c.n) return c.c;
        return 0;
    };
    if (w.empty()) { // [LyricsForge] Japanese / Chinese colour words (no spaces: the word can hold more than the colour)
        // only forms that can't hide inside another word: bare 赤 / 青 are in 赤ちゃん (baby) / 青春 (youth), 白い in 面白い
        static constexpr C kCjk[] = {
            {"\xe3\x83\x94\xe3\x83\xb3\xe3\x82\xaf", 0xFF5FB4}, // pinku
            {"\xe6\xa1\x83\xe8\x89\xb2", 0xFF7FBF}, // momoiro
            {"\xe8\x99\xb9", 0xFF5FD0}, // niji (rainbow)
            {"\xe8\xb5\xa4\xe3\x81\x84", 0xFF3B3B}, // akai
            {"\xe8\xb5\xa4\xe8\x89\xb2", 0xFF3B3B}, // akairo
            {"\xe7\x9c\x9f\xe3\x81\xa3\xe8\xb5\xa4", 0xFF3B3B}, // makka
            {"\xe9\x9d\x92\xe3\x81\x84", 0x3B8BFF}, // aoi
            {"\xe9\x9d\x92\xe8\x89\xb2", 0x3B8BFF}, // aoiro
            {"\xe7\x9c\x9f\xe3\x81\xa3\xe9\x9d\x92", 0x3B8BFF}, // massao
            {"\xe7\xb7\x91\xe8\x89\xb2", 0x3BE26A}, // midoriiro
            {"\xe7\xb4\xab\xe8\x89\xb2", 0xA45BFF}, // murasakiiro
            {"\xe9\xbb\x84\xe8\x89\xb2", 0xFFE53B}, // kiiro
            {"\xe7\x99\xbd\xe8\x89\xb2", 0xFFFFFF}, // shiroiro
            {"\xe7\x9c\x9f\xe3\x81\xa3\xe7\x99\xbd", 0xFFFFFF}, // masshiro
            {"\xe9\xbb\x92\xe8\x89\xb2", 0x2A2A34}, // kuroiro
            {"\xe7\x9c\x9f\xe3\x81\xa3\xe9\xbb\x92", 0x2A2A34}, // makkuro
            {"\xe9\x87\x91\xe8\x89\xb2", 0xFFC93C}, // kin-iro
            {"\xe9\x8a\x80\xe8\x89\xb2", 0xD8E0EA}, // gin-iro
            {"\xe3\x82\xaa\xe3\x83\xac\xe3\x83\xb3\xe3\x82\xb8", 0xFF8A1E}, // orenji
        };
        if (text::length(text) > 8) return 0; // a long unspaced run is a whole phrase: tinting it would colour the line
        for (auto const& c : kCjk) if (text.find(c.n) != std::string_view::npos) return c.c;
        return 0;
    }
    if (w.size() < 3) return 0;
    if (uint32_t c = look(w)) return c;
    if (w.back() == 's') return look(std::string_view(w).substr(0, w.size() - 1)); // "flamingos", "blues"
    return 0;
}


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
    if (cfg.autoFont && !in.songFont.empty() && mFonts.has(in.songFont)) cfg.font = in.songFont; // [LyricsForge] per-song font
    if (cfg.combatAware && in.fight > 0.5f && mFonts.has(cfg.combatFont)) cfg.font = cfg.combatFont; // [LyricsForge] fighting: clean font
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
        // [LyricsForge] the whole lyric palette from the cover, not just the glow: a light tint of it for the letters
        // (readable), the cover colour itself and a neighbouring hue for the two-colour gradient
        cfg.customTheme = true;
        cfg.customMain  = int32_t(colBrighten(mAccent, 0.55f));
        cfg.customA     = int32_t(mAccent);
        cfg.customB     = int32_t(colBrighten(hueTurn(mAccent, -32.0f), 0.18f));
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
    if (cfg.colorWords) // [LyricsForge] "pink", "blue", "gold"...: the word turns that colour as it is sung
        for (auto& w : words) {
            if (w.plain || w.argb) continue;
            uint32_t const t = colorWord(w.text);
            if (!t) continue;
            w.tint  = t;
            w.tintK = w.sungMs < 0 ? 0.0f : std::clamp(float(w.sungMs) / 280.0f, 0.0f, 1.0f);
            w.glow += 0.35f * w.tintK;
        }
    if (m) direct(words, *m, cfg);
    bool const haveLyrics = in.lyrics && !in.lyrics->lines.empty(); // [LyricsForge] no lyrics: nothing on screen at all
    if (haveLyrics && cfg.titleCard && in.music && in.music->songAge < kTitleCardSec) titleCard(words, in, cfg, cam);
    if (haveLyrics && cfg.songOutro && in.music && in.music->outro > 0.01f) endCredits(words, in, cfg, cam);

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
        ai.density *= std::clamp(in.awaken, 0.0f, 1.0f);
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
        { // [LyricsForge] constellation outro: the song finished (stopped in its last seconds, or the next one started
          // right after its end) -> its final line rises into the sky ahead of you as stars, whatever the sky
            bool const finished = mLastDur > 0 && mLastT >= mLastDur - 3500 && mLastT >= mFinalLine.start;
            bool const stopped  = mWasPlaying && !in.playing;
            bool const nextSong = mPrevSeq >= 0 && in.songSeq != mPrevSeq;
            int64_t const which = nextSong ? mPrevSeq : in.songSeq;
            if (finished && (stopped || nextSong) && mFinalFor == which && mFinaled != which && cfg.finaleStars && cfg.showLyrics) {
                SkyWriter::In fi = si;
                fi.color = tint;
                mSkyWriter.finale(mFinalLine, fi);
                mFinaled = which;
            }
            if (in.lyrics && in.songSeq != mFinalFor) { // remember this song's final line (copied: it outlives the lyrics)
                Line const* last = nullptr;
                for (auto const& l : in.lyrics->lines) if (l.index >= 0 && !l.text.empty()) last = &l;
                if (last) { mFinalLine = *last; mFinalFor = in.songSeq; }
            }
            if (in.playing) { mLastT = in.t; mLastDur = in.durationMs; }
            if (nextSong && !in.playing) mLastT = -1;
            mWasPlaying = in.playing;
            mPrevSeq    = in.songSeq;
        }
        mSkyWriter.step(si);
        mSkyWriter.emit(si, mOrigin, mBack);
        if (cfg.chapterCover && in.skyCard >= 0.0f && in.coverAtlas >= 0) { // [LyricsForge] the new song's cover, painted in the sky
            float const p   = std::clamp(in.skyCard, 0.0f, 1.0f);
            auto  sm        = [](float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
            float const env = sm(0.0f, 0.22f, p) * (1.0f - sm(0.68f, 1.0f, p));
            float const el  = 24.0f + 4.0f * p;                              // it drifts up a little as it glows
            double const R  = 70.0;
            Vec3 const  d   = dir(double(in.skyCardYaw), double(-el));
            Vec3 const  c0  = d * R;                                         // relative to the camera eye (mOrigin)
            Vec3 const  n   = d * -1.0, up0{0, 1, 0};
            Vec3 const  r   = up0.cross(n).normalized(), u = n.cross(r).normalized();
            float const half = 8.5f * (0.94f + 0.08f * p);
            auto quad = [&](double h, uint32_t argb, float alpha, uint32_t flags, float round, float soft, uint32_t atlas) {
                BfGlyphV1 q{};
                q.pos[0] = float(c0.x); q.pos[1] = float(c0.y); q.pos[2] = float(c0.z);
                Vec3 const rr = r * h, uu = u * h;
                q.right[0] = float(rr.x); q.right[1] = float(rr.y); q.right[2] = float(rr.z);
                q.up[0] = float(uu.x); q.up[1] = float(uu.y); q.up[2] = float(uu.z);
                q.uv[0] = 0; q.uv[1] = 0; q.uv[2] = 1; q.uv[3] = 1;
                q.fill = q.fill2 = toRgba(argb | 0xFF000000u);
                q.alpha = alpha; q.outlineW = round; q.glowW = soft; q.flags = flags; q.atlas = atlas;
                mBack.push_back(q);
            };
            uint32_t const glowCol = in.albumAccent ? in.albumAccent : tint;
            quad(half * 1.45, glowCol, 0.32f * env, BF_GLYPH_SOLID, 1.0f, 12.0f, 0);        // the halo in the cover's colour
            quad(half, 0xFFFFFFFFu, 0.9f * env, BF_GLYPH_IMAGE, 0.12f, 0.0f, uint32_t(in.coverAtlas)); // the cover
        }
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
