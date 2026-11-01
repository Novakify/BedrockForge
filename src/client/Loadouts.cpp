#include "client/Loadouts.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <nlohmann/json.hpp>

#include "ll/api/mod/NativeMod.h"

#include "client/Module.h"
#include "client/ShaderPacks.h"

namespace bedrock_forge::client::loadouts {

namespace {
namespace fs = std::filesystem;
using nlohmann::json;

fs::path          gDir, gModsRoot, gActiveFile;
std::string       gSelfFolder;
std::mutex        gMutex; // guards gStatus / gActive
std::string       gStatus = "pick a loadout";
std::string       gActive;
std::atomic<bool> gBusy{false}, gRestart{false};

constexpr char const* kOff = "manifest.json.bfoff";

void setStatus(std::string s) {
    std::lock_guard l(gMutex);
    gStatus = std::move(s);
}
auto& log() { return ll::mod::NativeMod::current()->getLogger(); }

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// LeviLamina and Bedrock Forge itself are never switched off or swapped.
bool isProtected(std::string const& folder) {
    auto f = lower(folder);
    return f == "levilamina" || f == lower(gSelfFolder);
}

std::optional<json> readJson(fs::path const& p) {
    std::ifstream in{p};
    if (!in) return std::nullopt;
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return j;
}

// ---- native mod folders ----
struct ModFolder {
    fs::path    dir;
    std::string name;
    bool        enabled;
    std::string entry; // DLL file name from the manifest
};
std::vector<ModFolder> scanMods() {
    std::vector<ModFolder> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(gModsRoot, ec)) {
        if (!e.is_directory(ec)) continue;
        ModFolder m{e.path(), e.path().filename().string(), false, {}};
        fs::path man = m.dir / "manifest.json";
        if (fs::exists(man, ec)) m.enabled = true;
        else if (fs::exists(m.dir / kOff, ec)) man = m.dir / kOff;
        else continue; // not a mod
        if (auto j = readJson(man); j && j->contains("entry") && (*j)["entry"].is_string()) m.entry = (*j)["entry"].get<std::string>();
        if (m.entry.empty()) m.entry = m.name + ".dll";
        out.push_back(std::move(m));
    }
    return out;
}

uint64_t hashFile(fs::path const& p) {
    std::ifstream in{p, std::ios::binary};
    if (!in) return 0;
    uint64_t h = 1469598103934665603ull;
    char     buf[1 << 16];
    while (in) {
        in.read(buf, sizeof buf);
        for (std::streamsize i = 0; i < in.gcount(); ++i) h = (h ^ uint8_t(buf[i])) * 1099511628211ull;
    }
    return h;
}
bool isTempOld(fs::path const& p) { return p.filename().string().find(".bfswap-") != std::string::npos; }
std::string stemOf(std::string const& entry) { return fs::path(entry).stem().string(); }

// Another .dll in the folder with exactly the entry DLL's bytes (a named build), or "".
std::string namedCopyOf(ModFolder const& m, uint64_t h) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(m.dir, ec)) {
        auto fn = e.path().filename().string();
        if (!e.is_regular_file(ec) || lower(fn) == lower(m.entry) || isTempOld(e.path()) || lower(e.path().extension().string()) != ".dll") continue;
        if (hashFile(e.path()) == h) return fn;
    }
    return {};
}

// Put <build> in place as the mod's entry DLL. Returns "" on success, else the error.
std::string swapDll(ModFolder const& m, std::string const& build) {
    std::error_code ec;
    fs::path src = m.dir / build, dst = m.dir / m.entry;
    if (!fs::is_regular_file(src, ec)) return std::format("{}: build '{}' not found", m.name, build);
    uint64_t hs = hashFile(src), hd = fs::exists(dst, ec) ? hashFile(dst) : 0;
    if (hs == hd) return {};
    if (hd) {
        // keep the current DLL if it's the only copy; otherwise it's just a temp file removed next launch
        fs::path aside;
        if (namedCopyOf(m, hd).empty()) {
            aside = m.dir / std::format("{}.saved-{:08x}.dll", stemOf(m.entry), uint32_t(hd));
            if (fs::exists(aside, ec)) aside.clear();
        }
        if (aside.empty()) aside = m.dir / std::format("{}.bfswap-{}.old", stemOf(m.entry), GetTickCount64());
        fs::rename(dst, aside, ec); // allowed even while the DLL is loaded
        if (ec) return std::format("{}: couldn't move the running DLL aside ({})", m.name, ec.message());
    }
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) return std::format("{}: copy failed ({})", m.name, ec.message());
    gRestart = true;
    log().info("[BF] loadouts: {} now runs {} (next launch)", m.name, build);
    return {};
}

