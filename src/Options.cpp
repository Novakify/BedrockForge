// Renderer Foundation - options storage (modified from BetterRenderDragon's Options.cpp; see Notice.h).
#include "Options.h"

#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Options {
Option<bool> showImGui          = true;
Option<int>  uiKey              = VK_F6;
Option<int>  reloadShadersKey   = 0;
Option<bool> performanceEnabled = true;

Option<bool>        materialBinLoaderEnabled = true;
Option<bool>        redirectShaders          = true;
Option<std::string> shaderPack               = std::string{};
Option<bool>        autoReloadOnPackChange   = false;
bool                reloadShadersAvailable   = false;
std::atomic_bool    reloadShaders            = false;

Option<bool>     vibrantVisualsEnabled = false;
bool             vibrantVisualsAllowed = false;
std::atomic_bool applyGraphicsMode     = false;

std::filesystem::path optionsFile;
} // namespace Options

using nlohmann::json;

namespace {
std::vector<IOption*> gOptions;

template <class T>
void read(json const& j, char const* key, Option<T>& opt) {
    if (auto it = j.find(key); it != j.end()) try {
            opt.set(it->template get<T>());
        } catch (...) {}
}
} // namespace

bool Options::init(std::filesystem::path const& configDir) {
    gOptions = {&showImGui,       &uiKey,      &reloadShadersKey,       &performanceEnabled,   &materialBinLoaderEnabled,
                &redirectShaders, &shaderPack, &autoReloadOnPackChange, &vibrantVisualsEnabled};
    std::error_code ec;
    std::filesystem::create_directories(configDir, ec);
    optionsFile = configDir / "RendererFoundation.json";
    return true;
}

bool Options::load() {
    std::ifstream in(optionsFile);
    if (!in) return save();
    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    read(j, "showImGui", showImGui);
    read(j, "uiKey", uiKey);
    read(j, "reloadShadersKey", reloadShadersKey);
    read(j, "performanceEnabled", performanceEnabled);
    read(j, "materialBinLoaderEnabled", materialBinLoaderEnabled);
    read(j, "redirectShaders", redirectShaders);
    read(j, "shaderPack", shaderPack);
    read(j, "autoReloadOnPackChange", autoReloadOnPackChange);
    read(j, "vibrantVisualsEnabled", vibrantVisualsEnabled);
    record();
    return true;
}

bool Options::save() {
    json j;
    j["showImGui"]                = showImGui.get();
    j["uiKey"]                    = uiKey.get();
    j["reloadShadersKey"]         = reloadShadersKey.get();
    j["performanceEnabled"]       = performanceEnabled.get();
    j["materialBinLoaderEnabled"] = materialBinLoaderEnabled.get();
    j["redirectShaders"]          = redirectShaders.get();
    j["shaderPack"]               = shaderPack.get();
    j["autoReloadOnPackChange"]   = autoReloadOnPackChange.get();
    j["vibrantVisualsEnabled"]    = vibrantVisualsEnabled.get();
    std::ofstream out(optionsFile, std::ios::trunc);
    if (!out) return false;
    out << j.dump(4);
    return true;
}

void Options::record() {
    for (auto* o : gOptions) o->record();
}

bool Options::isDirty() {
    for (auto* o : gOptions)
        if (o->isChanged()) return true;
    return false;
}
