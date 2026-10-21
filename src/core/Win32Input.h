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

namespace bedrock_forge::win32::files {

/// Rewrites a path the game is about to open. Return true and fill `out` to open that file instead.
using RedirectFn = bool (*)(wchar_t const* path, wchar_t* out, unsigned long outLen);

struct HookSite {
    char const* name;
    void*       target;   // the KernelBase export
    void*       detour;   // our replacement
    void**      original; // trampoline slot filled by the hook engine
};

/// CreateFileW / CreateFile2 hook sites (targets resolved from KernelBase.dll). Returns how many are valid.
int  hookSites(HookSite* out, int max);
void setRedirect(RedirectFn fn);

} // namespace bedrock_forge::win32::files
