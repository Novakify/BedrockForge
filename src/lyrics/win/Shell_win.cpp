// LyricsForge - [BF] small shell helpers (kept out of Runtime_win.cpp so <windows.h> stays out of it).
// Original code for Bedrock Forge.
#include "lyrics/win/Win.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <thread>
#pragma comment(lib, "shell32.lib")

namespace bf::lyrics::win {

void openInNotepad(std::filesystem::path const& p) {
    std::wstring const arg = L"\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

void pressHotkey(int const* vks, int count) {
    if (count <= 0 || count > 4) return;
    std::array<WORD, 4> k{};
    for (int i = 0; i < count; ++i) k[size_t(i)] = WORD(vks[i]);
    std::thread([k, count] { // held ~60 ms like a real press: the NVIDIA app's hotkey hook ignores 0 ms taps
        auto send = [](WORD vk, bool up) {
            INPUT in{};
            in.type       = INPUT_KEYBOARD;
            in.ki.wVk     = vk;
            in.ki.wScan   = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
            in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
            SendInput(1, &in, sizeof in);
        };
        for (int i = 0; i < count; ++i) { send(k[size_t(i)], false); Sleep(20); }
        Sleep(60);
        for (int i = count - 1; i >= 0; --i) { send(k[size_t(i)], true); Sleep(10); }
    }).detach();
}

} // namespace bf::lyrics::win
#endif
