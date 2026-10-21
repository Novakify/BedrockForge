// Renderer Foundation - MaterialBinLoader for 1.26 (see Notice.h for licence/credits).
//
// Upstream BetterRenderDragon hooked the game's readFile (found by byte signature) and served
// renderer/materials/*.material.bin from ResourcePackManager::load. That signature does not exist in 1.26, so this
// port hooks the Win32/NT file-open layer instead (version independent) and, when the game opens
// <game>/data/renderer/materials/<X>.material.bin, serves, in order:
//   1. <X> from the active resource packs (ResourcePackManager::load, like upstream) - redirectShaders
//   2. <X> from the selected shaderpack folder <mod>/shaderpacks/<pack>/[renderer/materials/]<X>
//   3. vanilla
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace rf::materials {

struct Stats {
    long seen = 0, fromPacks = 0, fromShaderpack = 0;
};

void                     install(std::filesystem::path const& modDir);
void                     uninstall();
std::vector<std::string> listShaderpacks();
void                     setShaderpack(std::string const& name); // "" = none
Stats                    stats();
std::vector<std::string> lastServed(); // most recent redirects, for the overlay

} // namespace rf::materials
