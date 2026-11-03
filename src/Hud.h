// Bedrock Forge client HUD + client menu. Original code for Renderer Foundation.
// Base-layer feature: it lives in RF itself (not in LyricsForge), so every Bedrock Forge setup gets the same client
// menu and HUD modules - FPS, CPS, keystrokes, coordinates, direction, armor/held-item durability, player list and a
// custom crosshair. Game state is read on the client thread (camera hook, throttled) and drawn from the ImGui frame.
#pragma once
#include <filesystem>

namespace rf::hud {
/// mod load: reads <config>/BedrockForgeClient.cfg
void loadConfig(std::filesystem::path const& configDir);
void saveConfig();
/// mod enable / disable: the client-thread snapshot hook
void install();
void uninstall();
/// GUI.cpp initializeImGui(): adds the HUD font (Segoe UI) as a secondary font; ImGui's default font stays first
void loadFonts();
/// Inside the ImGui frame, every frame: the HUD modules, then the client menu (when open)
void drawOverlay();
void drawMenu();
/// Overlay.cpp window procedure: the client-menu key (Right Shift by default)
bool isMenuKey(unsigned long long wParam, long long lParam);
void toggleMenu();
bool menuOpen();
/// ImGui should get the window's mouse/keyboard messages
bool wantsInput();
/// The game's own mouse (GameInput) should see no buttons (the cursor is over the client menu)
bool blocksGameMouse();
} // namespace rf::hud
