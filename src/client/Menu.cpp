#include "client/Menu.h"
#include "client/Diag.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <atomic>
#include <functional>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/event/input/KeyInputEvent.h"
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
#include "core/Win32Input.h"
#include "mc/client/player/LocalPlayer.h"

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
struct Click { float x, y; bool right; bool middle = false; };
std::string gSearch; // typed while the menu is open
bool        gMiddle = false; // the click being handled is a middle click
std::vector<Click> gClicks;
std::vector<int>   gWheel;
glm::vec2          gActionPos{0, 0}; // where the click being handled landed (sliders use it)
bf::events::Subscriptions gSubs;
std::atomic<IClientInstance*> gLastClient{nullptr};

// palette
constexpr Rgba kBack{0.04f, 0.05f, 0.07f, 0.92f}, kSide{0.07f, 0.09f, 0.12f, 1}, kCard{0.12f, 0.14f, 0.18f, 1},
    kCardHover{0.17f, 0.2f, 0.26f, 1}, kAccent{0.25f, 0.78f, 0.95f, 1}, kText{0.93f, 0.95f, 0.98f, 1},
    kDim{0.55f, 0.6f, 0.68f, 1}, kOn{0.2f, 0.85f, 0.55f, 1}, kOff{0.35f, 0.38f, 0.44f, 1}, kSoon{0.85f, 0.65f, 0.25f, 1};