std::string setModEnabled(ModFolder const& m, bool on) {
    if (m.enabled == on || isProtected(m.name)) return {};
    std::error_code ec;
    if (on) fs::rename(m.dir / kOff, m.dir / "manifest.json", ec);
    else fs::rename(m.dir / "manifest.json", m.dir / kOff, ec);
    if (ec) return std::format("{}: couldn't turn {} ({})", m.name, on ? "on" : "off", ec.message());
    gRestart = true;
    log().info("[BF] loadouts: mod {} {} (next launch)", m.name, on ? "on" : "off");
    return {};
}

// Module settings a loadout must not carry: the pack slider (position shifts when packs are added) and one-shot buttons.
bool skipSetting(std::string const& mod, std::string const& sid) { return mod == "shaderpacks" && (sid == "pack" || sid == "reload_now"); }

void applyModules(json const& mods) {
    auto& reg = Registry::get();
    for (auto& [id, v] : mods.items()) {
        Module* m = reg.find(id);
        if (!m || m->planned || !v.is_object()) continue;
        if (auto s = v.find("settings"); s != v.end() && s->is_object())
            for (auto& st : m->settings) {
                if (skipSetting(id, st.id)) continue;
                auto it = s->find(st.id);
                if (it == s->end()) continue;
                if (st.kind == Setting::Kind::Toggle && it->is_boolean()) st.on = it->get<bool>();
                if (st.kind == Setting::Kind::Slider && it->is_number()) st.value = std::clamp(it->get<float>(), st.min, st.max);
            }
        if (auto e = v.find("enabled"); e != v.end() && e->is_boolean()) reg.setEnabled(*m, e->get<bool>());
    }
    reg.save();
}

fs::path fileFor(std::string const& name) { return gDir / (name + ".json"); }
} // namespace

void install(fs::path const& modDir) {
    gDir        = modDir / "loadouts";
    gModsRoot   = modDir.parent_path();
    gSelfFolder = modDir.filename().string();
    gActiveFile = modDir / "config" / "loadout.txt";
    std::error_code ec;
    fs::create_directories(gDir, ec);
    if (std::ifstream in{gActiveFile}; in) std::getline(in, gActive);
    // DLLs swapped out last session are no longer loaded: remove the temp copies (".saved-" ones are kept)
    int cleaned = 0;
    for (auto& m : scanMods())
        for (auto& e : fs::directory_iterator(m.dir, ec))
            if (isTempOld(e.path()) && fs::remove(e.path(), ec)) ++cleaned;
    if (fs::is_empty(gDir, ec)) {
        // starter: plain vanilla look
        std::ofstream{fileFor("Vanilla")} << json{{"description", "No shaders, Fancy graphics"},
                                                  {"shaderpack", "off"},
                                                  {"modules", {{"shaderpacks", {{"settings", {{"vibrant", false}}}}}}}}
                                                 .dump(2);
    }
    log().info("[BF] loadouts: {} found, active '{}', {} old DLL copies cleaned", list().size(), gActive, cleaned);
}

std::vector<Info> list() {
    std::vector<Info> out;
    std::error_code   ec;
    for (auto& e : fs::directory_iterator(gDir, ec)) {
        if (lower(e.path().extension().string()) != ".json") continue;
        Info i{e.path().stem().string(), {}};
        if (auto j = readJson(e.path()); j && j->contains("description") && (*j)["description"].is_string())
            i.description = (*j)["description"].get<std::string>();
        out.push_back(std::move(i));
    }
    std::sort(out.begin(), out.end(), [](Info const& a, Info const& b) { return lower(a.name) < lower(b.name); });
    return out;
}

std::string active() {
    std::lock_guard l(gMutex);
    return gActive;
}
std::string status() {
    std::lock_guard l(gMutex);
    return gStatus;
}
bool restartNeeded() { return gRestart; }

