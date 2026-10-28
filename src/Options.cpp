// Renderer Foundation - options storage (modified from BetterRenderDragon's Options.cpp; see Notice.h).
#include "Options.h"

#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace RfOptions {
RfOption<bool> showImGui          = true;
RfOption<int>  uiKey              = VK_F6;
RfOption<int>  reloadShadersKey   = 0;
RfOption<bool> performanceEnabled = true;

RfOption<bool>        materialBinLoaderEnabled = true;
RfOption<bool>        redirectShaders          = true;
RfOption<std::string> shaderPack               = std::string{};
RfOption<bool>        autoReloadOnPackChange   = false;
bool                reloadShadersAvailable   = false;
std::atomic_bool    reloadShaders            = false;

RfOption<bool>     vibrantVisualsEnabled = false;
bool             vibrantVisualsAllowed = false;
std::atomic_bool applyGraphicsMode     = false;

RfOption<bool> uiCaptureSpike = false;
RfOption<int>  uiCaptureKey   = VK_F7;
RfOption<int>  uiPanelKey     = VK_F8;

std::filesystem::path optionsFile;
} // namespace RfOptions

using nlohmann::json;

namespace {
std::vector<IRfOption*> gOptions;

template <class T>
void read(json const& j, char const* key, RfOption<T>& opt) {
    if (auto it = j.find(key); it != j.end()) try {
            opt.set(it->template get<T>());
        } catch (...) {}
}
} // namespace

bool RfOptions::init(std::filesystem::path const& configDir) {
    gOptions = {&showImGui,       &uiKey,      &reloadShadersKey,       &performanceEnabled,   &materialBinLoaderEnabled,
                &redirectShaders, &shaderPack, &autoReloadOnPackChange, &vibrantVisualsEnabled, &uiCaptureSpike,
                &uiCaptureKey, &uiPanelKey};
    std::error_code ec;
    std::filesystem::create_directories(configDir, ec);
    optionsFile = configDir / "RendererFoundation.json";
    return true;
}

bool RfOptions::load() {
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
    read(j, "uiCaptureSpike", uiCaptureSpike);
    read(j, "uiCaptureKey", uiCaptureKey);
    read(j, "uiPanelKey", uiPanelKey);
    record();
    return true;
}

bool RfOptions::save() {
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
    j["uiCaptureSpike"]           = uiCaptureSpike.get();
    j["uiCaptureKey"]             = uiCaptureKey.get();
    j["uiPanelKey"]               = uiPanelKey.get();
    std::ofstream out(optionsFile, std::ios::trunc);
    if (!out) return false;
    out << j.dump(4);
    return true;
}

void RfOptions::record() {
    for (auto* o : gOptions) o->record();
}

bool RfOptions::isDirty() {
    for (auto* o : gOptions)
        if (o->isChanged()) return true;
    return false;
}
