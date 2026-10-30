// Renderer Foundation - Spatial GUI: which game screen is on top (SceneStack hook). Original code for Renderer Foundation.
#pragma once

namespace rf::screens {
void install();
void uninstall();
/// Close the player's inventory screen if it is open (the hidden window of a scene at a block with no window of its
/// own, when the scene ends by itself). Client thread.
void popInventory();
} // namespace rf::screens
