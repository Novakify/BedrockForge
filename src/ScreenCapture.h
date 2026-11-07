// Renderer Foundation - LyricsForge video screen: window / monitor capture (Windows.Graphics.Capture, C++/WinRT).
// Original code for Renderer Foundation. No LeviLamina or game headers here (keeps the WinRT projection away from them).
// A worker thread owns the capture session; frames are read back to the CPU (cropped to the client area) and the
// newest one is handed to ScreenPass, which uploads it on the game's own device (D3D12 or D3D11).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rf::screen::cap {

struct Frame {
    uint32_t             w = 0, h = 0; // px
    uint64_t             seq = 0;      // bumps with every new frame
    std::vector<uint8_t> bgra;         // tightly packed rows (w * 4 bytes), B8G8R8A8
};

struct Want {
    bool     on      = false;
    uint64_t hwnd    = 0;  // window, or 0
    int      monitor = -1; // monitor index when hwnd == 0
    bool     crop    = true;
    uint32_t maxFps  = 60;
    bool operator==(Want const&) const = default;
};

enum class State : uint32_t { Idle = 0, Starting = 1, Live = 2, Error = 3 };
struct Status {
    State       state = State::Idle;
    uint32_t    w = 0, h = 0;
    float       fps = 0;
    std::string message = "off";
};

/// Called every Present (cheap): what should be captured. The worker thread starts / stops / switches sessions.
void   want(Want const& w);
/// Copy the newest frame into `out` if it is newer than `haveSeq` (swaps buffers, no big copy). Returns true if so.
bool   latest(uint64_t haveSeq, Frame& out);
Status status();
void   shutdown();

} // namespace rf::screen::cap
