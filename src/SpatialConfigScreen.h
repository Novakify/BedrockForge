// Renderer Foundation - Spatial GUI in-game config screen (Java: the mod's YACL config screen). Original code.
#pragma once

namespace rf::sgconfig {
/// Opens the screen (from the pause screen button or the F6 menu); edits apply live, Cancel restores.
void open();
bool isOpen();
/// Client thread: whether the pause screen is the top screen (shows the "Spatial GUI Settings" button).
void notePause(bool pauseOnTop);
/// ImGui should get the window's mouse/keyboard messages (screen open, or the pause button shown).
bool wantsInput();
/// The game's own mouse (GameInput) should see no buttons.
bool blocksGameMouse();
/// Inside the ImGui frame, every frame.
void draw();
} // namespace rf::sgconfig
