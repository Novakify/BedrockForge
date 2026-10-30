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

namespace {
SceneStack* gInvStack = nullptr; // the stack whose top is the inventory screen (client thread)
}
LL_TYPE_INSTANCE_HOOK(RfSceneStackUpdateHook, ll::memory::HookPriority::Normal, SceneStack, &SceneStack::$update, bool) {
    bool        r    = origin();
    std::string name = this->getScreenName();
    if (name.find("inventory_screen") != std::string::npos) gInvStack = this;
    else if (gInvStack == this) gInvStack = nullptr;
    rf::panel::noteScreen(this, name);
    return r;
}

namespace rf::screens {
bool gInstalled = false;
void install() {
    if (gInstalled || !RfOptions::uiCaptureSpike) return;
    RfSceneStackUpdateHook::hook();
    gInstalled = true;
}
void popInventory() {
    if (gInvStack) gInvStack->schedulePopScreen(1);
    gInvStack = nullptr;
}
void uninstall() {
    if (!gInstalled) return;
    RfSceneStackUpdateHook::unhook();
    gInstalled = false;
}
} // namespace rf::screens
