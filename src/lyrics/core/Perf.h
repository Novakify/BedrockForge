// LyricsFlow port - performance: Auto FPS (LyricsFlow 2.1 perf.AutoPerf, 374m, MIT: same thresholds and hysteresis),
// graphics quality (Performance / Balanced / Pretty), Chill / PvP profiles, and the effective config the renderer
// uses (effects dropped as quality / auto level demand). Original C++ for Bedrock Forge.
#pragma once
#include <cstdint>

#include "lyrics/core/Config.h"

namespace bf::lyrics {

class AutoPerf {
public:
    static constexpr int kMax = 6;
    /// Call every frame with the measured FPS and the mod's own cost this frame (ms); checks every 500 ms like Java.
    void tick(int64_t nowMs, int fps, double modMs, Config const& cfg);
    int  level() const { return mLevel; }
    bool allow(int n) const { return mLevel < n; }      // effect n stays on while level < n
    int  quality(Config const& cfg) const;               // 0 Performance, 1 Balanced, 2 Pretty after auto limits
    int  target(Config const& cfg) const;                // FPS target: targetFps, or 60 (capped by VSync / max FPS)
    int  fps() const { return mLastFps; }
    double modMs() const { return mModMs; }
    void reset();

private:
    int     mLevel = 0, mLastFps = 0, mLowTicks = 0, mOkTicks = 0, mRelaxTicks = 0, mBadLevel = -1;
    int64_t mLastCheck = 0, mBadUntil = 0, mRaisedAt = 0;
    double  mModMs = 0;
};

/// The config the renderer actually uses this frame: quality + auto level drop the costly effects, PvP profile on top.
Config effectiveConfig(Config const& cfg, AutoPerf const& perf);

/// Profiles (LyricsFlow 2.1 perf.Profiles): Chill = everything on, PvP = sky and effects off, combat safe, most FPS.
void applyProfile(Config& cfg, int profile); // 0 Chill, 1 PvP

} // namespace bf::lyrics
