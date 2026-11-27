// Renderer Foundation - lab switches (see Lab.h). Original code for Renderer Foundation.
#define WIN32_LEAN_AND_MEAN
#include "Lab.h"
#include "LLShim.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace rf::lab {
namespace {
namespace fs = std::filesystem;
std::atomic_bool gVvTest{false}, gPix{false};

constexpr char const* kTemplate =
    "# Renderer Foundation lab switches - read once when the game starts (restart the game after changing).\n"
    "# vvtest 1      : every custom sky off this session (Living Skies / LyricsForge, Cloud Sea, Novak Glass, Haze, Mist)\n"
    "#                 so Vibrant Visuals resource packs can be judged on the game's own sky and light.\n"
    "# vvtest_once 1 : vvtest goes back to 0 right after it is read (next launch is normal)\n"
    "# pix 1         : load PIX's GPU capturer at start, then attach PIX (Attach -> Minecraft.Windows.exe -> GPU capture)\n"
    "# pix_once 1    : same one-launch behaviour for pix\n"
    "# pix_dll       : optional full path to WinPixGpuCapturer.dll (default: newest C:\\Program Files\\Microsoft PIX\\<ver>\\)\n";

// "2611.19" -> {2611, 19}: newest PIX version folder wins
std::vector<long> versionKey(std::wstring const& s) {
    std::vector<long> k;
    long cur = -1;
    for (wchar_t c : s) {
        if (std::iswdigit(c)) cur = (cur < 0 ? 0 : cur * 10) + (c - L'0');
        else if (cur >= 0) { k.push_back(cur); cur = -1; }
    }
    if (cur >= 0) k.push_back(cur);
    return k;
}

fs::path findPixCapturer(std::string const& overridePath) {
    std::error_code ec;
    if (!overridePath.empty()) {
        fs::path p(overridePath);
        return fs::exists(p, ec) ? p : fs::path{};
    }
    wchar_t pf[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH) && !GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH)) return {};
    fs::path const root = fs::path(pf) / L"Microsoft PIX";
    fs::path best;
    std::vector<long> bestKey;
    for (auto const& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        fs::path const dll = e.path() / L"WinPixGpuCapturer.dll";
        if (!fs::exists(dll, ec)) continue;
        auto const key = versionKey(e.path().filename().wstring());
        if (best.empty() || key > bestKey) { best = dll; bestKey = key; }
    }
    return best;
}

void loadPix(std::string const& overridePath) {
    if (GetModuleHandleW(L"WinPixGpuCapturer.dll")) {
        gPix = true;
        ll_shim::info("[RF] lab: PIX GPU capturer was already loaded");
        return;
    }
    fs::path const dll = findPixCapturer(overridePath);
    if (dll.empty()) {
        ll_shim::warn("[RF] lab: pix 1 but WinPixGpuCapturer.dll was not found (install PIX, or set pix_dll in config/Lab.txt)");
        return;
    }
    HMODULE const h = LoadLibraryW(dll.c_str());
    gPix = h != nullptr;
    if (h) ll_shim::info("[RF] lab: PIX GPU capturer loaded from " + dll.string() + " - attach PIX to this game for GPU captures");
    else ll_shim::warn("[RF] lab: LoadLibrary failed for " + dll.string() + " (error " + std::to_string(GetLastError()) + ")");
}
} // namespace

void init(fs::path const& configDir) {
    fs::path const file = configDir / "Lab.txt";
    std::error_code ec;
    if (!fs::exists(file, ec)) {   // first run: leave a commented template so the switches are easy to find
        std::ofstream(file) << kTemplate << "vvtest 0\nvvtest_once 1\npix 0\npix_once 0\n";
        return;
    }
    // keep the file's lines (comments, order) and only rewrite the values that a *_once flag turns back off
    std::vector<std::string> lines;
    std::map<std::string, std::string> kv;
    {
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            lines.push_back(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string k, v;
            ls >> k;
            std::getline(ls, v);
            v.erase(0, v.find_first_not_of(" \t"));
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
            kv[k] = v;
        }
    }
    auto on = [&](char const* k) { auto it = kv.find(k); return it != kv.end() && it->second == "1"; };
    bool const vv = on("vvtest"), pix = on("pix");
    gVvTest = vv;
    if (vv) ll_shim::info("[RF] lab: VV test session - every custom sky is off (Living Skies, Cloud Sea, Novak Glass, Haze, Mist); WorldFx.txt is not saved this session");
    if (pix) loadPix(kv.count("pix_dll") ? kv["pix_dll"] : std::string{});

    bool const resetVv = vv && on("vvtest_once"), resetPix = pix && on("pix_once");
    if (resetVv || resetPix) {
        std::ofstream out(file);
        for (auto const& line : lines) {
            std::istringstream ls(line);
            std::string k;
            ls >> k;
            if (resetVv && k == "vvtest") out << "vvtest 0\n";
            else if (resetPix && k == "pix") out << "pix 0\n";
            else out << line << "\n";
        }
        ll_shim::info(std::string("[RF] lab: one-launch switch used - next launch is normal (") + (resetVv ? "vvtest" : "") +
                      (resetVv && resetPix ? ", " : "") + (resetPix ? "pix" : "") + " set back to 0)");
    }
}

bool vvTest() { return gVvTest.load(std::memory_order_relaxed); }
bool pixLoaded() { return gPix.load(std::memory_order_relaxed); }
} // namespace rf::lab
