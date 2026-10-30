// Renderer Foundation - Spatial GUI: which game screen is on top. Original code for Renderer Foundation.
// SceneStack::update runs every client tick; its top screen name decides whether the 3D panel should be up
// (inventory, chest, crafting table and other container screens) or the UI stays flat (HUD, pause, settings, chat).
#include "ScreenWatch.h"

#include <atomic>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/screens/SceneStack.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/UIScene.h"

#include "Options.h"

namespace rf::panel {
void noteScreen(void const* stack, std::string const& name); // SpatialPanel.cpp
bool keepHud();                                              // SpatialPanel.cpp
}

namespace {
SceneStack* gInvStack = nullptr; // the stack whose top is the inventory screen (client thread)
// the hud_screen scene, found while it is the top of its stack (client thread); compared by address only
std::atomic<void const*> gHudScene{nullptr};
}
LL_TYPE_INSTANCE_HOOK(RfSceneStackUpdateHook, ll::memory::HookPriority::Normal, SceneStack, &SceneStack::$update, bool) {
    bool        r    = origin();
    std::string name = this->getScreenName();
    if (name.find("inventory_screen") != std::string::npos) gInvStack = this;
    else if (gInvStack == this) gInvStack = nullptr;
    if (name.find("hud_screen") != std::string::npos) {
        AbstractScene const* top = nullptr;
        this->forEachScreenConst([&](AbstractScene const& sc) { if (!top) top = &sc; return false; }, true);
        if (top) gHudScene = static_cast<void const*>(top);
    }
    rf::panel::noteScreen(this, name);
    return r;
}

// Java Spatial GUI: the hotbar and hearts stay on the screen while a container is shown as a panel. Bedrock's hud_screen
// renders only while it is the top screen (render_only_when_topmost defaults to true); while a panel is up it is drawn
// under the container as well (SpatialPanel::record keeps its strip flat).
LL_TYPE_INSTANCE_HOOK(RfHudTopmostHook, ll::memory::HookPriority::Normal, UIScene, &UIScene::$renderOnlyWhenTopMost, bool) {
    bool r = origin();
    // nothing but an address compare here: this runs while the game renders its screen stack
    if (r && static_cast<void const*>(static_cast<AbstractScene const*>(this)) == gHudScene.load(std::memory_order_relaxed) && rf::panel::keepHud()) return false;
    return r;
}

namespace rf::screens {
bool gInstalled = false;
void install() {
    if (gInstalled || !RfOptions::uiCaptureSpike) return;
    RfSceneStackUpdateHook::hook();
    RfHudTopmostHook::hook();
    gInstalled = true;
}
void popInventory() {
    if (gInvStack) gInvStack->schedulePopScreen(1);
    gInvStack = nullptr;
}
void uninstall() {
    if (!gInstalled) return;
    RfSceneStackUpdateHook::unhook();
    RfHudTopmostHook::unhook();
    gInstalled = false;
}
} // namespace rf::screens
