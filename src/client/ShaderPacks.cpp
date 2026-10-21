#include "client/ShaderPacks.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <string>
#include <string_view>
#include <mutex>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

#include "client/Module.h"
#include "core/Win32Input.h"

namespace bedrock_forge::client::shaderpacks {

namespace {
namespace fs = std::filesystem;
std::wstring           gActiveDir; // lower-case not required; compared case-insensitively
std::atomic<int>       gRedirects{0};
std::atomic<int>       gSeen[4]{}, gRedirected[4]{};
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
    auto it = std::search(s.begin(), s.end(), needle.begin(), needle.end(),
                          [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
    return it != s.end();
}

bool redirect(int api, wchar_t const* path, wchar_t* out, unsigned long outLen) {
    if (gActiveDir.empty()) return false;
    std::wstring_view p(path);
    if (!endsWithI(p, L".material.bin")) return false;
    if (api >= 0 && api < 4) ++gSeen[api];
    {
        std::lock_guard lock(gNamesMutex);
        if (gSeenPaths.size() < 200) gSeenPaths.push_back(std::to_wstring(api) + L" " + std::wstring(p));
    }
    if (containsI(p, gActiveDir)) return false; // already ours
    if (!containsI(p, L"renderer\\materials\\") && !containsI(p, L"renderer/materials/")) return false;
    auto slash = p.find_last_of(L"\\/");
    std::wstring name(slash == std::wstring_view::npos ? p : p.substr(slash + 1));
    for (std::wstring cand : {gActiveDir + L"\\renderer\\materials\\" + name, gActiveDir + L"\\" + name}) {
        std::error_code ec;
        if (cand.size() + 1 < outLen && fs::is_regular_file(fs::path(cand), ec)) {
            std::copy(cand.begin(), cand.end(), out);
            out[cand.size()] = 0;
            ++gRedirects;
            if (api >= 0 && api < 4) ++gRedirected[api];
            {
                std::lock_guard lock(gNamesMutex);
                if (gNames.size() < 40) gNames.push_back(name);
            }
            return true;
        }
    }
    return false;
}
} // namespace

void install(fs::path const& modDir) {
    auto& log = ll::mod::NativeMod::current()->getLogger();
    fs::path packs = modDir / "shaderpacks", cfg = modDir / "config" / "shaderpack.txt";
    std::error_code ec;
    fs::create_directories(packs, ec);
    fs::create_directories(cfg.parent_path(), ec);
    std::string active = "Anime";
    if (std::ifstream in{cfg}; in) std::getline(in, active);
    else std::ofstream{cfg} << active << "\n";
    while (!active.empty() && (active.back() == '\r' || active.back() == ' ')) active.pop_back();

    std::vector<std::string> found;
    for (auto& e : fs::directory_iterator(packs, ec))
        if (e.is_directory()) found.push_back(e.path().filename().string());
    log.info("[BF] shaderpacks: {} found, active = '{}'", found.size(), active);

    auto& m = Registry::get().add("shaderpacks", "Shaderpacks", Category::Visual,
                                  "Load custom RenderDragon shaders from mods/BedrockForgeCore/shaderpacks (restart to apply)", true);
    m.onToggle = [cfg](bool on) {
        if (!on) std::ofstream{cfg} << "off\n";
    };
    if (active == "off" || active.empty()) { m.enabled = false; return; }
    if (!fs::is_directory(packs / active, ec)) {
        log.warn("[BF] shaderpack '{}' not found in {}", active, packs.string());
        return;
    }
    gActiveDir = (packs / active).wstring();
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
    out << "loaded from pack:";
    for (auto& n : gNames) out << " " << fs::path(n).string();
    out << "\nall material opens (api path):\n";
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
    win32::files::setRedirect(nullptr);
    for (auto& s : gSites) ll::memory::unhook(s.target, s.detour);
    gSites.clear();
    if (int n = gRedirects.load()) ll::mod::NativeMod::current()->getLogger().info("[BF] shaderpacks: {} materials were loaded from the pack", n);
}

} // namespace bedrock_forge::client::shaderpacks
