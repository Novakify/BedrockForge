#pragma once
// Bedrock Forge shaderpacks - Iris-style "shaderpacks folder" for RenderDragon.
//
//   mods/BedrockForgeCore/shaderpacks/<Pack>/renderer/materials/<Name>.material.bin   (or <Pack>/<Name>.material.bin)
//   mods/BedrockForgeCore/config/shaderpack.txt   -> the active pack's folder name ("off" = vanilla)
//
// When the game opens data/renderer/materials/<Name>.material.bin and the active pack has a file with that name,
// the open is redirected to the pack's copy (CreateFileW / CreateFile2 hooks). Materials load at startup, so a
// pack change applies on the next launch. Packs are built with tools/ShaderForge (lazurite + dxc).

#include <filesystem>

namespace bedrock_forge::client::shaderpacks {

/// Install the file hooks (call from Core::load so it's in place before the renderer loads materials).
void install(std::filesystem::path const& modDir);
void uninstall();
/// Log what the hooks saw so far (once; call from a normal thread after startup, e.g. on joining a world).
void reportOnce();
/// Rewrite mods/BedrockForgeCore/logs/shaderpacks.txt (counts + every material path seen).
void writeReport();

} // namespace bedrock_forge::client::shaderpacks
