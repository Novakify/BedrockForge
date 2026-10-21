// Renderer Foundation - graphics controls for 1.26 (see Notice.h for licence/credits).
//   * Vibrant Visuals on/off through the game's own advanced-graphics options. Replaces upstream's
//     "Vanilla2Deferred / Force Enable Deferred Technical Preview" patches: on 1.26 the deferred renderer ships as
//     Vibrant Visuals, so no patching is needed and a server that disables it stays in control.
//   * Reload shaders: upstream discarded a frame and freed RenderDragon's compiled shader blobs using offsets that
//     are 1.20/1.21-only. 1.26 port (experimental): MinecraftGame::cleanReloadMaterials on the client thread.
#pragma once
#include <string>

namespace rf::graphics {
void        install();
void        uninstall();
void        tick();   // client thread: consumes pending requests from the overlay
std::string status(); // last result, shown in the overlay
long        reloadCount();
} // namespace rf::graphics
