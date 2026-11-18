#pragma once
// Bedrock Forge SDK - ConfigAPI (v0)
// Load-or-create a JSON config in the mod's config folder.
// T must be a plain aggregate struct with an `int version` member.

#include <filesystem>
#include <string_view>

#include "ll/api/Config.h"
#include "ll/api/mod/NativeMod.h"

namespace bf::config {

template <ll::config::IsConfig T>
bool loadOrCreate(ll::mod::NativeMod& mod, T& cfg, std::string_view fileName = "config.json") {
    auto path = mod.getConfigDir() / fileName;
    bool ok   = false;
    try {
        ok = ll::config::loadConfig(cfg, path);
    } catch (...) {
        ok = false;
    }
    if (!ok) {
        // Missing or broken file: write defaults so the player can edit them.
        ll::config::saveConfig(cfg, path);
        mod.getLogger().warn("Config {} was missing or invalid; wrote defaults.", path.string());
    }
    return ok;
}

template <ll::config::IsConfig T>
bool save(ll::mod::NativeMod& mod, T const& cfg, std::string_view fileName = "config.json") {
    return ll::config::saveConfig(cfg, mod.getConfigDir() / fileName);
}

} // namespace bf::config
