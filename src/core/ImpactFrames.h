#pragma once
// Prototype 3 - Cinematic Impact Frames (v0)
//
// When the LOCAL player hits an entity in a single-player / locally hosted world:
//   0 ..  flashMs          white impact flash (manga "frame")
//   ..   +inkMs            dark "ink" frame with speed lines converging on the crosshair
//   ..   +fadeMs           lines and vignette fade out
// plus an impact sound. All of this is VISUAL ONLY: the game simulation is never paused
// ("hit-stop" here is a held frame of overlay, not a frozen world).
//
// Not done yet (needs camera hooks, see docs/KNOWN_LIMITATIONS.md): real camera shake.

#include <string>

#include "ll/api/mod/NativeMod.h"

#include "bf/Events.h"

namespace bedrock_forge::impact {

struct Settings {
    bool        enabled   = true;
    float       minDamage = 1.0f;   // ignore tiny hits
    int         flashMs   = 45;
    int         inkMs     = 70;
    int         fadeMs    = 160;
    int         lines     = 28;     // speed lines per impact
    float       intensity = 1.0f;   // 0..1 overall opacity multiplier
    std::string sound     = "random.anvil_land";
    float       volume    = 0.6f;
    float       pitch     = 1.6f;
};

/// Wire listeners into `subs`. `settings` must outlive the subscriptions.
void install(bf::events::Subscriptions& subs, ll::mod::NativeMod& mod, Settings const& settings);

/// Remember who the local player is (called on ClientJoinLevelEvent).
void setLocalPlayerId(long long rawId);

} // namespace bedrock_forge::impact
