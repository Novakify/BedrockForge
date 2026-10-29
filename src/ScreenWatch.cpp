// Renderer Foundation - Spatial GUI: which game screen is on top. Original code for Renderer Foundation.
// SceneStack::update runs every client tick; its top screen name decides whether the 3D panel should be up
// (inventory, chest, crafting table and other container screens) or the UI stays flat (HUD, pause, settings, chat).
#include "ScreenWatch.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/screens/SceneStack.h"

#include "Options.h"

namespace rf::panel {
void noteScreen(void const* stack, std::string const& name); // SpatialPanel.cpp
}

LL_TYPE_INSTANCE_HOOK(RfSceneStackUpdateHook, ll::memory::HookPriority::Normal, SceneStack, &SceneStack::$update, bool) {
    bool r = origin();
    rf::panel::noteScreen(this, this->getScreenName());
    return r;
}

namespace rf::screens {
bool gInstalled = false;
void install() {
    if (gInstalled || !RfOptions::uiCaptureSpike) return;
    RfSceneStackUpdateHook::hook();
    gInstalled = true;
}
void uninstall() {
    if (!gInstalled) return;
    RfSceneStackUpdateHook::unhook();
    gInstalled = false;
}
} // namespace rf::screens
