// [LyricsForge] Video screen - the Windows side in Bedrock Forge: which windows can be shown, which one the settings
// mean right now, and the BF_GetScreenConfig / BF_PutScreenStatus exports Renderer Foundation talks to (BfLyrics.h).
// RF does the capturing and drawing; Bedrock Forge only decides what and where. Original code for Bedrock Forge.
// No LeviLamina headers in this file.
#ifdef _WIN32
#include "lyrics/win/Win.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#include "lyrics/BfLyrics.h"

#pragma comment(lib, "dwmapi.lib")

namespace bf::lyrics::win {
namespace {
std::mutex       gMutex;
BfScreenConfigV1 gCfg{};
BfScreenStatusV1 gStatus{};
int64_t          gCfgAtMs = 0, gStatusAtMs = 0;
HWND             gHwnd = nullptr; // the window cfg.screenWindow resolved to
std::string      gHwndKey;        // ... for this key
std::string      gLabel;
int64_t          gSearchAtMs = 0;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string utf8(wchar_t const* w, int n = -1) {
    int const len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), len, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string titleOf(HWND h) {
    wchar_t b[512];
    int const n = GetWindowTextW(h, b, 512);
    return n > 0 ? utf8(b, n) : std::string();
}

/// "chrome", "vlc", "msedge" ... (exe name without the folder and ".exe")
std::string appOf(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t buf[MAX_PATH];
    DWORD   n = MAX_PATH;
    std::string out;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
        std::wstring_view v(buf, n);
        if (auto s = v.find_last_of(L"\\/"); s != std::wstring_view::npos) v.remove_prefix(s + 1);
        if (v.size() > 4 && (v.substr(v.size() - 4) == L".exe" || v.substr(v.size() - 4) == L".EXE")) v.remove_suffix(4);
        out = utf8(v.data(), int(v.size()));
    }
    CloseHandle(p);
    return out;
}

bool candidate(HWND h) {
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return false;
    LONG_PTR const ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false; // never Minecraft itself (a mirror in a mirror)
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return false; // other desktops, suspended UWP
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"WorkerW")) return false;
    return GetWindowTextLengthW(h) > 0;
}

std::string keyOf(HWND h) { return appOf(h) + "|" + titleOf(h); }

std::string labelOf(HWND h) {
    std::string app = appOf(h), title = titleOf(h);
    if (!app.empty()) app[0] = char(std::toupper(static_cast<unsigned char>(app[0])));
    return (IsIconic(h) ? "(minimised) " : "") + (app.empty() ? title : app + ": " + title);
}

/// cfg.screenWindow -> a live window: the one we already had (titles change with every video), the same title, or
/// the same app. Searching is throttled to once a second while nothing matches.
HWND resolve(std::string const& key) {
    if (key.empty()) return nullptr;
    if (gHwndKey == key && gHwnd && IsWindow(gHwnd)) return gHwnd;
    int64_t const now = nowMs();
    if (gHwndKey == key && now - gSearchAtMs < 1000) return nullptr;
    gSearchAtMs = now;
    gHwndKey    = key;
    gHwnd       = nullptr;
    std::string const app = key.substr(0, key.find('|'));
    HWND sameApp = nullptr;
    for (auto const& s : screenSources()) {
        if (!s.hwnd) continue;
        if (s.key == key) { gHwnd = reinterpret_cast<HWND>(s.hwnd); break; }
        if (!sameApp && !app.empty() && s.key.substr(0, s.key.find('|')) == app) sameApp = reinterpret_cast<HWND>(s.hwnd);
    }
    if (!gHwnd) gHwnd = sameApp;
    return gHwnd;
}
} // namespace

