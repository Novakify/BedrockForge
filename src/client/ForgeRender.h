#pragma once
// Bedrock Forge "ForgeRender" - shaderpack switcher, shaders from resource packs, material reload, Vibrant Visuals
// toggle (ForgeRender.cpp). Original implementation; BetterRenderDragon was only read for ideas.
#include <filesystem>
#include <ostream>

namespace bedrock_forge::client::forgerender {
/// Installs the shaderpack file hooks + registers the "shaderpacks" (ForgeRender) module. Call from Core::load.
void install(std::filesystem::path const& modDir);
void uninstall();
/// Apply menu changes (client thread, every HUD frame).
void tick();
/// Append ForgeRender state to logs/shaderpacks.txt.
void writeReport(std::ostream& out);
} // namespace bedrock_forge::client::forgerender
