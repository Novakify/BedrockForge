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
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/controls/UIControl.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "Options.h"

namespace rf::panel {
void noteScreen(void const* stack, std::string const& name); // SpatialPanel.cpp
bool keepHud();                                              // SpatialPanel.cpp
// the drawn part of a container screen, in uv of the window (x0 >= x1: unknown)
void noteScreenBounds(std::string const& name, float x0, float y0, float x1, float y1); // SpatialPanel.cpp
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
    // Spatial GUI: where the container screen draws, so everything else of the UI (the HUD, mods' HUD text) stays flat
    if (rf::panel::keepHud() && name.find("hud_screen") == std::string::npos && name.find("_screen") != std::string::npos &&
        name.find("ui://") == std::string::npos) {
        AbstractScene const* top = nullptr;
        this->forEachScreenConst([&](AbstractScene const& sc) { if (!top) top = &sc; return false; }, true);
        float b[4] = {1e9f, 1e9f, -1e9f, -1e9f};
        bool  ok   = false;
        if (top) {
            auto const* ui = static_cast<UIScene const*>(static_cast<BaseScreen const*>(top));
            ScreenView const* view = ui->mScreenView.get();
            if (view && view->mVisualTree.get()) {
                glm::vec2 vs = view->mSize;
                std::shared_ptr<UIControl> root = view->mVisualTree->mRootControl;
                if (vs.x > 1 && vs.y > 1 && root) {
                    std::vector<UIControl const*> todo{root.get()};
                    int n = 0;
                    while (!todo.empty() && n < 4000) {
                        UIControl const* c = todo.back();
                        todo.pop_back();
                        ++n;
                        if (!c || !c->mIsVisibleInTree) continue;
                        glm::vec2 sz = c->mSize, pos = c->mCachedPosition;
                        // something drawn, smaller than the window (full-window backgrounds / dims are not the screen)
                        if (!c->mRenderableComponentLookup.empty() && sz.x > 0.5f && sz.y > 0.5f && sz.x < vs.x * 0.9f &&
                            sz.y < vs.y * 0.9f && pos.x > -vs.x && pos.y > -vs.y && pos.x < 2 * vs.x && pos.y < 2 * vs.y) {
                            b[0] = std::min(b[0], pos.x / vs.x);
                            b[1] = std::min(b[1], pos.y / vs.y);
                            b[2] = std::max(b[2], (pos.x + sz.x) / vs.x);
                            b[3] = std::max(b[3], (pos.y + sz.y) / vs.y);
                            ok   = true;
                        }
                        for (auto const& ch : c->mChildren) todo.push_back(ch.get());
                    }
                }
            }
        }
        if (ok) rf::panel::noteScreenBounds(name, b[0], b[1], b[2], b[3]);
        else rf::panel::noteScreenBounds(name, 1, 1, 0, 0);
    }
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
