#pragma once
// Bedrock Forge client - HUD editor support (Purxiy-style: drag elements, snap guides, scroll to resize).
// Every movable HUD element reports where it drew last frame; the editor (menu, HUD petal / pencil) shows those boxes
// over the frozen world, moves them by writing the module's x / y settings and resizes them through its size setting.
#include <algorithm>
#include <string>
#include <vector>

namespace bedrock_forge::client::hudedit {

struct Elem {
    std::string mod, label;              // module id, what the editor calls it
    std::string xKey, yKey, sizeKey;     // the module's settings it writes ("" = none)
    float       x = 0, y = 0, w = 0, h = 0;  // last drawn box, fractions of the screen (top-left)
    bool        pixels = false;          // x / y settings are GUI pixels from the top-left (the info panel)
};
/// Called by an element's HUD draw (GUI units, the screen W x H).
void note(char const* mod, char const* label, char const* xKey, char const* yKey, char const* sizeKey, float x, float y, float w, float h,
          float W, float H, bool pixels = false);
/// Every element drawn in the last few seconds (snapshot).
std::vector<Elem> elems();

/// Where an element of w x h goes for x / y settings fx, fy (0 = left / top edge, 1 = right / bottom edge, 0.5 middle),
/// with a 4 px margin. The editor writes fx = left / (W - w - 8) back.
inline void place(float fx, float fy, float w, float h, float W, float H, float& x, float& y) {
    float const m = 4.f;
    x = m + std::clamp(fx, 0.f, 1.f) * std::max(W - w - 2 * m, 0.f);
    y = m + std::clamp(fy, 0.f, 1.f) * std::max(H - h - 2 * m, 0.f);
}

} // namespace bedrock_forge::client::hudedit
