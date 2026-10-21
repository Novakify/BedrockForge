#include "client/Menu.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/input/KeyRegistry.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/deps/input/MouseAction.h"

#include "bf/Events.h"
#include "bf/Hud.h"
#include "client/Module.h"

namespace bedrock_forge::client::menu {

namespace {
using bf::hud::Rgba;

std::mutex                     gMutex;
std::shared_ptr<AbstractScene> gScene;
IClientInstance*               gClient = nullptr;
int                            gGoneFrames = 0;
Category                       gCategory = Category::Hud;
std::string                    gSelected; // module whose settings are shown
float                          gScroll = 0;
struct Click { float x, y; bool right; };
std::vector<Click> gClicks;
std::vector<int>   gWheel;
glm::vec2          gActionPos{0, 0}; // where the click being handled landed (sliders use it)
bf::events::Subscriptions gSubs;

// palette
constexpr Rgba kBack{0.04f, 0.05f, 0.07f, 0.92f}, kSide{0.07f, 0.09f, 0.12f, 1}, kCard{0.12f, 0.14f, 0.18f, 1},
    kCardHover{0.17f, 0.2f, 0.26f, 1}, kAccent{0.25f, 0.78f, 0.95f, 1}, kText{0.93f, 0.95f, 0.98f, 1},
    kDim{0.55f, 0.6f, 0.68f, 1}, kOn{0.2f, 0.85f, 0.55f, 1}, kOff{0.35f, 0.38f, 0.44f, 1}, kSoon{0.85f, 0.65f, 0.25f, 1};

bool ownsTop() {
    if (!gClient || !gScene) return false;
    return gClient->getSceneFactory().getCurrentSceneStack()->getTopScene() == gScene.get();
}
bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px < x + w && py >= y && py < y + h; }
float tw(std::string const& s) { return 6.0f * float(s.size()); }

struct Hit { float x, y, w, h; std::function<void(bool right)> action; };

void render(MinecraftUIRenderContext& ctx, ScreenView& view) {
    float const W = view.mSize->x, H = view.mSize->y;
    glm::vec2 const ptr = *view.mPointerLocationPrevious;
    std::vector<Hit> hits;
    auto& reg = Registry::get();

    bf::hud::fillBox(ctx, 0, 0, W, H, {0, 0, 0, 0.35f});
    float const mw = std::min(W - 16, 380.0f), mh = std::min(H - 16, 240.0f);
    float const mx = (W - mw) / 2, my = (H - mh) / 2;
    bf::hud::fillBox(ctx, mx, my, mw, mh, kBack);
    // title bar
    bf::hud::fillBox(ctx, mx, my, mw, 16, kSide);
    bf::hud::drawLabel(ctx, mx + 6, my + 4, "Bedrock Forge", kAccent);
    int planned = 0, active = 0;
    for (auto& m : reg.all()) { planned += m->planned; active += (m->enabled && !m->planned); }
    std::string stats = std::format("{} modules | {} on | {} planned", reg.all().size() - planned, active, planned);
    bf::hud::drawLabel(ctx, mx + 90, my + 4, stats, kDim);
    hits.push_back({mx + mw - 14, my + 2, 12, 12, [](bool) {
        if (gClient) gClient->getSceneFactory().getCurrentSceneStack()->schedulePopScreen(1);
    }});
    bf::hud::drawLabel(ctx, mx + mw - 11, my + 4, "x", inside(ptr.x, ptr.y, mx + mw - 14, my + 2, 12, 12) ? kAccent : kDim);

    // category column
    float const cw = 70, top = my + 18;
    bf::hud::fillBox(ctx, mx, top, cw, mh - 18, kSide);
    float cy = top + 4;
    for (Category c : kCategories) {
        bool sel = c == gCategory, hov = inside(ptr.x, ptr.y, mx, cy, cw, 14);
        if (sel || hov) bf::hud::fillBox(ctx, mx, cy, cw, 14, sel ? kCardHover : kCard);
        if (sel) bf::hud::fillBox(ctx, mx, cy, 2, 14, kAccent);
        bf::hud::drawLabel(ctx, mx + 7, cy + 3, categoryName(c), sel ? kText : kDim);
        hits.push_back({mx, cy, cw, 14, [c](bool) { gCategory = c; gScroll = 0; gSelected.clear(); }});
        cy += 15;
    }

    // module cards (2 columns), settings panel on the right third when a module is selected
    Module* sel = gSelected.empty() ? nullptr : reg.find(gSelected);
    float const settingsW = sel ? 120.0f : 0.0f;
    float const gx = mx + cw + 6, gw = mw - cw - 12 - (sel ? settingsW + 6 : 0);
    float const colW = (gw - 4) / 2, rowH = 24;
    std::vector<Module*> list;
    for (auto& m : reg.all()) if (m->category == gCategory) list.push_back(m.get());
    float const viewH = mh - 24;
    float const contentH = float((list.size() + 1) / 2) * (rowH + 4);
    gScroll = std::clamp(gScroll, 0.0f, std::max(0.0f, contentH - viewH));
    for (size_t i = 0; i < list.size(); ++i) {
        Module* m = list[i];
        float x = gx + float(i % 2) * (colW + 4), y = top + 4 + float(i / 2) * (rowH + 4) - gScroll;
        if (y < top || y + rowH > my + mh - 2) continue;
        bool hov = inside(ptr.x, ptr.y, x, y, colW, rowH);
        bf::hud::fillBox(ctx, x, y, colW, rowH, hov ? kCardHover : kCard);
        if (gSelected == m->id) bf::hud::fillBox(ctx, x, y, colW, 1, kAccent);
        Rgba pill = m->planned ? kSoon : m->enabled ? kOn : kOff;
        bf::hud::fillBox(ctx, x, y, 2, rowH, pill);
        std::string name = m->name.size() > size_t(colW / 6 - 6) ? m->name.substr(0, size_t(colW / 6 - 7)) + "." : m->name;
        bf::hud::drawLabel(ctx, x + 6, y + 3, name, m->planned ? kDim : kText);
        std::string state = m->planned ? "SOON" : m->enabled ? "ON" : "OFF";
        bf::hud::drawLabel(ctx, x + colW - tw(state) - 4, y + 3, state, pill);
        std::string desc = m->description.size() > size_t(colW / 5 - 2) ? m->description.substr(0, size_t(colW / 5 - 3)) + "." : m->description;
        bf::hud::drawLabel(ctx, x + 6, y + 13, desc, kDim, 0.8f);
        hits.push_back({x, y, colW, rowH, [m](bool right) {
            if (right || m->planned) { gSelected = (gSelected == m->id) ? "" : m->id; return; }
            Registry::get().setEnabled(*m, !m->enabled);
        }});
    }

    // settings panel
    if (sel) {
        float sx = mx + mw - settingsW - 6, sy = top + 4;
        bf::hud::fillBox(ctx, sx, sy, settingsW, mh - 26, kSide);
        bf::hud::drawLabel(ctx, sx + 5, sy + 4, sel->name, kAccent);
        float ly = sy + 17;
        auto wrap = [&](std::string const& text) {
            size_t per = size_t((settingsW - 10) / 4.8f);
            for (size_t p = 0; p < text.size() && ly < my + mh - 20; p += per) {
                bf::hud::drawLabel(ctx, sx + 5, ly, text.substr(p, per), kDim, 0.8f);
                ly += 9;
            }
        };
        wrap(sel->planned ? sel->description + " - on the roadmap." : sel->description);
        ly += 4;
        for (auto& s : sel->settings) {
            if (ly > my + mh - 22) break;
            if (s.kind == Setting::Kind::Toggle) {
                bf::hud::drawLabel(ctx, sx + 5, ly, s.label, kText, 0.8f);
                float bx = sx + settingsW - 22;
                bf::hud::fillBox(ctx, bx, ly - 1, 17, 9, s.on ? kOn : kOff);
                bf::hud::fillBox(ctx, s.on ? bx + 9 : bx + 1, ly, 7, 7, kText);
                Setting* sp = &s;
                hits.push_back({sx, ly - 2, settingsW, 12, [sp](bool) { sp->on = !sp->on; Registry::get().save(); }});
                ly += 13;
            } else {
                bf::hud::drawLabel(ctx, sx + 5, ly, s.label, kText, 0.8f);
                ly += 9;
                std::string v = s.step >= 1 ? std::format("{:.0f}", s.value) : std::format("{:.2f}", s.value);
                float bx = sx + 5;
                Setting* sp = &s;
                bf::hud::fillBox(ctx, bx, ly, 12, 10, kCard);
                bf::hud::drawLabel(ctx, bx + 3, ly + 1, "-", kText);
                hits.push_back({bx, ly, 12, 10, [sp](bool r) { sp->value = std::max(sp->min, sp->value - sp->step * (r ? 10 : 1)); Registry::get().save(); }});
                float barX = bx + 15, barW = settingsW - 45;
                bf::hud::fillBox(ctx, barX, ly + 3, barW, 4, kCard);
                float t = (s.value - s.min) / std::max(0.0001f, s.max - s.min);
                bf::hud::fillBox(ctx, barX, ly + 3, barW * std::clamp(t, 0.0f, 1.0f), 4, kAccent);
                hits.push_back({barX, ly, barW, 10, [sp, barX, barW](bool) {
                    float f = std::clamp((gActionPos.x - barX) / barW, 0.0f, 1.0f);
                    float v = sp->min + f * (sp->max - sp->min);
                    sp->value = sp->step > 0 ? std::round(v / sp->step) * sp->step : v;
                    Registry::get().save();
                }});
                bf::hud::fillBox(ctx, barX + barW + 3, ly, 12, 10, kCard);
                bf::hud::drawLabel(ctx, barX + barW + 6, ly + 1, "+", kText);
                hits.push_back({barX + barW + 3, ly, 12, 10, [sp](bool r) { sp->value = std::min(sp->max, sp->value + sp->step * (r ? 10 : 1)); Registry::get().save(); }});
                bf::hud::drawLabel(ctx, barX + barW / 2 - tw(v) * 0.4f, ly + 9, v, kDim, 0.8f);
                ly += 20;
            }
        }
    }
    bf::hud::drawLabel(ctx, mx + cw + 6, my + mh - 10, "L-click toggle | R-click settings | wheel scroll | Esc close", kDim, 0.7f);

    // apply input collected since the last frame
    for (int d : gWheel) gScroll += float(d) * 16.0f;
    gWheel.clear();
    for (auto const& c : gClicks)
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (inside(c.x, c.y, it->x, it->y, it->w, it->h)) {
                gActionPos = {c.x, c.y}; // sliders use the click position, not the hover pointer
                it->action(c.right);
                break;
            }
    gClicks.clear();
}
} // namespace

