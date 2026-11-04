// LyricsForge - [BF] small shell helpers (kept out of Runtime_win.cpp so <windows.h> stays out of it).
// Original code for Bedrock Forge.
#include "lyrics/win/Win.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace bf::lyrics::win {

void openInNotepad(std::filesystem::path const& p) {
    std::wstring const arg = L"\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

} // namespace bf::lyrics::win
#endif
