// FiguraBridge - Bedrock Forge module entry points (src/figura/win/FiguraBridge_win.cpp).
#pragma once
#include <filesystem>

namespace bf::figura::win {
void install(std::filesystem::path const& modDir, std::filesystem::path const& configDir);
void uninstall();
} // namespace bf::figura::win
