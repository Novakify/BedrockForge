// LyricsForge - [BF] Clip mode: after a song's big moment, ask the replay recorder (OBS Replay Buffer, or NVIDIA
// Instant Replay) to save what it holds, so the
// chorus / drop you just watched is on disk without you pressing anything. Original code for Bedrock Forge.
//
// Pure timing logic here (testable); the runtime presses the hotkey (cfg.clipHotkey, NVIDIA's default Alt+F10).
// Instant Replay itself must be on in the NVIDIA app. A clip is saved kDelay s after the chorus look starts on a
// big drop or on the song's first chorus, at most kMaxPerSong per song and never closer than kGap s apart.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace bf::lyrics {

struct MusicState;

class ClipTimer {
public:
    /// one frame; true on the frame the clip should be saved
    bool step(MusicState const* m, bool enabled, float dt, int64_t songSeq);
    int  savedThisSong() const { return mCount; }

    static constexpr float kDelay = 8.0f;    // s after the hit: the clip holds the build-up, the hit and the chorus look
    static constexpr float kGap   = 120.0f;  // s between clips
    static constexpr int   kMaxPerSong = 2;

private:
    int64_t mSeq = -1;
    float   mPending = -1, mSince = 1e9f;
    int     mCount = 0;
    bool    mFirstChorusDone = false;
};

/// "alt+f10" -> virtual-key list (modifiers first, the key last); empty when it can't be read.
struct Hotkey { int keys[4] = {0, 0, 0, 0}; int count = 0; };
Hotkey parseHotkey(std::string_view s);

} // namespace bf::lyrics