bool isOpen() {
    std::lock_guard lock(gMutex);
    return gScene != nullptr;
}

void open(IClientInstance& client) {
    std::lock_guard lock(gMutex);
    if (gScene) return;
    auto scene = client.getSceneFactory().createCommonDialogInfoScreen("Bedrock Forge", "");
    if (!scene) return;
    gScene      = scene;
    gClient     = &client;
    gGoneFrames = 0;
    gClicks.clear();
    gWheel.clear();
    client.getSceneFactory().getCurrentSceneStack()->pushScreen(scene, false);
}

void install() {
    auto& key = ll::input::KeyRegistry::getInstance().getOrCreateKey("bedrock_forge.menu", {0xA1 /* Right Shift */}, true);
    key.registerButtonDownHandler([](::FocusImpact, ::IClientInstance& client) { open(client); }, true);

    gSubs.on<ll::event::BeforeUIRenderEvent>([](ll::event::BeforeUIRenderEvent& ev) {
        std::lock_guard lock(gMutex);
        if (!gScene) return;
        if (!ownsTop()) {
            if (++gGoneFrames > 4) { gScene.reset(); gSelected.clear(); }
            return;
        }
        gGoneFrames = 0;
        if (bf::hud::isHudScreen(ev.screenView())) return; // the game behind our dialog
        ev.cancel();                                        // replace the empty dialog with our menu
        try { render(ev.uiRenderContext(), ev.screenView()); } catch (...) {}
    });

    gSubs.on<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
        std::lock_guard lock(gMutex);
        if (!gScene || !ownsTop()) return;
        char const  a = ev.actionButtonId();
        schar const d = ev.buttonData();
        if (a == MouseAction::ActionMove || a == MouseAction::ActionMoveRelative) return;
        float inv = *gClient->getGuiData()->mInvGuiScale;
        if (a == MouseAction::ActionWheel) {
            if (d != 0) gWheel.push_back(d > 0 ? -1 : 1);
            ev.cancel();
            return;
        }
        if ((a == MouseAction::ActionLeft || a == MouseAction::ActionRight) && d == MouseAction::DataDown)
            gClicks.push_back({float(ev.x()) * inv, float(ev.y()) * inv, a == MouseAction::ActionRight});
        if (d == MouseAction::DataDown) ev.cancel();
    });
}

void uninstall() {
    gSubs.clear();
    std::lock_guard lock(gMutex);
    gScene.reset();
}

} // namespace bedrock_forge::client::menu
