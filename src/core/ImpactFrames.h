#pragma once
// Prototype 3 - Cinematic Impact Frames (v0.2)
//
// Behaviour reference: "Impact frames" Fabric mod by TheFoxThatHates (ARR licence:
// NO code, shaders, textures or sounds reused - concept only, see docs/LICENSE_AUDIT.md).
//
// When the LOCAL player hits an entity in a single-player / locally hosted world:
//   [0, lengthMs)            `frameCount` full-screen frames, alternating bg/fg colour (manga flash)
//                            with speed lines in the opposite colour. Frames are opaque, so they
//                            also cover the HUD ("cinematic" by default).
//   [lengthMs, +fadeMs)      vignette + speed lines fade out
// plus an impact sound. Trigger rules: min damage, chance with decay + recovery, cooldown.
//
// VISUAL ONLY: the game simulation is never paused. Not possible yet (needs RenderingAPI /
// shader hook): colour inversion of the world, solid black/white mob silhouettes, true view
// freeze, real camera shake.

#include <string>

#include "ll/api/mod/NativeMod.h"

#include "bf/Events.h"

namespace bedrock_forge::impact {

struct Rgb {
    float r = 0, g = 0, b = 0;
};

struct Settings {
    bool        enabled          = true;
    // --- trigger rules ---
    float       minDamage        = 1.0f;   // ignore tiny hits (proxy for "full hit")
    float       chance           = 1.0f;   // 0..1 chance per qualifying hit
    float       chanceDecay      = 0.0f;   // chance lost per trigger (0 = off)
    int         chanceRecoverMs  = 2000;   // no hits for this long -> chance resets
    int         cooldownMs       = 0;      // minimum gap between impact frames
    // --- the frames ---
    int         lengthMs         = 100;    // whole frame sequence
    int         frameCount       = 2;
    bool        alternate        = true;   // swap bg/fg on odd frames
    Rgb         background       = {0, 0, 0};
    Rgb         foreground       = {1, 1, 1};
    // Animated burst frames from a resource pack (ships as packs/BedrockForgeImpact).
    // Swap in any pack that provides <framePath>0..N-1 to change the look.
    bool        useFrameTextures = true;
    // Built-in styles: impact/ (star), impact_ring/, impact_ink/
    std::string framePath        = "textures/bedrock_forge/impact/frame_";
    int         frameTextures    = 5;
    bool        speedLines       = false;   // off by default: clean frames read better
    int         lines            = 28;
    int         fadeMs           = 60;    // after-glow (vignette + fading lines); 0 = hard cut
    float       intensity        = 1.0f;   // 0..1 opacity multiplier for everything
    // --- sound (vanilla id; custom needs a resource pack) ---
    std::string sound            = "random.anvil_land";
    float       volume           = 0.6f;
    float       pitch            = 1.6f;   // 0 = no sound
};

/// Wire listeners into `subs`. `settings` must outlive the subscriptions.
void install(bf::events::Subscriptions& subs, ll::mod::NativeMod& mod, Settings const& settings);

/// Remember who the local player is (called on ClientJoinLevelEvent; 0 on exit).
void setLocalPlayerId(long long rawId);

} // namespace bedrock_forge::impact
