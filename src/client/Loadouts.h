#pragma once
// Bedrock Forge Loadouts - one click sets up EVERYTHING for a play style: shaderpack, Vibrant Visuals, every BF module
// (on/off + settings), which other native mods are on, and which DLL build each of those mods runs.
//
//   mods/BedrockForgeCore/loadouts/<Name>.json
//   {
//     "description": "Anime shaders + cinematic stuff",
//     "shaderpack": "Anime",                       // folder under shaderpacks, or "off"
//     "modules": { "zoom": { "enabled": true, "settings": { "fov": 30 } }, ... },   // same format as modules.json
//     "mods":    { "RendererFoundation": true, "SomeOtherMod": false },             // native mod folders on/off
//     "dlls":    { "RendererFoundation": "RendererFoundation.sgm1-4d7df40.dll" }    // build file inside that mod's folder
//   }
// Every key is optional: whatever a loadout leaves out stays as it is.
//
// Applied right away: shaderpack (ForgeRender reloads materials; a restart may still be needed for some packs),
// Vibrant Visuals and module states. Applied on the NEXT launch: mods on/off and DLL builds. DLL swap = rename the
// running DLL aside (Windows allows renaming a loaded DLL) and copy the build in under the real name. A DLL that has no
// other copy in its folder is kept as <name>.saved-<hash>.dll, so nothing is ever lost. LeviLamina and Bedrock Forge
// itself are never touched.

#include <filesystem>
#include <string>
#include <vector>

namespace bedrock_forge::client::loadouts {

struct Info {
    std::string name, description;
};

/// Call from Core::enable after the module registry is loaded. Cleans up DLLs swapped out last run.
void install(std::filesystem::path const& modDir);
std::vector<Info> list();
std::string       active();            // last applied loadout ("" = none)
/// Apply a loadout. File work (DLL swaps) runs on a worker thread; watch status().
void apply(std::string const& name);
/// Save the current setup (shaderpack, modules, mods, DLL builds) as loadouts/<name>.json. Empty name = "Loadout N".
std::string saveCurrent(std::string name);
std::string status();
bool        restartNeeded();           // mods / DLLs changed: they load at startup

} // namespace bedrock_forge::client::loadouts
