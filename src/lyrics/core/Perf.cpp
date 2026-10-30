// LyricsFlow port - performance (see Perf.h). AutoPerf.tick follows LyricsFlow 2.1's perf.AutoPerf bytecode: 500 ms
// checks, "low" below 92 % of the target, only steps up when the mod itself is costly (> 10 % of the frame or
// > 2.5 ms), +2 levels below 60 % of the target, a level that had to be raised again within 6 s is remembered as bad
// for 2 minutes, relaxes after 3 s of cheap frames or 4 s (great FPS) / 20 s (fine FPS). Original C++ for Bedrock Forge.
#include "lyrics/core/Perf.h"

#include <algorithm>

namespace bf::lyrics {

int AutoPerf::target(Config const& cfg) const {
    int t = cfg.targetFps > 0 ? cfg.targetFps : 60;
    if (cfg.vsync) t = std::min(t, 57);
    if (cfg.maxFps > 0 && cfg.maxFps < 260) t = std::min(t, cfg.maxFps - 3);
    return std::max(10, t);
}

int AutoPerf::quality(Config const& cfg) const {
    int q = std::clamp(cfg.quality, 0, 2);
    if (mLevel >= 2) return 0;
    if (mLevel >= 1) return std::min(q, 1);
    return q;
}

void AutoPerf::reset() { *this = AutoPerf{}; }

void AutoPerf::tick(int64_t now, int fps, double modMs, Config const& cfg) {
    mModMs = mModMs * 0.9 + modMs * 0.1; // smoothed per-frame cost
    if (now - mLastCheck < 500) return;
    mLastCheck = now;
    mLastFps   = fps;
    if (!cfg.autoPerf) {
        mOkTicks = mLowTicks = 0;
        return;
    }
    int const t = target(cfg);
    if (double(fps) < double(t) * 0.92) { ++mLowTicks; mOkTicks = 0; }
    else { ++mOkTicks; mLowTicks = 0; }
    double const frameMs = 1000.0 / double(std::max(1, fps));
    bool const   heavy   = mModMs > frameMs * 0.1 || mModMs > 2.5;
    if (!heavy) { // the mod is cheap: never blame it, slowly give effects back
        mLowTicks = 0;
        if (mLevel > 0 && ++mRelaxTicks >= 6 && (mLevel - 1 > mBadLevel || now >= mBadUntil)) {
            --mLevel;
            mRaisedAt   = now;
            mRelaxTicks = 0;
        }
        return;
    }
    mRelaxTicks = 0;
    if (mLowTicks >= 2 && mLevel < kMax) {
        if (now - mRaisedAt < 6000) { mBadLevel = mLevel; mBadUntil = now + 120000; }
        mLevel    = std::min(kMax, mLevel + (double(fps) < double(t) * 0.6 ? 2 : 1));
        mLowTicks = mOkTicks = 0;
        return;
    }
    bool const great = double(fps) >= double(t) * 1.25, fine = double(fps) >= double(t) * 0.98;
    if (mLevel > 0 && ((great && mOkTicks >= 8) || (fine && mOkTicks >= 40))) {
        int next = mLevel - 1;
        if (next <= mBadLevel && now < mBadUntil) return;
        mLevel    = next;
        mRaisedAt = now;
        mOkTicks  = 0;
    }
}

Config effectiveConfig(Config const& cfg, AutoPerf const& perf) {
    Config e = cfg;
    int const q = perf.quality(cfg);
    // Pretty = as set. Balanced: softer glow, fewer falling letters. Performance: no glow, no neon offset passes.
    if (q <= 1) {
        e.glowStrength = std::min(e.glowStrength, 0.4f);
        e.maxLetters   = std::min(e.maxLetters, 150);
    }
    if (q == 0) {
        e.glow       = false;
        e.twoColor   = false;
        e.textShadow = std::min(e.textShadow, 1);
        e.maxLetters = std::min(e.maxLetters, 60);
    }
    // auto levels beyond quality (allow(n) = level < n)
    if (!perf.allow(3)) e.glow = false;
    if (!perf.allow(4)) e.fallingLetters = false;
    if (!perf.allow(5)) { e.twoColor = false; e.maxLines = std::min(e.maxLines, 1); }
    if (!perf.allow(6)) e.textShadow = 0;
    return e;
}

void applyProfile(Config& cfg, int profile) {
    cfg.profile = profile == 1 ? 1 : 0;
    if (cfg.profile == 1) { // PvP: effects off, lyrics kept out of your aim
        cfg.combatSafe     = true;
        cfg.fallingLetters = false;
        cfg.glow           = false;
        cfg.maxLines       = 1;
        cfg.quality        = 0;
        cfg.skyPreset      = "off";
    } else { // Chill: everything on
        cfg.combatSafe = false;
        cfg.glow       = true;
        cfg.quality    = 2;
        if (cfg.maxLines < 2) cfg.maxLines = 2;
    }
}

} // namespace bf::lyrics