std::vector<ScreenSource> screenSources() {
    std::vector<ScreenSource> out;
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        if (candidate(h)) {
            auto& v = *reinterpret_cast<std::vector<ScreenSource>*>(p);
            v.push_back({keyOf(h), labelOf(h), uint64_t(reinterpret_cast<uintptr_t>(h)), -1});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    struct Mon { std::vector<ScreenSource>* v; int i; } m{&out, 0};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM p) -> BOOL {
        auto& m = *reinterpret_cast<Mon*>(p);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(mon, &mi);
        char b[96];
        std::snprintf(b, sizeof b, "Whole screen %d (%ldx%ld)%s", m.i + 1, mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top, (mi.dwFlags & MONITORINFOF_PRIMARY) ? ", main" : "");
        m.v->push_back({"@monitor:" + std::to_string(m.i), b, 0, m.i});
        ++m.i;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&m));
    return out;
}

std::string screenUpdate(Config const& cfg, bool inWorld) {
    BfScreenConfigV1 c{};
    c.size       = sizeof c;
    c.version    = 1;
    c.upscale    = uint32_t(std::clamp(cfg.screenUpscale, 0, 2));
    c.cropClient = cfg.screenCrop ? 1u : 0u;
    c.maxFps     = 60;
    c.monitor    = -1;
    std::string label;
    std::lock_guard l(gMutex);
    if (cfg.screenOn && !cfg.screenWindow.empty()) {
        if (cfg.screenWindow.rfind("@monitor:", 0) == 0) {
            c.monitor = std::atoi(cfg.screenWindow.c_str() + 9);
            label     = "Whole screen " + std::to_string(c.monitor + 1);
        } else if (HWND h = resolve(cfg.screenWindow)) {
            c.hwnd = uint64_t(reinterpret_cast<uintptr_t>(h));
            if (nowMs() - gSearchAtMs > 1000 || gLabel.empty()) { gLabel = labelOf(h); gSearchAtMs = nowMs(); }
            label = gLabel;
        }
    }
    c.enabled = cfg.screenOn && inWorld && (c.hwnd || c.monitor >= 0) ? 1u : 0u;
    gCfg      = c;
    gCfgAtMs  = nowMs();
    return label;
}

std::string screenStatusLine() {
    std::lock_guard l(gMutex);
    if (!gStatusAtMs || nowMs() - gStatusAtMs > 3000) return "needs Renderer Foundation (a build with the video screen)";
    std::string s = gStatus.message;
    if (gStatus.state == BF_SCREEN_LIVE) {
        char b[64];
        std::snprintf(b, sizeof b, " %ux%u %.0f fps", gStatus.srcW, gStatus.srcH, double(gStatus.fpsX10) / 10.0);
        s += b;
        if (gStatus.upscaled & 3) s += (gStatus.upscaled & 3) == BF_SCREEN_MAX ? ", Anime4K Max" : ", Anime4K Sharp";
        if (gStatus.upscaled & 4) s += " x2";
    }
    return s;
}

bool screenLive() {
    std::lock_guard l(gMutex);
    return gStatusAtMs && nowMs() - gStatusAtMs < 3000 && gStatus.state == BF_SCREEN_LIVE;
}

} // namespace bf::lyrics::win

extern "C" BF_LYRICS_EXPORT void BF_GetScreenConfig(BfScreenConfigV1* out) {
    using namespace bf::lyrics::win;
    if (!out) return;
    uint32_t const room = out->size ? std::min<uint32_t>(out->size, sizeof(BfScreenConfigV1)) : uint32_t(sizeof(BfScreenConfigV1));
    BfScreenConfigV1 c;
    {
        std::lock_guard l(gMutex);
        c = gCfg;
        if (nowMs() - gCfgAtMs > 2000) c.enabled = 0; // no HUD frames: menu, loading, module off -> stop capturing
    }
    c.size = room;
    std::memcpy(out, &c, room);
}

extern "C" BF_LYRICS_EXPORT void BF_PutScreenStatus(BfScreenStatusV1 const* in) {
    using namespace bf::lyrics::win;
    if (!in) return;
    BfScreenStatusV1 s{};
    std::memcpy(&s, in, std::min<size_t>(in->size ? in->size : sizeof s, sizeof s));
    s.message[sizeof s.message - 1] = '\0';
    std::lock_guard l(gMutex);
    gStatus     = s;
    gStatusAtMs = nowMs();
}
#endif
