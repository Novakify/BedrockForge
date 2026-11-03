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
// the stack that shows hud_screen, and whether that stack's top is still the HUD (Bedrock Forge client HUD)
void const*       gHudStack = nullptr;
std::atomic<int>  gHudTop{0}; // 0 another screen, 1 the HUD, 2 chat over the HUD
}
LL_TYPE_INSTANCE_HOOK(RfSceneStackUpdateHook, ll::memory::HookPriority::Normal, SceneStack, &SceneStack::$update, bool) {
    bool        r    = origin();
    std::string name = this->getScreenName();
    if (name.find("hud_screen") != std::string::npos) { gHudStack = this; gHudTop = 1; }
    else if (gHudStack == static_cast<void const*>(this)) gHudTop = name.find("chat_screen") != std::string::npos ? 2 : 0;
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
                    struct R { float x0, y0, x1, y1; };
                    std::vector<R>                rects;
                    std::vector<UIControl const*> todo{root.get()};
                    int n = 0;
                    while (!todo.empty() && n < 4000) {
                        UIControl const* c = todo.back();
                        todo.pop_back();
                        ++n;
                        if (!c || !c->mIsVisibleInTree) continue;
                        glm::vec2 sz = c->mSize, pos = c->mCachedPosition;
                        // something drawn, smaller than the window (full-window backgrounds / dims are not the screen)
                        if (!c->mRenderableComponentLookup->empty() && sz.x > 0.5f && sz.y > 0.5f && sz.x < vs.x * 0.9f &&
                            sz.y < vs.y * 0.9f && pos.x > -vs.x && pos.y > -vs.y && pos.x < 2 * vs.x && pos.y < 2 * vs.y)
                            rects.push_back({pos.x, pos.y, pos.x + sz.x, pos.y + sz.y});
                        for (auto const& ch : *c->mChildren) todo.push_back(ch.get());
                    }
                    // the screen is its biggest drawn piece (the container's panel) and what touches it (tabs, the
                    // recipe book, buttons on its edge, tooltips over it) - loose bits elsewhere (a hotbar row the screen
                    // keeps at the bottom of the window) stay off the panel
                    int   big = -1;
                    float area = 0;
                    for (int i = 0; i < int(rects.size()); ++i) {
                        float a = (rects[i].x1 - rects[i].x0) * (rects[i].y1 - rects[i].y0);
                        if (a > area) { area = a; big = i; }
                    }
                    if (big >= 0) {
                        R u = rects[big];
                        for (int pass = 0; pass < 3; ++pass) // grow through pieces touching the union (a margin of 4 GUI px)
                            for (auto const& r : rects)
                                if (r.x0 <= u.x1 + 4 && r.x1 >= u.x0 - 4 && r.y0 <= u.y1 + 4 && r.y1 >= u.y0 - 4) {
                                    u.x0 = std::min(u.x0, r.x0); u.y0 = std::min(u.y0, r.y0);
                                    u.x1 = std::max(u.x1, r.x1); u.y1 = std::max(u.y1, r.y1);
                                }
                        b[0] = u.x0 / vs.x; b[1] = u.y0 / vs.y; b[2] = u.x1 / vs.x; b[3] = u.y1 / vs.y;
                        ok   = true;
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
bool hudOnTop() { return gHudTop.load(std::memory_order_relaxed) != 0; }
bool playing() { return gHudTop.load(std::memory_order_relaxed) == 1; }
void uninstall() {
    if (!gInstalled) return;
    RfSceneStackUpdateHook::unhook();
    RfHudTopmostHook::unhook();
    gInstalled = false;
}
} // namespace rf::screens
