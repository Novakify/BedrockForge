#pragma once

#include "ll/api/mod/NativeMod.h"

#include "bf/Events.h"
#include "core/ClientModules.h"
#include "core/ImpactFrames.h"

namespace bedrock_forge {

/// Settings saved to mods/BedrockForgeCore/config/config.json
struct Config {
    int  version         = 1;
    bool hudEnabled      = true;   // HUD visible when the world loads
    bool logTickHeartbeat = true;  // log a line every 600 client ticks (~30s) to prove ticks arrive
    int  hudX            = 4;
    int  hudY            = 4;
    impact::Settings impact;       // Prototype 3
    modules::Settings modules;     // client HUD modules (FPS, CPS, keystrokes, coords...)
};

class Core {
public:
    static Core& getInstance();

    Core() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }
    [[nodiscard]] Config&             config() { return mConfig; }

    bool load();    // Prototype 1: startup message
    bool enable();  // Prototype 1: event callbacks + Prototype 2: HUD + keybind
    bool disable(); // Prototype 1: clean disable (all listeners removed)

private:
    ll::mod::NativeMod&     mSelf;
    Config                  mConfig;
    bf::events::Subscriptions mSubs;
};

} // namespace bedrock_forge