void apply(std::string const& name) {
    if (gBusy.exchange(true)) return;
    auto j = readJson(fileFor(name));
    if (!j) {
        setStatus("can't read loadouts/" + name + ".json (broken JSON?)");
        gBusy = false;
        return;
    }
    // instant part (render thread): shaderpack + modules
    if (auto s = j->find("shaderpack"); s != j->end() && s->is_string()) {
        std::ofstream{shaderpacks::configFile()} << s->get<std::string>() << "\n"; // ForgeRender picks it up + reloads
    }
    if (auto m = j->find("modules"); m != j->end() && m->is_object()) applyModules(*m);
    {
        std::lock_guard l(gMutex);
        gActive = name;
        gStatus = name + ": applying...";
    }
    std::ofstream{gActiveFile} << name << "\n";
    log().info("[BF] loadouts: applying '{}'", name);

    // file part (worker thread): mods + DLL builds, hashing can take a moment
    std::thread([name, cfg = std::move(*j)] {
        std::vector<std::string> errors;
        try {
            auto mods = scanMods();
            auto find = [&](std::string const& n) -> ModFolder* {
                for (auto& m : mods)
                    if (lower(m.name) == lower(n)) return &m;
                return nullptr;
            };
            if (auto ms = cfg.find("mods"); ms != cfg.end() && ms->is_object())
                for (auto& [n, on] : ms->items()) {
                    auto* m = find(n);
                    if (!on.is_boolean()) continue;
                    if (!m) { errors.push_back("mod '" + n + "' not found"); continue; }
                    if (auto e = setModEnabled(*m, on.get<bool>()); !e.empty()) errors.push_back(e);
                }
            if (auto ds = cfg.find("dlls"); ds != cfg.end() && ds->is_object())
                for (auto& [n, build] : ds->items()) {
                    auto* m = find(n);
                    if (!build.is_string() || (m && isProtected(m->name))) continue;
                    if (!m) { errors.push_back("mod '" + n + "' not found"); continue; }
                    if (auto e = swapDll(*m, build.get<std::string>()); !e.empty()) errors.push_back(e);
                }
        } catch (std::exception const& ex) { errors.push_back(ex.what()); }
        std::string st = name + (errors.empty() ? " ready" : " - " + errors.front());
        if (gRestart) st += " | RESTART to finish";
        for (auto& e : errors) log().warn("[BF] loadouts: {}", e);
        setStatus(st);
        gBusy = false;
    }).detach();
}

std::string saveCurrent(std::string name) {
    std::error_code ec;
    // file names: letters, digits, space, - _
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return !(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_'); }), name.end());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty())
        for (int n = 1;; ++n)
            if (name = std::format("Loadout {}", n); !fs::exists(fileFor(name), ec)) break;
    if (!name.empty()) name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));

    json j;
    j["description"] = "Saved from the menu";
    j["shaderpack"]  = shaderpacks::activePack();
    json mods = json::object();
    for (auto& m : Registry::get().all()) {
        if (m->planned) continue;
        json s = json::object();
        for (auto& st : m->settings)
            if (!skipSetting(m->id, st.id)) s[st.id] = st.kind == Setting::Kind::Toggle ? json(st.on) : json(st.value);
        mods[m->id] = {{"enabled", m->enabled}, {"settings", s}};
    }
    j["modules"] = mods;
    json on = json::object(), dlls = json::object();
    for (auto& m : scanMods()) {
        if (isProtected(m.name)) continue;
        on[m.name] = m.enabled;
        fs::path dll = m.dir / m.entry;
        uint64_t h   = fs::exists(dll, ec) ? hashFile(dll) : 0;
        if (!h) continue;
        // pin the exact build: point at a named copy, or make one so the loadout can always restore it
        std::string build = namedCopyOf(m, h);
        if (build.empty()) {
            build = std::format("{}.saved-{:08x}.dll", stemOf(m.entry), uint32_t(h));
            fs::copy_file(dll, m.dir / build, fs::copy_options::skip_existing, ec);
        }
        dlls[m.name] = build;
    }
    j["mods"] = on;
    j["dlls"] = dlls;
    std::ofstream{fileFor(name)} << j.dump(2);
    {
        std::lock_guard l(gMutex);
        gActive = name;
        gStatus = "saved as " + name;
    }
    std::ofstream{gActiveFile} << name << "\n";
    log().info("[BF] loadouts: saved current setup as '{}'", name);
    return name;
}

} // namespace bedrock_forge::client::loadouts
