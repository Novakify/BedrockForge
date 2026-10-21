#pragma once
// Bedrock Forge - client HUD modules (v0)
//
// The standard "PvP client" quality-of-life widgets (the feature class Blueberry / Flarial / Latite ship),
// written from scratch on LeviLamina. Display-only: nothing here changes gameplay, sends packets or
// reads other players' data, so it's fine on servers that allow cosmetic clients.
//
//   FPS            frames per second
//   CPS            left / right clicks per second (rolling 1 s window)
//   Keystrokes     W A S D + Space + LMB/RMB, lit while held
//   Coordinates    X Y Z of the local player
//   Direction      facing (N / NE / E ...) + yaw
//   Clock          local time
//
// Toggle all with the "bedrock_forge.toggle_modules" key (default H), per-module in config.json.

#include "ll/api/mod/NativeMod.h"

#include "bf/Events.h"

namespace bedrock_forge::modules {

struct Settings {
    bool  enabled    = true;
    bool  fps        = true;
    bool  cps        = true;
    bool  keystrokes = true;
    bool  coords     = true;
    bool  direction  = true;
    bool  clock      = false;
    int   x          = 4;     // top-left of the info panel (UI units)
    int   y          = 34;
    int   keysX      = 4;     // keystrokes widget; negative = measured from the right/bottom edge
    int   keysY      = -70;
    float opacity    = 0.45f; // panel background
};

void install(bf::events::Subscriptions& subs, ll::mod::NativeMod& mod, Settings& settings);

} // namespace bedrock_forge::modules
