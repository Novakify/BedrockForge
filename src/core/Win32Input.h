#pragma once
// Tiny Win32 shim so game headers never see <windows.h> (macro clashes).

namespace bedrock_forge::win32 {

/// True while the key / mouse button (Windows virtual-key code) is held.
bool keyDown(int vk);
/// True when the game window has keyboard focus (we don't count keys typed into other apps).
bool gameFocused();
/// Local wall-clock time as "HH:MM".
void localTime(int& hour, int& minute);

namespace vk {
inline constexpr int LButton = 0x01, RButton = 0x02, Space = 0x20, Shift = 0x10;
inline constexpr int W = 0x57, A = 0x41, S = 0x53, D = 0x44;
} // namespace vk

} // namespace bedrock_forge::win32