// 1.26 doesn't hand back the scene we pushed from getTopScene(), so "is our dialog up" is judged by what renders:
// while our scene is open, a non-HUD screen rendering = our dialog.
std::chrono::steady_clock::time_point gOpenedAt{}, gDialogSeen{};
ScreenView const* gView = nullptr; // the dialog's view, latched on its first render after open
// Pop our dialog and forget the menu right away (caller holds gMutex).
void closeMenu() {
    if (gClient && gScene) gClient->getSceneFactory().getCurrentSceneStack()->schedulePopScreen(1);
    gScene.reset();
    gView = nullptr;
    gSelected.clear();
    gSearch.clear();
}
bool ownsTop() {
    if (!gClient || !gScene) return false;
    return std::chrono::steady_clock::now() - gDialogSeen < std::chrono::milliseconds(250);
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
    // search field (just type; Backspace deletes)
    {
        float sx = mx + mw - 120, sw = 100;
        bf::hud::fillBox(ctx, sx, my + 2, sw, 12, kCard);
        std::string shown = gSearch.empty() ? "type to search" : gSearch + "_";
        bf::hud::drawLabel(ctx, sx + 4, my + 4, shown, gSearch.empty() ? kDim : kText, 0.8f);
    }
    hits.push_back({mx + mw - 14, my + 2, 12, 12, [](bool) { closeMenu(); }});
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
    auto lower = [](std::string s) { for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch))); return s; };
    std::string const q = lower(gSearch);
    for (auto& m : reg.all())
        if (q.empty() ? m->category == gCategory : (lower(m->name).find(q) != std::string::npos || lower(m->description).find(q) != std::string::npos))
            list.push_back(m.get());
    std::stable_sort(list.begin(), list.end(), [](Module* a, Module* b) {
        if (a->favorite != b->favorite) return a->favorite;
        return !a->planned && b->planned;
    });
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
        if (m->favorite) bf::hud::drawLabel(ctx, x + colW - tw(m->planned ? "SOON" : m->enabled ? "ON" : "OFF") - 12, y + 3, "*", kSoon);
        std::string state = m->planned ? "SOON" : m->enabled ? "ON" : "OFF";
        bf::hud::drawLabel(ctx, x + colW - tw(state) - 4, y + 3, state, pill);
        std::string desc = m->description.size() > size_t(colW / 5 - 2) ? m->description.substr(0, size_t(colW / 5 - 3)) + "." : m->description;
        bf::hud::drawLabel(ctx, x + 6, y + 13, desc, kDim, 0.8f);
        hits.push_back({x, y, colW, rowH, [m](bool right) {
            if (gMiddle) { m->favorite = !m->favorite; Registry::get().save(); return; }
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
    bf::hud::drawLabel(ctx, mx + cw + 6, my + mh - 10, "L toggle | R settings | Mid favourite | type to search | Esc close", kDim, 0.7f);

    // apply input collected since the last frame
    for (int d : gWheel) gScroll += float(d) * 16.0f;
    gWheel.clear();
    for (auto const& c : gClicks)
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (inside(c.x, c.y, it->x, it->y, it->w, it->h)) {
                gActionPos = {c.x, c.y}; // sliders use the click position, not the hover pointer
                gMiddle    = c.middle;
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
    gOpenedAt   = std::chrono::steady_clock::now();
    gDialogSeen = {};
    gView       = nullptr;
    ++diag::menuOpens;
    gClicks.clear();
    gWheel.clear();
    client.getSceneFactory().getCurrentSceneStack()->pushScreen(scene, false);
}

void install() {
    // The game reports both Shift keys as VK_SHIFT (0x10), so a key binding can't tell Right Shift apart.
    // Watch raw key events instead and check the physical right Shift with GetAsyncKeyState.
    // Insert (0x2D) also opens the menu, as a fallback for keyboards without a right Shift.
    gSubs.on<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
        if (!ev.isDown()) return;
        int const k = ev.keyCode();
        bool const rshift = (k == 0x10 || k == 0xA1) && (win32::keyDown(0xA1) || win32::keyDownSync(0xA1)) &&
                            !win32::keyDown(0xA0) && !win32::keyDownSync(0xA0);
        if (!rshift && k != 0x2D) return;
        IClientInstance* client = gLastClient.load();
        if (!client) return;
        {
            std::lock_guard lock(gMutex);
            if (gScene) return;
        }
        if (!client->getLocalPlayer()) return; // only in a world
        open(*client);
    });
    // Polling fallback: some builds never deliver Shift through the key event, so also watch the physical key
    // state once per HUD frame (edge-triggered).
    gSubs.on<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
        static bool wasDown = false;
        auto& client = ev.uiRenderContext().mClient;
        gLastClient  = &client;
        if (!bf::hud::isHudScreen(ev.screenView())) return;
        bool down = win32::gameFocused() && (win32::keyDown(0xA1) || win32::keyDown(0x2D));
        bool edge = down && !wasDown;
        wasDown   = down;
        if (!edge || !client.getLocalPlayer()) return;
        {
            std::lock_guard lock(gMutex);
            if (gScene) return;
        }
        open(client);
    });

    gSubs.on<ll::event::BeforeUIRenderEvent>([](ll::event::BeforeUIRenderEvent& ev) {
        std::lock_guard lock(gMutex);
        if (!gScene) return;
        auto const now = std::chrono::steady_clock::now();
        if (bf::hud::isHudScreen(ev.screenView())) {
            // only the HUD is rendering: once the dialog has been gone a while (Esc / OK), forget the menu
            if (now - gOpenedAt > std::chrono::milliseconds(800) && now - gDialogSeen > std::chrono::milliseconds(400)) {
                gScene.reset();
                gView = nullptr;
                gSelected.clear();
                gSearch.clear();
            }
            return;
        }
        // Other non-HUD layers (toasts, overlays) also render; only draw over the dialog we pushed, which is the
        // first non-HUD view to render after open.
        // (Every non-HUD view is covered while the menu is open: the dialog can render through more than one view,
        // and Esc / x reset the menu directly, so stray overlays can't keep it alive.) Record the names for diag.
        if (auto const& tree = ev.screenView().mVisualTree; tree) diag::noteScreen(*tree->mRootControlName);
        gDialogSeen = now;
        ev.cancel(); // replace the empty dialog with our menu
        try { render(ev.uiRenderContext(), ev.screenView()); } catch (...) {}
    });

    gSubs.on<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
        std::lock_guard lock(gMutex);
        if (!gScene || !ownsTop()) return;
        int const k = ev.keyCode();
        if (k == 0x1B) {                                   // Esc: clear the search first, then close
            ev.cancel();
            if (!ev.isDown()) return;
            if (!gSearch.empty()) gSearch.clear();
            else closeMenu();
            return;
        }
        if (k == 0xA1 || k == 0xA0 || k == 0x10) return;  // shifts
        ev.cancel();                                       // the menu owns the keyboard while open
        if (!ev.isDown()) return;
        if (k == 0x08) { if (!gSearch.empty()) gSearch.pop_back(); gScroll = 0; return; }
        if (gSearch.size() >= 20) return;
        if ((k >= 0x41 && k <= 0x5A) || (k >= 0x30 && k <= 0x39)) { gSearch += char(std::tolower(k)); gScroll = 0; }
        else if (k == 0x20) gSearch += ' ';
    });
    gSubs.on<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
        std::lock_guard lock(gMutex);
        if (!gScene || !ownsTop()) return;
        char const  a = ev.actionButtonId();
        schar const d = ev.buttonData();
        if (a == MouseAction::ActionMove || a == MouseAction::ActionMoveRelative) return;
        float inv = gClient->getGuiData()->mInvGuiScale;
        if (a == MouseAction::ActionWheel) {
            if (d != 0) gWheel.push_back(d > 0 ? -1 : 1);
            ev.cancel();
            return;
        }
        if ((a == MouseAction::ActionLeft || a == MouseAction::ActionRight || a == MouseAction::ActionMiddle) && d == MouseAction::DataDown)
            gClicks.push_back({float(ev.x()) * inv, float(ev.y()) * inv, a == MouseAction::ActionRight, a == MouseAction::ActionMiddle});
        if (d == MouseAction::DataDown) ev.cancel();
    });
}

void uninstall() {
    gSubs.clear();
    std::lock_guard lock(gMutex);
    gScene.reset();
}

} // namespace bedrock_forge::client::menu
