// LyricsForge - [BF] Clip mode. Original code for Bedrock Forge; see Clip.h.
#include "lyrics/core/Clip.h"

#include <cctype>

#include "lyrics/core/Conductor.h"

namespace bf::lyrics {

bool ClipTimer::step(MusicState const* m, bool enabled, float dt, int64_t songSeq) {
    mSince += dt;
    if (songSeq != mSeq) { mSeq = songSeq; mCount = 0; mPending = -1; mFirstChorusDone = false; }
    if (!enabled || !m || !m->active) { mPending = -1; return false; }
    if (m->liftNow && mPending < 0 && mCount < kMaxPerSong && mSince > kGap) {
        bool const firstChorus = m->chorusNow && !mFirstChorusDone;
        if ((m->dropNow && m->bigDrop) || firstChorus) mPending = kDelay;
    }
    if (m->chorusNow) mFirstChorusDone = true;
    if (mPending >= 0) {
        mPending -= dt;
        if (mPending < 0) { mCount++; mSince = 0; return true; }
    }
    return false;
}

Hotkey parseHotkey(std::string_view s) {
    Hotkey h;
    std::string tok;
    auto flush = [&] {
        if (tok.empty() || h.count >= 4) { tok.clear(); return true; }
        int vk = 0;
        if (tok == "alt") vk = 0x12;
        else if (tok == "ctrl" || tok == "control") vk = 0x11;
        else if (tok == "shift") vk = 0x10;
        else if (tok == "win") vk = 0x5B;
        else if (tok.size() >= 2 && tok[0] == 'f' && std::isdigit(uint8_t(tok[1]))) {
            int n = std::atoi(tok.c_str() + 1);
            if (n >= 1 && n <= 24) vk = 0x70 + n - 1;
        } else if (tok.size() == 1 && std::isalnum(uint8_t(tok[0]))) vk = std::toupper(uint8_t(tok[0]));
        tok.clear();
        if (!vk) return false;
        h.keys[h.count++] = vk;
        return true;
    };
    for (char c : s) {
        if (c == '+' || c == ' ') { if (!flush()) return {}; }
        else tok += char(std::tolower(uint8_t(c)));
    }
    if (!flush()) return {};
    return h;
}

} // namespace bf::lyrics
