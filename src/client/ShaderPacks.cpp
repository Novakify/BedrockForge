#include "client/ShaderPacks.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <string>
#include <string_view>
#include <mutex>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

#include "client/ForgeRender.h"
#include "client/Module.h"
#include "core/Win32Input.h"

namespace bedrock_forge::client::shaderpacks {

namespace {
namespace fs = std::filesystem;

// What the file hooks redirect to. Swapped atomically so the menu can change it while the renderer opens files.
struct State {
    std::wstring packDir;  // active shaderpack folder ("" = none)
    std::wstring rpDir;    // cache of material.bin files taken from the active resource packs ("" = off)
};
std::atomic<std::shared_ptr<State const>> gState{std::make_shared<State const>()};
fs::path                gPacksDir, gCfg;
std::string             gActiveName;
std::atomic<int>        gRedirects{0}, gRpRedirects{0};
std::atomic<int>        gSeen[4]{}, gRedirected[4]{};
// first few redirected names, logged later from a safe place (never log inside a file hook)
std::mutex             gNamesMutex;
std::vector<std::wstring> gNames;
std::vector<std::wstring> gSeenPaths; // every material path that reached the hook (first 200)
std::vector<win32::files::HookSite> gSites;

bool endsWithI(std::wstring_view s, std::wstring_view suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i)
        if (std::towlower(s[s.size() - suffix.size() + i]) != std::towlower(suffix[i])) return false;
    return true;
}
bool containsI(std::wstring_view s, std::wstring_view needle) {
    if (needle.empty()) return false;
    auto it = std::search(s.begin(), s.end(), needle.begin(), needle.end(),
                          [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
    return it != s.end();
}

bool tryCandidate(std::wstring const& cand, wchar_t* out, unsigned long outLen) {
    std::error_code ec;
    if (cand.size() + 1 >= outLen || !fs::is_regular_file(fs::path(cand), ec)) return false;
    std::copy(cand.begin(), cand.end(), out);
    out[cand.size()] = 0;
    return true;
}

std::atomic<bool> gYield{false}; // Renderer Foundation is loaded: it owns material redirection

bool redirect(int api, wchar_t const* path, wchar_t* out, unsigned long outLen) {
    if (gYield.load(std::memory_order_relaxed)) return false;
    std::wstring_view p(path);
    if (!endsWithI(p, L".material.bin")) return false;
    auto st = gState.load();
    if (st->packDir.empty() && st->rpDir.empty()) return false;
    if (api >= 0 && api < 4) ++gSeen[api];
    {
        std::lock_guard lock(gNamesMutex);
        if (gSeenPaths.size() < 200) gSeenPaths.push_back(std::to_wstring(api) + L" " + std::wstring(p));
    }
    if (containsI(p, st->packDir) || containsI(p, st->rpDir)) return false; // already ours
    if (!containsI(p, L"renderer\\materials\\") && !containsI(p, L"renderer/materials/")) return false;
    auto slash = p.find_last_of(L"\\/");
    std::wstring name(slash == std::wstring_view::npos ? p : p.substr(slash + 1));
    // priority: active resource packs (ForgeRender pack shaders) > shaderpack folder > vanilla
    bool hit = false, fromRp = false;
    if (!st->rpDir.empty() && tryCandidate(st->rpDir + L"\\" + name, out, outLen)) hit = fromRp = true;
    if (!hit && !st->packDir.empty())
        hit = tryCandidate(st->packDir + L"\\renderer\\materials\\" + name, out, outLen)
           || tryCandidate(st->packDir + L"\\" + name, out, outLen);
    if (!hit) return false;
    ++gRedirects;
    if (fromRp) ++gRpRedirects;
    if (api >= 0 && api < 4) ++gRedirected[api];
    std::lock_guard lock(gNamesMutex);
    if (gNames.size() < 60) gNames.push_back((fromRp ? L"rp:" : L"") + name);
    return true;
}

void publish(std::wstring packDir, std::wstring rpDir) {
    gState.store(std::make_shared<State const>(State{std::move(packDir), std::move(rpDir)}));
}
} // namespace

std::vector<std::string> listPacks() {
    std::vector<std::string> found;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(gPacksDir, ec))
        if (e.is_directory()) found.push_back(e.path().filename().string());
    std::sort(found.begin(), found.end());
    return found;
}

std::string activePack() { return gActiveName; }

// E-024: with Renderer Foundation loaded, BF stopped redirecting and RF's own loader served nothing (vanilla terrain on
// Fancy). BF's loader is the proven one, so it keeps serving; it only yields if config/yield_to_rf exists.
bool yieldToRendererFoundation() {
    static int decided = -1;
    if (decided < 0) {
        std::error_code ec;
        decided = GetModuleHandleW(L"RendererFoundation.dll") && fs::exists(gCfg.parent_path() / "yield_to_rf", ec) ? 1 : 0;
        gYield  = decided == 1;
    }
    return gYield;
}

std::filesystem::path configFile() { return gCfg; }

bool setActivePack(std::string const& name) {
    std::error_code ec;
    std::string use = name;
    if (use.empty() || use == "off" || !fs::is_directory(gPacksDir / use, ec)) use = "off";
    gActiveName = use;
    std::ofstream{gCfg} << use << "\n";
    if (!gCfg.empty()) std::ofstream{gCfg.parent_path() / "shaderpack.running"} << use << "\n";
    auto st = gState.load();
    publish(use == "off" ? std::wstring{} : (gPacksDir / use).wstring(), st->rpDir);
    return use != "off";
}

void setResourcePackCache(fs::path const& dir) {
    auto st = gState.load();
    publish(st->packDir, dir.empty() ? std::wstring{} : dir.wstring());
}

void install(fs::path const& modDir) {
    auto& log = ll::mod::NativeMod::current()->getLogger();
    gPacksDir = modDir / "shaderpacks";
    gCfg      = modDir / "config" / "shaderpack.txt";
    std::error_code ec;
    fs::create_directories(gPacksDir, ec);
    fs::create_directories(gCfg.parent_path(), ec);
    std::string active = "Anime";
    if (std::ifstream in{gCfg}; in) std::getline(in, active);
    while (!active.empty() && (active.back() == '\r' || active.back() == ' ')) active.pop_back();
    // Crash guard: shaderpack.running exists while the game runs with a pack and is removed on a clean shutdown.
    // Still there at startup = the last run crashed with that pack -> fall back (config/shaderpack.fallback, else off).
    auto running = gCfg.parent_path() / "shaderpack.running";
    if (fs::exists(running, ec)) {
        std::string crashed;
        if (std::ifstream in{running}; in) std::getline(in, crashed);
        if (!crashed.empty() && crashed == active && active != "off") {
            std::string fb = "off";
            if (std::ifstream in{gCfg.parent_path() / "shaderpack.fallback"}; in) std::getline(in, fb);
            while (!fb.empty() && (fb.back() == '\r' || fb.back() == ' ')) fb.pop_back();
            if (fb == active) fb = "off";
            log.warn("[BF] shaderpacks: the last run crashed while '{}' was active -> using '{}' (see logs/crash)", active, fb);
            std::ofstream{gCfg.parent_path() / "shaderpack.crashed"} << active << "\n";
            active = fb;
        }
    }
    bool on = setActivePack(active);
    std::ofstream{running} << gActiveName << "\n";
    log.info("[BF] shaderpacks: {} found, active = '{}'", listPacks().size(), gActiveName);
    if (!on && active != "off") log.warn("[BF] shaderpack '{}' not found in {}", active, gPacksDir.string());

    // Hooks go in even with no pack active: ForgeRender can switch packs and enable resource-pack shaders later.
    win32::files::setRedirect(&redirect);
    win32::files::HookSite sites[6];
    int n = win32::files::hookSites(sites, 6);
    for (int i = 0; i < n; ++i) {
        int rc = ll::memory::hook(sites[i].target, sites[i].detour, sites[i].original, ll::memory::HookPriority::Normal);
        log.info("[BF] shaderpacks: hooked {} (rc {})", sites[i].name, rc);
        if (rc == 0) gSites.push_back(sites[i]);
    }
}

void writeReport() {
    static char const* api[4] = {"CreateFileW", "CreateFile2", "NtCreateFile", "NtOpenFile"};
    std::lock_guard lock(gNamesMutex);
    std::error_code ec;
    auto dir = ll::mod::NativeMod::current()->getModDir() / "logs";
    fs::create_directories(dir, ec);
    std::ofstream out{dir / "shaderpacks.txt"};
    if (!out) return;
    for (int i = 0; i < 4; ++i) out << api[i] << ": saw " << gSeen[i].load() << " material opens, redirected " << gRedirected[i].load() << "\n";
    auto st = gState.load();
    out << "active shaderpack: " << gActiveName << "\nresource-pack shaders: " << (st->rpDir.empty() ? "off" : fs::path(st->rpDir).string())
        << " (" << gRpRedirects.load() << " opens served)\nloaded from pack:";
    for (auto& n : gNames) out << " " << fs::path(n).string();
    out << "\n";
    forgerender::writeReport(out);
    out << "all material opens (api path):\n";
    for (auto& n : gSeenPaths) out << "  " << fs::path(n).string() << "\n";
}

void reportOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& log = ll::mod::NativeMod::current()->getLogger();
    log.info("[BF] shaderpacks: {} materials redirected", gRedirects.load());
    writeReport(); // the game log is buffered and lost if the process is killed
}

void uninstall() {
    std::error_code ec;
    fs::remove(gCfg.parent_path() / "shaderpack.running", ec); // clean shutdown
    win32::files::setRedirect(nullptr);
    for (auto& s : gSites) ll::memory::unhook(s.target, s.detour);
    gSites.clear();
    if (int n = gRedirects.load()) ll::mod::NativeMod::current()->getLogger().info("[BF] shaderpacks: {} materials were loaded from the pack", n);
}

} // namespace bedrock_forge::client::shaderpacks
