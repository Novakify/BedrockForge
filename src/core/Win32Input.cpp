#include "core/Win32Input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bedrock_forge::win32 {

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
bool keyDownSync(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

bool gameFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool cursorHidden() {
    CURSORINFO ci{};
    ci.cbSize = sizeof ci;
    return GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING);
}

void localTime(int& hour, int& minute) {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    hour   = t.wHour;
    minute = t.wMinute;
}

} // namespace bedrock_forge::win32
