// Bedrock Forge "ForgeRender" - shader management for RenderDragon (original implementation).
//
//   * shaderpack switcher: pick any folder under mods/BedrockForgeCore/shaderpacks from the ClickGUI
//   * resource-pack shaders: renderer/materials/*.material.bin inside the ACTIVE resource packs (e.g. Java->Bedrock
//     converted packs that ship their own shaders) are extracted to cache/packshaders and win over the shaderpack
//   * reload: asks the game to reload its materials (MinecraftGame::cleanReloadMaterials) - experimental, the
//     shaderpacks.txt report shows whether materials were re-opened afterwards
//   * Vibrant Visuals toggle: flips the graphics mode through the game's own advanced-graphics options, only where
//     the game says Vibrant Visuals is allowed (a server that disables it keeps it disabled)
//
// The file-level redirect itself lives in ShaderPacks.cpp.

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ClientThreadExecutor.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/file/Path.h"
#include "mc/options/GraphicsMode.h"
#include "mc/options/IAdvancedGraphicsOptions.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/ResourcePackManager.h"

#include "client/Diag.h"
#include "client/Module.h"
#include "client/ShaderPacks.h"

namespace bedrock_forge::client::forgerender {

namespace {
namespace fs = std::filesystem;

fs::path                 gCacheDir;
std::vector<std::string> gPacks;          // shaderpack folders at startup
std::atomic<long>        gReloads{0}, gReloadHook{0}, gPackChanges{0};
std::mutex               gIndexMutex;
std::vector<std::string> gIndex;          // "<material> <- <pack>" for the report
std::atomic<bool>        gRpDirty{false}; // resource-pack shaders changed since the last reload
std::mutex               gStatusMutex;
std::string              gStatusText = "ready";
void        setStatus(std::string s) { std::lock_guard l(gStatusMutex); gStatusText = std::move(s); }
std::string status() { std::lock_guard l(gStatusMutex); return gStatusText; }

// State last applied, so the HUD tick only acts on changes.
int  gAppliedPack  = -2;
int  gAppliedRp    = -1;
int  gAppliedVivid = -1;

Module* mod() { return Registry::get().find("shaderpacks"); }

std::vector<std::wstring> vanillaMaterialNames() {
    std::vector<std::wstring> names;
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return names;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(exe).parent_path() / "data" / "renderer" / "materials", ec)) {
        auto n = e.path().filename().wstring();
        if (n.size() > 13 && n.ends_with(L".material.bin")) names.push_back(n);
    }
    return names;
}

// Extract material.bin files from the active resource packs into the cache. Later packs in the stack override
// earlier ones (the order is written to the report so it can be checked in game). Returns true if anything changed.
bool refreshPackShaders(ResourcePackManager& mgr) {
    static std::vector<std::wstring> names = vanillaMaterialNames();
    std::map<std::wstring, std::pair<std::string, std::string>> found; // material -> (bytes, pack)
    std::vector<std::string> order;
    mgr.iteratePacks([&](PackInstance const& pi) {
        ResourcePack const& rp = *pi.mPack;
        if (rp.mImpl->mIsBaseGamePack) return;
        std::string packName = rp.mImpl->mNameWithVersionForTelemetry.get();
        order.push_back(packName);
        for (auto const& n : names) {
            std::string bytes;
            if (pi.getResource(Core::Path(fs::path(L"renderer/materials/" + n)), bytes) && bytes.size() > 16)
                found[n] = {std::move(bytes), packName};
        }
    });

    std::error_code ec;
    fs::create_directories(gCacheDir, ec);
    bool changed = false;
    // drop stale files
    for (auto& e : fs::directory_iterator(gCacheDir, ec)) {
        if (!found.contains(e.path().filename().wstring())) { fs::remove(e.path(), ec); changed = true; }
    }
    std::vector<std::string> index;
    for (auto& [n, v] : found) {
        auto path = gCacheDir / n;
        std::string old;
        if (std::ifstream in{path, std::ios::binary}; in) old.assign(std::istreambuf_iterator<char>(in), {});
        if (old != v.first) {
            std::ofstream{path, std::ios::binary | std::ios::trunc}.write(v.first.data(), std::streamsize(v.first.size()));
            changed = true;
        }
        index.push_back(std::format("{} <- {}", fs::path(n).string(), v.second));
    }
    index.insert(index.begin(), std::format("stack ({} non-vanilla packs, bottom to top):", order.size()));
    for (size_t i = 0; i < order.size(); ++i) index.insert(index.begin() + 1 + std::ptrdiff_t(i), "  " + order[i]);
    {
        std::lock_guard lock(gIndexMutex);
        gIndex = std::move(index);
    }
    diag::packShaders = long(found.size());
    return changed;
}

void requestReload(char const* why) {
    ll::thread::ClientThreadExecutor::getDefault().execute([why] {
        auto ci = ll::service::getClientInstance();
        if (!ci) return;
        auto& game = static_cast<MinecraftGame&>(ci->getMinecraftGame());
        game.cleanReloadMaterials();
        ++gReloads;
        gRpDirty = false;
        setStatus(std::format("reloaded ({})", why));
        shaderpacks::writeReport();
    });
}

