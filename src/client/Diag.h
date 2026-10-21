#pragma once
// Bedrock Forge client - hook call counters, written to mods/BedrockForgeCore/logs/diag.txt every ~10 s of HUD
// time (the game log is buffered and lost when the process is killed, so this is how a test run is read back).
#include <atomic>
#include <string>

namespace bedrock_forge::client::diag {
inline std::atomic<long> lightHook{0}, lightBrightened{0}, overlayHook{0}, bobHook{0}, timeHook{0}, fovHook{0},
    attackHook{0}, chatHook{0}, menuOpens{0};
void write(); // Modules.cpp
void noteScreen(std::string const& rootControlName); // non-HUD screens drawn under the menu
} // namespace bedrock_forge::client::diag