void applyVibrant(bool on) {
    ll::thread::ClientThreadExecutor::getDefault().execute([on] {
        auto ci = ll::service::getClientInstance();
        if (!ci) return;
        auto tg = ci->getTextureGroup();
        if (!tg) return;
        IAdvancedGraphicsOptions* o = tg->mAdvancedGraphicsOptions.get().get();
        if (!o) return;
        if (on && !o->isVibrantVisualsAllowed()) { setStatus("Vibrant Visuals not allowed here"); return; }
        if (!o->canSwitchGraphicsModeInGame()) { setStatus("graphics mode can't switch in game right now"); return; }
        o->setGraphicsMode(on ? GraphicsMode::Advanced : GraphicsMode::Fancy);
        setStatus(on ? "Vibrant Visuals on" : "Vibrant Visuals off");
    });
}
} // namespace

// Active resource packs changed (startup, settings, joining a world with packs): refresh the cache.
LL_TYPE_INSTANCE_HOOK(BfPacksChangedHook, ll::memory::HookPriority::Normal, MinecraftGame,
                      &MinecraftGame::$onActiveResourcePacksChanged, void, ResourcePackManager& mgr) {
    origin(mgr);
    ++gPackChanges;
    auto* m = mod();
    if (!m || !m->flag("pack_shaders", true)) return;
    try {
        if (refreshPackShaders(mgr)) {
            gRpDirty = true;
            setStatus("resource-pack shaders changed");
            if (m->enabled && m->flag("auto_reload", false)) requestReload("resource packs changed");
        }
    } catch (...) {
        setStatus("resource-pack shader scan failed");
    }
}

LL_TYPE_INSTANCE_HOOK(BfCleanReloadHook, ll::memory::HookPriority::Normal, MinecraftGame, &MinecraftGame::cleanReloadMaterials, void) {
    ++gReloadHook;
    origin();
}

void install(fs::path const& modDir) {
    gCacheDir = modDir / "cache" / "packshaders";
    shaderpacks::install(modDir);
    gPacks = shaderpacks::listPacks();

    auto& m = Registry::get().add("shaderpacks", "ForgeRender", Category::Visual,
                                  "Shaderpacks + shaders from resource packs (mods/BedrockForgeCore/shaderpacks)", true);
    float cur = 0;
    auto  act = shaderpacks::activePack();
    for (size_t i = 0; i < gPacks.size(); ++i)
        if (gPacks[i] == act) cur = float(i + 1);
    m.slider("pack", "Shaderpack", cur, 0, float(std::max<size_t>(gPacks.size(), 1)), 1)
        .toggleSetting("pack_shaders", "Use shaders inside active resource packs", true)
        .toggleSetting("auto_reload", "Reload materials when packs change (experimental)", false)
        .toggleSetting("reload_now", "Reload materials now (experimental)", false)
        .toggleSetting("vibrant", "Vibrant Visuals", false);
    BfPacksChangedHook::hook();
    BfCleanReloadHook::hook();
}

void uninstall() {
    BfPacksChangedHook::unhook();
    BfCleanReloadHook::unhook();
    shaderpacks::uninstall();
}

void tick() {
    auto* m = mod();
    if (!m) return;
    // shaderpack
    int pack = m->enabled ? int(m->num("pack", 0) + 0.5f) : 0;
    if (pack != gAppliedPack) {
        bool first   = gAppliedPack == -2;
        gAppliedPack = pack;
        std::string name = pack >= 1 && pack <= int(gPacks.size()) ? gPacks[size_t(pack - 1)] : "off";
        if (auto* s = m->setting("pack")) s->label = "Shaderpack: " + (name == "off" ? std::string("vanilla") : name);
        if (!first || shaderpacks::activePack() != name) {
            shaderpacks::setActivePack(name);
            setStatus("shaderpack -> " + name + (m->flag("auto_reload") ? "" : " (Reload or restart to apply)"));
            if (!first && m->flag("auto_reload")) requestReload("shaderpack switched");
        }
    }
    // resource-pack shaders
    int rp = m->enabled && m->flag("pack_shaders", true) ? 1 : 0;
    if (rp != gAppliedRp) {
        gAppliedRp = rp;
        shaderpacks::setResourcePackCache(rp ? gCacheDir : fs::path{});
    }
    // reload button
    if (auto* s = m->setting("reload_now"); s && s->on) {
        s->on = false;
        Registry::get().save();
        requestReload("menu");
    }
    // Vibrant Visuals (apply on change only; the first tick just records the saved value)
    int vivid = m->flag("vibrant") ? 1 : 0;
    if (gAppliedVivid == -1) gAppliedVivid = vivid;
    else if (vivid != gAppliedVivid) {
        gAppliedVivid = vivid;
        applyVibrant(vivid == 1);
    }
    m->description = "Shaderpacks + resource-pack shaders. Status: " + status() + (gRpDirty ? " - pack shaders changed, Reload/restart" : "");
}

void writeReport(std::ostream& out) {
    out << "forgerender: pack changes " << gPackChanges.load() << ", reloads asked " << gReloads.load() << ", cleanReloadMaterials ran "
        << gReloadHook.load() << ", status: " << status() << "\n";
    std::lock_guard lock(gIndexMutex);
    for (auto& l : gIndex) out << "  " << l << "\n";
}

} // namespace bedrock_forge::client::forgerender
