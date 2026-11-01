// Bedrock Forge client menu (2026-10-31): glass UI drawn by Renderer Foundation's glyph pass.
//
// Right Shift opens a four-button glass wheel (Mods / HUD / Shaders / Settings) over a blurred world; Mods opens the
// glass window (tab pills, icon rail, search, 2-column module grid, per-module settings pages, shaderpacks, client
// settings, LyricsFlow). Look: frosted, refracting glass with a neon purple + white theme (design inspired by the
// Blueberry client's layout, our own palette). Prototype + shader reference: the "Bedrock Forge Glass UI" artifact.
//
// How it draws: the menu still pushes an empty native dialog (that gives us the mouse cursor and input focus) and
// cancels the dialog's own rendering. Every frame it builds screen-space glyph quads (BfLyrics.h: SOLID / GLASS /
// PETAL / BACKDROP + Outfit MSDF text + the white icon atlas) and hands them to the LyricsFlow frame store as an
// overlay; RF copies the frame, blurs it and draws the glass at Present. Without RF the menu is invisible, so the
// old flat menu is kept as a fallback (BF_NO_RF below / when the glyph store is never read).
#include "client/Menu.h"
#include "client/Diag.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/MouseAction.h"

#include "bf/Events.h"
#include "bf/Hud.h"
#include "client/Module.h"
#include "client/Loadouts.h"
#include "client/ShaderPacks.h"
#include "core/Win32Input.h"
#include "lyrics/BfLyrics.h"
#include "lyrics/core/Engine.h"
#include "lyrics/core/Text.h"
#include "lyrics/win/Win.h"

namespace bedrock_forge::client::menu {

namespace {
namespace lyr = ::bf::lyrics;
using Clock   = std::chrono::steady_clock;

// ---------------------------------------------------------------- state
std::mutex                     gMutex;
std::shared_ptr<AbstractScene> gScene;
IClientInstance*               gClient = nullptr;
std::chrono::steady_clock::time_point gOpenedAt{}, gDialogSeen{};
struct Click { float x, y; bool right; bool middle = false; };
std::vector<Click> gClicks;      // GUI units
std::vector<int>   gWheel;
std::string        gSearch;
bool               gHeld = false; // left button held (slider drag)
bf::events::Subscriptions     gSubs;
std::atomic<IClientInstance*> gLastClient{nullptr};

enum class View { Wheel, Window };
enum class Page { Mods, ModSettings, Shaders, Loadout, Client, Lyrics };
View        gView     = View::Wheel;
Page        gPage     = Page::Mods;
int         gTab      = 0;
std::string gModPage;            // module shown on Page::ModSettings
std::string gLoadoutPage;        // loadout shown on Page::Loadout
float       gScroll   = 0;       // design px: scroll target (the wheel moves it, pages clamp it)
float       gScrollAt = 0;       // design px: where the list is drawn, eased towards gScroll every frame
int64_t     gScrollMs = 0;
std::atomic<int64_t> gCamUntilMs{0}; // BF_ClientMenuCamera: RF swings the camera round to the player until then
int64_t     gViewAtMs = 0;       // when the current view started (open / switch animation)
std::string gDrag;               // slider being dragged ("<module>/<setting>")
std::string gToast;
int64_t     gToastMs  = 0;

int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
float   ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

void closeMenu() { // caller holds gMutex
    if (gClient && gScene) gClient->getSceneFactory().getCurrentSceneStack()->schedulePopScreen(1);
    gScene.reset();
    gSearch.clear();
    gDrag.clear();
    gHeld = false;
    lyr::FrameStore::get().setOverlay({});
    gCamUntilMs = 0;
}
bool ownsTop() { return gClient && gScene && Clock::now() - gDialogSeen < std::chrono::milliseconds(250); }

Module* uiModule() { return Registry::get().find("client_ui"); }
float   look(char const* id, float fallback) { auto* m = uiModule(); return m ? m->num(id, fallback) : fallback; }
bool    lookOn(char const* id, bool fallback) { auto* m = uiModule(); return m ? m->flag(id, fallback) : fallback; }

void resetScroll() { gScroll = gScrollAt = 0; }
void setView(View v) { gView = v; gViewAtMs = nowMs(); resetScroll(); }
void showPage(Page p) { gPage = p; resetScroll(); if (gView != View::Window) setView(View::Window); }
void toast(std::string s) { gToast = std::move(s); gToastMs = nowMs(); }

// ---------------------------------------------------------------- theme (neon purple + white)
constexpr float kH = 760.0f; // design units: the screen is always 760 tall (the prototype's 1280 x 760 frame)
struct Rgb { int r, g, b; };
constexpr Rgb kAccent{177, 76, 255}, kLilac{233, 213, 255}, kDark{19, 10, 36}, kWhite{255, 255, 255};
uint32_t rgba(Rgb c, float a) {
    return uint32_t(c.r) | uint32_t(c.g) << 8 | uint32_t(c.b) << 16 | uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24;
}

// ---------------------------------------------------------------- glyph builder (design px -> BF screen glyphs)
struct Gfx {
    float W = 1280, H = kH;  // design units
    float px = 1;            // real screen px per design unit
    float blurK = 1, refrK = 1, tintK = 1;
    lyr::win::ClientUiAssets a;
    std::vector<BfGlyphV1> shapes, top; // top = icons + text, drawn after every shape
    float clipTop = 0, clipBot = 0;     // design px; clipBot > clipTop = everything placed now is cut to that band (RF fades the edges)

    void place(BfGlyphV1& q, float cx, float cy, float hw, float hh) const {
        q.pos[0]   = cx / kH;
        q.pos[1]   = cy / kH;
        q.right[0] = hw / kH;
        q.up[1]    = -hh / kH; // screen y grows downwards; +up = towards the top
        q.uv[0] = 0; q.uv[1] = 0; q.uv[2] = 1; q.uv[3] = 1;
        q.flags |= BF_GLYPH_SCREEN;
        if (clipBot > clipTop) { q._pad0 = clipTop / kH; q._pad1 = clipBot / kH; }
    }
    void backdrop(float cx, float cy, float clearR, float feather, float blur, float dark, float alpha) {
        BfGlyphV1 q{};
        float     hw = std::max(cx, W - cx) + 2, hh = std::max(cy, H - cy) + 2; // centred on (cx, cy), covers the screen
        place(q, cx, cy, hw, hh);
        q.fill    = rgba({0, 0, 0}, dark);
        q.pxRange = std::clamp(blur * blurK, 0.0f, 1.0f);
        q.glowW   = clearR * px;
        q.glowK   = feather * px;
        q.alpha   = alpha;
        q.flags  |= BF_GLYPH_BACKDROP;
        shapes.push_back(q);
    }
    void glass(float x, float y, float w, float h, float r, Rgb tint, float tintA, float bevel, float refr, float blur, float rimA,
               float innerA, float alpha) {
        if (alpha <= 0.003f || w <= 0 || h <= 0) return;
        BfGlyphV1 q{};
        place(q, x + w / 2, y + h / 2, w / 2, h / 2);
        float half = std::min(w, h) / 2;
        q.outlineW = std::clamp(r / std::max(0.01f, half), 0.0f, 1.0f);
        q.fill     = rgba(tint, tintA * tintK);
        q.glowW    = bevel * px;
        q.glowK    = refr * px * refrK;
        q.pxRange  = std::clamp(blur * blurK, 0.0f, 1.0f);
        q.outline  = rgba(kWhite, rimA);
        q.glow     = rgba(kAccent, innerA);
        q.alpha    = alpha;
        q.flags   |= BF_GLYPH_SOLID | BF_GLYPH_GLASS;
        shapes.push_back(q);
    }
    void petal(float cx, float cy, float r1, float r2, float angDeg, float halfDeg, Rgb tint, float tintA, float innerA, float alpha) {
        BfGlyphV1 q{};
        place(q, cx, cy, r2 + 1.5f, r2 + 1.5f);
        q.outlineW = r1 / r2;
        float a    = std::fmod(angDeg + 360.0f, 360.0f);
        q.fill2    = uint32_t(a * 100.0f + 0.5f) | uint32_t(halfDeg * 100.0f + 0.5f) << 16;
        q.fill     = rgba(tint, tintA * tintK);
        q.glowW    = 34.0f * px;
        q.glowK    = 30.0f * px * refrK;
        q.pxRange  = std::clamp(0.15f * blurK, 0.0f, 1.0f);
        q.outline  = rgba(kWhite, 1.0f);
        q.glow     = rgba(kAccent, innerA);
        q.alpha    = alpha;
        q.flags   |= BF_GLYPH_PETAL | BF_GLYPH_GLASS;
        shapes.push_back(q);
    }
    void solid(float x, float y, float w, float h, float r, uint32_t c, float alpha, float softD = 0, bool onTop = false) {
        if (alpha <= 0.003f || w <= 0 || h <= 0) return;
        BfGlyphV1 q{};
        place(q, x + w / 2, y + h / 2, w / 2, h / 2);
        float half = std::min(w, h) / 2;
        q.outlineW = std::clamp(r / std::max(0.01f, half), 0.0f, 1.0f);
        q.fill = q.fill2 = c;
        q.glowW    = std::max(1.0f, softD * px);
        q.alpha    = alpha;
        q.flags   |= BF_GLYPH_SOLID;
        (onTop ? top : shapes).push_back(q);
    }
    /// neon halo: a soft accent glow that spills `s` design px around a rounded rect
    void halo(float x, float y, float w, float h, float r, float alpha, float s = 11) {
        solid(x - s, y - s, w + 2 * s, h + 2 * s, r + s, rgba(kAccent, 1.0f), 0.55f * alpha, 2 * s);
    }
    void icon(std::string const& name, float cx, float cy, float size, uint32_t c, float alpha) {
        auto it = a.iconRects.find(name);
        if (a.icons < 0 || it == a.iconRects.end() || alpha <= 0.003f) return;
        BfGlyphV1 q{};
        place(q, cx, cy, size / 2, size / 2);
        q.uv[0] = it->second.u0; q.uv[1] = it->second.v0; q.uv[2] = it->second.u1; q.uv[3] = it->second.v1;
        q.fill = q.fill2 = c;
        q.atlas = uint32_t(a.icons);
        q.alpha = alpha;
        q.flags |= BF_GLYPH_IMAGE;
        top.push_back(q);
    }
    lyr::Font const* font(bool bold) const { return bold ? a.bold : a.regular; }
    float textW(std::string_view s, float size, bool bold = false) const {
        auto const* f = font(bold);
        return f ? f->width(s) * size : float(s.size()) * size * 0.52f;
    }
    /// text vertically centred on midY (cap height), align 0 left / 1 centre / 2 right of x
    void text(std::string_view s, float x, float midY, float size, uint32_t c, float alpha, bool bold = false, int align = 0, float glow = 0) {
        auto const* f = font(bold);
        if (!f || s.empty() || alpha <= 0.01f) return;
        float pen  = align == 1 ? x - textW(s, size, bold) / 2 : align == 2 ? x - textW(s, size, bold) : x;
        float base = midY + f->capHeight * size * 0.5f;
        for (uint32_t cp : lyr::text::codepoints(s)) {
            auto const* g = f->find(cp);
            if (!g) g = f->find('?');
            if (g && !g->empty) {
                BfGlyphV1 q{};
                place(q, pen + (g->pl + g->pr) * 0.5f * size, base - (g->pb + g->pt) * 0.5f * size, (g->pr - g->pl) * 0.5f * size,
                      (g->pt - g->pb) * 0.5f * size);
                q.uv[0] = g->u0; q.uv[1] = g->v0; q.uv[2] = g->u1; q.uv[3] = g->v1;
                q.fill = q.fill2 = c;
                q.alpha   = alpha;
                q.atlas   = f->atlasIndex;
                q.pxRange = f->pxRange;
                q.flags  |= f->flags;
                if (glow > 0) { q.glow = rgba(kLilac, 1.0f); q.glowW = 0.22f; q.glowK = glow; }
                top.push_back(q);
            }
            pen += (g ? g->advance : 0.5f) * size;
        }
    }
    std::string ellip(std::string s, float maxW, float size, bool bold = false) const {
        if (textW(s, size, bold) <= maxW) return s;
        while (!s.empty() && textW(s + "...", size, bold) > maxW) s.pop_back();
        return s + "...";
    }
};

// ---------------------------------------------------------------- interaction
struct Hit {
    float x, y, w, h;
    std::function<void(bool right)> action;
    std::string drag; // slider id: dragged while the button is held
    std::string mod;  // module row: a middle click pins / unpins it
};
float gActionX = 0; // design-px x of the click being handled (sliders)
bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px < x + w && py >= y && py < y + h; }

struct UiFrame {
    Gfx               g;
    std::vector<Hit>  hits;
    float             mx = -1, my = -1; // pointer (design px)
    bool over(float x, float y, float w, float h) const { return inside(mx, my, x, y, w, h); }
};

// Everything drawn while a ClipBand lives is cut to [top, bot] (design px), and so are the click areas added meanwhile.
struct ClipBand {
    UiFrame& f;
    float    top, bot;
    size_t   hit0;
    bool     live = true;
    ClipBand(UiFrame& fr, float t, float b) : f(fr), top(t), bot(b), hit0(fr.hits.size()) { f.g.clipTop = t; f.g.clipBot = b; }
    ClipBand(ClipBand const&)            = delete;
    ClipBand& operator=(ClipBand const&) = delete;
    void done() noexcept { // compacts in place (no allocation: also runs from the destructor)
        if (!live) return;
        live = false;
        f.g.clipTop = f.g.clipBot = 0;
        size_t out = hit0;
        for (size_t i = hit0; i < f.hits.size(); ++i) {
            Hit&  h  = f.hits[i];
            float y0 = std::max(h.y, top), y1 = std::min(h.y + h.h, bot);
            if (y1 - y0 < 2) continue;
            h.y = y0; h.h = y1 - y0;
            if (out != i) f.hits[out] = std::move(h);
            ++out;
        }
        f.hits.erase(f.hits.begin() + std::ptrdiff_t(out), f.hits.end());
    }
    ~ClipBand() { done(); }
};

// a toggle switch at (x, y): 34 x 19
void toggleSwitch(UiFrame& f, float x, float y, bool on, bool onAccentPill, float a) {
    auto& g = f.g;
    if (on && !onAccentPill) g.halo(x, y, 34, 19, 9.5f, 0.6f * a, 6);
    g.solid(x, y, 34, 19, 9.5f, on ? (onAccentPill ? rgba(kWhite, 0.42f) : rgba(kAccent, 1)) : rgba(kWhite, 0.2f), a, 0, true);
    g.solid(on ? x + 17 : x + 2, y + 2, 15, 15, 7.5f, rgba(kWhite, 1), a, 0, true);
}

// ---------------------------------------------------------------- wheel
struct Petal { char const* label; char const* icon; float ang, half; };
constexpr Petal kPetals[4] = {{"MODS", "home", 270, 40.1f}, {"HUD", "pencil", 180, 32.1f}, {"SHADERS", "sparkle", 0, 32.1f}, {"SETTINGS", "gear", 90, 40.1f}};

void petalGeo(Gfx const& g, float& cx, float& cy, float& r1, float& r2) {
    cx = g.W / 2;
    cy = g.H / 2;
    r2 = std::min(std::min(g.W, g.H) * 0.42f, 330.0f);
    r1 = r2 * 0.56f;
}
int petalAt(Gfx const& g, float x, float y) {
    float cx, cy, r1, r2;
    petalGeo(g, cx, cy, r1, r2);
    float dx = x - cx, dy = y - cy, r = std::sqrt(dx * dx + dy * dy);
    if (r < r1 - 6 || r > r2 + 6) return -1;
    float an = std::atan2(dy, dx) * 180.0f / 3.14159265f;
    for (int i = 0; i < 4; ++i) {
        float d = std::fmod(an - kPetals[i].ang + 540.0f, 360.0f) - 180.0f;
        if (std::abs(d) < kPetals[i].half + 3) return i;
    }
    return -1;
}
void openPetal(int i) {
    if (i == 0) { gTab = 0; showPage(Page::Mods); }
    if (i == 1) { gTab = 2; showPage(Page::Mods); }         // HUD modules + HUD Layout
    if (i == 2) showPage(Page::Shaders);
    if (i == 3) showPage(Page::Client);
}

void drawWheel(UiFrame& f, float t) {
    auto& g = f.g;
    float cx, cy, r1, r2;
    petalGeo(g, cx, cy, r1, r2);
    if (lookOn("worldblur", true)) g.backdrop(cx, cy, r1 * 0.45f, r1 * 0.6f, 0.95f * t, 0.06f, 1.0f);
    int const hot = petalAt(g, f.mx, f.my);
    for (int i = 0; i < 4; ++i) {
        auto const& p    = kPetals[i];
        float const h    = i == hot ? 1.0f : 0.0f;
        float const grow = (1.0f - t) * 26.0f - h * 6.0f;
        g.petal(cx, cy, r1 + grow, r2 - grow, p.ang, p.half, h > 0 ? kAccent : kWhite, 0.04f + 0.34f * h, 0.18f + 0.35f * h, std::min(1.0f, t * 3));
        float rm = (r1 + r2) / 2, ar = p.ang * 3.14159265f / 180.0f;
        float lx = cx + std::cos(ar) * rm, ly = cy + std::sin(ar) * rm;
        float s  = 1.0f + 0.08f * h;
        g.icon(p.icon, lx, ly - 13 * s, 30 * s, rgba(kWhite, 1), t);
        g.text(p.label, lx, ly + 16 * s, 15 * s, rgba(kWhite, 1), t, false, 1, 0.55f + 0.4f * h);
    }
    g.text("CLICK A SECTION  -  ESC TO CLOSE", cx, g.H - 28, 11.5f, rgba(kWhite, 0.9f), t, false, 1, 0.3f);
}

// ---------------------------------------------------------------- window
constexpr char const* kTabLabel[6] = {"QUICK ACCESS", "VISUAL", "HUD", "COMBAT", "CAMERA", "MISC"};
constexpr char const* kTabIcon[6]  = {"pin", "eye", "monitor", "sword", "camera", "puzzle"};
bool inTab(Module const& m, int t) {
    if (m.id == "client_ui") return false;
    switch (t) {
    case 0: return m.favorite;
    case 1: return m.category == Category::Visual || m.category == Category::World;
    case 2: return m.category == Category::Hud;
    case 3: return m.category == Category::Combat;
    case 4: return m.category == Category::Camera;
    default: return m.category == Category::Misc || m.category == Category::Movement;
    }
}
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

struct Win { float x, y, w, h, mainX, mainW, bodyY, bodyH; };
Win winGeo(Gfx const& g) {
    Win w;
    w.w     = std::min(1060.0f, g.W - 32);
    w.h     = std::min(560.0f, g.H - 40);
    w.x     = (g.W - w.w) / 2;
    w.y     = (g.H - w.h) / 2;
    w.mainX = w.x + 12 + 52 + 14;
    w.mainW = w.w - 78 - 18;
    w.bodyY = w.y + 16 + 34 + 12;
    w.bodyH = w.h - (w.bodyY - w.y) - 16;
    return w;
}

// glass presets
void pillGlass(Gfx& g, float x, float y, float w, float h, float r, bool on, float a, float hover = 0) {
    if (on) {
        g.halo(x, y, w, h, r, a);
        g.glass(x, y, w, h, r, kAccent, 0.82f, 9, 5, 0.85f, 0.8f, 0.0f, a);
    } else {
        g.glass(x, y, w, h, r, kDark, 0.34f - 0.08f * hover, 9, 5, 0.85f, 0.6f + 0.25f * hover, 0.10f + 0.14f * hover, a);
    }
}

void row(UiFrame& f, float x, float y, float w, float h, std::string const& title, std::string const& desc, float a, bool on = false) {
    auto& g = f.g;
    pillGlass(g, x, y, w, h, 12, on, a, f.over(x, y, w, h) ? 1.0f : 0.0f);
    float ty = desc.empty() ? y + h / 2 : y + 17;
    g.text(g.ellip(title, w - 150, 13.5f, true), x + 14, ty, 13.5f, rgba(kWhite, 1), a, true, 0, on ? 0.6f : 0);
    if (!desc.empty()) g.text(g.ellip(desc, w - 150, 11.5f), x + 14, y + 33, 11.5f, rgba(on ? kWhite : kLilac, on ? 0.82f : 0.62f), a);
}

void slider(UiFrame& f, std::string const& id, float x, float y, float w, std::string const& title, std::string const& desc, float& value,
            float mn, float mx, float step, std::string const& unit, float a, std::function<void()> const& changed) {
    auto& g = f.g;
    float const h = 64;
    row(f, x, y, w, h, title, desc, a);
    std::string v = step >= 1 ? std::format("{:.0f}{}", value, unit) : step >= 0.1f ? std::format("{:.1f}{}", value, unit) : std::format("{:.2f}{}", value, unit);
    g.text(v, x + w - 14, y + 17, 12.5f, rgba(kWhite, 1), a, true, 2);
    float bx = x + 14, bw = w - 28, by = y + 49;
    float t  = std::clamp((value - mn) / std::max(1e-4f, mx - mn), 0.0f, 1.0f);
    g.solid(bx, by - 2.5f, bw, 5, 2.5f, rgba(kWhite, 0.22f), a, 0, true);
    g.solid(bx, by - 2.5f, bw * t, 5, 2.5f, rgba(kAccent, 1), a, 0, true);
    g.solid(bx - 4, by - 6.5f, bw * t + 8, 13, 6.5f, rgba(kAccent, 1), 0.35f * a, 9, false); // neon under the fill
    g.solid(bx + bw * t - 7.5f, by - 7.5f, 15, 15, 7.5f, rgba(kWhite, 1), a, 0, true);
    float* vp = &value;
    auto   set = [vp, bx, bw, mn, mx, step, changed](float px) {
        float v = mn + std::clamp((px - bx) / bw, 0.0f, 1.0f) * (mx - mn);
        if (step > 0) v = std::round(v / step) * step;
        *vp = std::clamp(v, mn, mx);
        changed();
    };
    f.hits.push_back({bx - 8, by - 12, bw + 16, 24, [set](bool) { set(gActionX); }, id});
    if (gDrag == id && gHeld) set(f.mx); // dragging
}

void toggleRow(UiFrame& f, float x, float y, float w, std::string const& title, std::string const& desc, bool on, float a, std::function<void()> flip) {
    row(f, x, y, w, 50, title, desc, a);
    toggleSwitch(f, x + w - 14 - 34, y + 15.5f, on, false, a);
    f.hits.push_back({x, y, w, 50, [flip](bool) { flip(); }});
}

void pageHeader(UiFrame& f, float x, float y, float w, std::string const& title, std::string const& crumb, float a, int toggle = -1,
                std::function<void()> flip = {}) {
    auto& g = f.g;
    g.text(title, x + 2, y + 13, 17, rgba(kWhite, 1), a, true, 0, 0.5f);
    if (!crumb.empty()) g.text(crumb, x + 2, y + 34, 11, rgba(kLilac, 0.62f), a);
    if (toggle >= 0) {
        toggleSwitch(f, x + w - 34, y + 4, toggle == 1, false, a);
        f.hits.push_back({x + w - 40, y, 44, 28, [flip](bool) { flip(); }});
    }
}

// slim scroll indicator at the window's right edge, only when the list is longer than its area
void scrollBar(Gfx& g, Win const& w, float top, float bot, float content, float at, float a) {
    float const view = bot - top;
    if (content <= view + 1 || view < 40) return;
    float const h = std::max(28.0f, view * view / content);
    float const p = std::clamp(at / (content - view), 0.0f, 1.0f);
    float const x = w.x + w.w - 13;
    g.solid(x, top, 3, view, 1.5f, rgba(kWhite, 1), 0.08f * a);
    g.solid(x, top + (view - h) * p, 3, h, 1.5f, rgba(kLilac, 1), 0.55f * a);
}

void drawMods(UiFrame& f, Win const& w, float a) {
    auto& g = f.g;
    auto& reg = Registry::get();
    std::vector<Module*> list;
    std::string const q = lower(gSearch);
    for (auto& m : reg.all())
        if (m->id != "client_ui" && (q.empty() ? inTab(*m, gTab) : (lower(m->name).find(q) != std::string::npos || lower(m->description).find(q) != std::string::npos)))
            list.push_back(m.get());
    std::stable_sort(list.begin(), list.end(), [](Module* l, Module* r) {
        if (l->favorite != r->favorite) return l->favorite;
        return !l->planned && r->planned;
    });
    float const colW = (w.mainW - 12 - 8) / 2, rowH = 42, gap = 10;
    float const contentH = float((list.size() + 1) / 2) * (rowH + gap);
    gScroll = std::clamp(gScroll, 0.0f, std::max(0.0f, contentH - w.bodyH));
    gScrollAt = std::clamp(gScrollAt, -60.0f, std::max(0.0f, contentH - w.bodyH) + 60.0f);
    ClipBand clip(f, w.bodyY - 6, w.bodyY + w.bodyH + 6); // RF's 10 px edge fade is centred on these lines
    scrollBar(g, w, w.bodyY + 2, w.bodyY + w.bodyH, contentH, gScrollAt, a);
    if (list.empty()) g.text(gSearch.empty() ? "Nothing pinned yet: middle-click a module to pin it here." : "No modules match \"" + gSearch + "\".",
                             w.mainX + 4, w.bodyY + 16, 13, rgba(kLilac, 0.62f), a);
    for (size_t i = 0; i < list.size(); ++i) {
        Module* m = list[i];
        float   x = w.mainX + 2 + float(i % 2) * (colW + 12), y = w.bodyY + 2 + float(i / 2) * (rowH + gap) - gScrollAt;
        if (y + rowH < w.bodyY - 10 || y > w.bodyY + w.bodyH + 10) continue; // rows partly in view are cut by the clip band
        bool const on  = m->enabled && !m->planned;
        float const hv = f.over(x, y, colW, rowH) ? 1.0f : 0.0f;
        pillGlass(g, x, y, colW, rowH, 12, on, a, hv);
        float tx = x + 14;
        if (m->favorite) { g.icon("pin", tx + 7, y + rowH / 2, 15, rgba(on ? kWhite : kAccent, 1), a); tx += 25; }
        float right = x + colW - 10;
        if (m->planned) {
            g.text("SOON", right - 4, y + rowH / 2, 11, rgba(kLilac, 0.7f), a, true, 2);
        } else {
            toggleSwitch(f, right - 34, y + (rowH - 19) / 2, on, on, a);
            float gx = right - 34 - 10 - 26;
            bool  gh = f.over(gx, y + 8, 26, 26);
            g.icon("gear", gx + 13, y + rowH / 2, 16, rgba(kWhite, on ? 0.9f : (gh ? 1.0f : 0.6f)), a);
            f.hits.push_back({x, y, colW, rowH, [m](bool right) {
                if (right) { gModPage = m->id; showPage(Page::ModSettings); return; }
                Registry::get().setEnabled(*m, !m->enabled);
            }, "", m->id});
            f.hits.push_back({gx, y + 6, 26, 30, [m](bool) { gModPage = m->id; showPage(Page::ModSettings); }});
        }
        g.text(g.ellip(m->name, right - 80 - tx, 14), tx, y + rowH / 2, 14, rgba(m->planned ? kLilac : kWhite, m->planned ? 0.6f : 1), a, false, 0,
               on ? 0.55f : 0);
        if (m->planned) f.hits.push_back({x, y, colW, rowH, [m](bool) { gModPage = m->id; showPage(Page::ModSettings); }, "", m->id});
    }
}

// Sub-page scrolling: rows under the page header slide with the eased scroll (gScrollAt); rows partly in view are
// cut by the clip band. Call fits() before drawing a row (false = fully out of view, skip), end() once at the bottom.
struct Scroller {
    Win const& w;
    float      top, y, alpha;
    ClipBand   clip;
    Scroller(UiFrame& f, Win const& win, float a)
        : w(win), top(win.bodyY + 48), y(win.bodyY + 48 - gScrollAt), alpha(a), clip(f, win.bodyY + 43, win.bodyY + win.bodyH + 6) {}
    bool fits(float h) const { return y + h >= top - 10 && y <= w.bodyY + w.bodyH + 10; }
    void end() {
        float content = y + gScrollAt - top, mx = std::max(0.0f, content - (w.bodyY + w.bodyH - top));
        gScroll   = std::clamp(gScroll, 0.0f, mx);
        gScrollAt = std::clamp(gScrollAt, -60.0f, mx + 60.0f);
        clip.done();
        scrollBar(clip.f.g, w, top, w.bodyY + w.bodyH, content, gScrollAt, alpha);
    }
};

void drawModSettings(UiFrame& f, Win const& w, float a) {
    auto* m = Registry::get().find(gModPage);
    if (!m) { gPage = Page::Mods; return; }
    float x = w.mainX + 2, y = w.bodyY + 2, wd = w.mainW - 8;
    pageHeader(f, x, y - 4, wd, m->name, m->planned ? m->description + " - on the roadmap." : m->description, a, m->planned ? -1 : (m->enabled ? 1 : 0),
               [m] { Registry::get().setEnabled(*m, !m->enabled); });
    Scroller sc(f, w, a);
    if (m->id == "lyricsflow") {
        if (sc.fits(50)) {
            row(f, x, sc.y, wd, 50, "Open the LyricsForge panel", "The glass settings panel in the world (also: L in game)", a);
            f.g.icon("music", x + wd - 30, sc.y + 25, 20, rgba(kWhite, 1), a);
            f.hits.push_back({x, sc.y, wd, 50, [](bool) { lyr::win::requestPanel(); closeMenu(); }});
        }
        sc.y += 60;
    }
    for (auto& s : m->settings) {
        Setting* sp = &s;
        if (s.kind == Setting::Kind::Toggle) {
            if (sc.fits(50)) toggleRow(f, x, sc.y, wd, s.label, "", s.on, a, [sp] { sp->on = !sp->on; Registry::get().save(); });
            sc.y += 60;
        } else {
            if (sc.fits(64)) slider(f, m->id + "/" + s.id, x, sc.y, wd, s.label, "", s.value, s.min, s.max, s.step, "", a, [] { Registry::get().save(); });
            sc.y += 74;
        }
    }
    if (m->settings.empty() && m->id != "lyricsflow")
        f.g.text(m->planned ? "Not built yet." : "This module has no settings.", x + 2, sc.y + 10, 13, rgba(kLilac, 0.62f), a);
    sc.end();
}

// Section label inside a page (small caps-style, lilac).
void sectionLabel(UiFrame& f, Scroller& sc, float x, std::string const& t, float a) {
    if (sc.fits(20)) f.g.text(t, x + 2, sc.y + 10, 11, rgba(kLilac, 0.62f), a, true);
    sc.y += 24;
}

// Shaders page: Loadouts first (one click sets shaders, graphics mode, modules, mods and DLL builds), then the plain
// shaderpack list. Right click a loadout = its details page.
void drawShaders(UiFrame& f, Win const& w, float a) {
    float x = w.mainX + 2, y = w.bodyY + 2, wd = w.mainW - 8;
    pageHeader(f, x, y - 4, wd, "Shaders", "Loadouts set everything at once (right click = details). Shaders apply after a restart.", a);
    Scroller sc(f, w, a);

    sectionLabel(f, sc, x, "LOADOUTS", a);
    auto const&       items  = loadouts::list();
    std::string const lo     = loadouts::active();
    for (auto const& it : items) {
        if (sc.fits(50)) {
            bool on = it.name == lo;
            row(f, x, sc.y, wd, 50, it.name, it.description, a, on);
            if (on) {
                f.g.text("ACTIVE", x + wd - 44, sc.y + 25, 11, rgba(kWhite, 0.85f), a, true, 2);
                f.g.icon("check", x + wd - 24, sc.y + 25, 18, rgba(kWhite, 1), a);
            }
            std::string n = it.name;
            f.hits.push_back({x, sc.y, wd, 50, [n](bool right) {
                if (right) { gLoadoutPage = n; showPage(Page::Loadout); return; }
                loadouts::apply(n);
                toast("Loading " + n + "...");
            }});
        }
        sc.y += 58;
    }
    if (sc.fits(50)) {
        row(f, x, sc.y, wd, 50, "Save current setup", "Saves shaders, graphics mode, modules, mods and DLL builds as a new loadout", a);
        f.g.icon("pin", x + wd - 24, sc.y + 25, 18, rgba(kWhite, 1), a);
        f.hits.push_back({x, sc.y, wd, 50, [](bool) {
            auto n = loadouts::saveCurrent("");
            if (!n.empty()) toast("Saved as " + n);
        }});
    }
    sc.y += 58;
    if (std::string st = loadouts::status(); sc.fits(20)) {
        bool restart = loadouts::restartNeeded();
        f.g.text(st, x + 2, sc.y + 8, 11.5f, rgba(restart ? kWhite : kLilac, restart ? 1.0f : 0.62f), a, restart, 0, restart ? 0.6f : 0);
    }
    sc.y += 30;

    sectionLabel(f, sc, x, "SHADERPACKS", a);
    auto packs = shaderpacks::listPacks();
    packs.insert(packs.begin(), "off");
    std::string const active = shaderpacks::activePack();
    for (auto const& p : packs) {
        if (p.rfind("Debug", 0) == 0) continue; // debug packs stay hidden
        if (sc.fits(46)) {
            bool on = p == active;
            float y2 = sc.y;
            pillGlass(f.g, x, y2, wd, 46, 12, on, a, f.over(x, y2, wd, 46) ? 1.0f : 0.0f);
            f.g.text(p == "off" ? "Off (vanilla)" : p, x + 14, y2 + 23, 13.5f, rgba(kWhite, 1), a, true, 0, on ? 0.6f : 0);
            if (on) {
                f.g.text("ACTIVE", x + wd - 44, y2 + 23, 11, rgba(kWhite, 0.85f), a, true, 2);
                f.g.icon("check", x + wd - 24, y2 + 23, 18, rgba(kWhite, 1), a);
            }
            f.hits.push_back({x, y2, wd, 46, [p](bool) {
                if (shaderpacks::setActivePack(p)) toast(p == "off" ? "Shaders off - restart the game to apply" : p + " selected - restart the game to apply");
            }});
        }
        sc.y += 54;
    }
    sc.end();
}

// One loadout: what it changes + Load / Overwrite rows.
void drawLoadout(UiFrame& f, Win const& w, float a) {
    loadouts::Info const* it = nullptr;
    for (auto const& i : loadouts::list())
        if (i.name == gLoadoutPage) it = &i;
    if (!it) { gPage = Page::Shaders; return; }
    float x = w.mainX + 2, y = w.bodyY + 2, wd = w.mainW - 8;
    pageHeader(f, x, y - 4, wd, it->name, it->description.empty() ? "Loadout" : it->description, a);
    Scroller sc(f, w, a);
    std::string const n = it->name;
    if (sc.fits(50)) {
        row(f, x, sc.y, wd, 50, "Load now", "Mods and DLL builds switch on the next launch", a, n == loadouts::active());
        f.g.icon("check", x + wd - 24, sc.y + 25, 18, rgba(kWhite, 1), a);
        f.hits.push_back({x, sc.y, wd, 50, [n](bool) { loadouts::apply(n); toast("Loading " + n + "..."); }});
    }
    sc.y += 58;
    if (sc.fits(50)) {
        row(f, x, sc.y, wd, 50, "Overwrite with current setup", "Replaces this loadout with how things are right now", a);
        f.g.icon("pencil", x + wd - 24, sc.y + 25, 18, rgba(kWhite, 1), a);
        f.hits.push_back({x, sc.y, wd, 50, [n](bool) { if (!loadouts::saveCurrent(n).empty()) toast("Updated " + n); }});
    }
    sc.y += 66;
    sectionLabel(f, sc, x, "WHAT IT SETS", a);
    auto line = [&](std::string const& t, std::string const& v) {
        if (sc.fits(46)) {
            row(f, x, sc.y, wd, 46, t, "", a);
            f.g.text(f.g.ellip(v, wd * 0.55f, 12.5f), x + wd - 14, sc.y + 23, 12.5f, rgba(kLilac, 0.85f), a, false, 2);
        }
        sc.y += 54;
    };
    line("Shaders", it->shaderpack.empty() ? "unchanged" : it->shaderpack == "off" ? "off (vanilla)" : it->shaderpack);
    line("Graphics", it->vibrant < 0 ? "unchanged" : it->vibrant ? "Vibrant Visuals" : "Fancy");
    line("Bedrock Forge modules", it->modules ? std::format("{} set", it->modules) : "unchanged");
    for (auto const& m : it->mods) line("Mod", m);
    for (auto const& d : it->dlls) line("DLL build", d);
    sc.end();
}

void drawClient(UiFrame& f, Win const& w, float a) {
    auto* m = uiModule();
    if (!m) return;
    float x = w.mainX + 2, y = w.bodyY + 2, wd = w.mainW - 8;
    pageHeader(f, x, y - 4, wd, "Client", "How the glass looks. Presets set blur, refraction and frost together.", a);
    Scroller sc(f, w, a);
    y = sc.y;
    // presets
    struct Pre { char const* n; float b, r, t; };
    constexpr Pre pres[4] = {{"Off", 0, 0, 0.3f}, {"Low", 0.5f, 0.5f, 0.7f}, {"Balanced", 1, 1, 1}, {"Max", 1.8f, 2, 1.3f}};
    float sw = (wd - 3 * 8) / 4;
    for (int i = 0; i < 4 && sc.fits(30); ++i) {
        float sx = x + float(i) * (sw + 8);
        bool  on = std::abs(m->num("blur", 1) - pres[i].b) < 0.01f && std::abs(m->num("refraction", 1) - pres[i].r) < 0.01f &&
                  std::abs(m->num("frost", 1) - pres[i].t) < 0.01f;
        pillGlass(f.g, sx, y, sw, 30, 14, on, a, f.over(sx, y, sw, 30) ? 1.0f : 0.0f);
        f.g.text(pres[i].n, sx + sw / 2, y + 15, 12.5f, rgba(kWhite, 1), a, true, 1, on ? 0.5f : 0);
        Pre pr = pres[i];
        f.hits.push_back({sx, y, sw, 30, [m, pr](bool) {
            if (auto* s = m->setting("blur")) s->value = pr.b;
            if (auto* s = m->setting("refraction")) s->value = pr.r;
            if (auto* s = m->setting("frost")) s->value = pr.t;
            Registry::get().save();
        }});
    }
    sc.y += 42;
    auto save = [] { Registry::get().save(); };
    struct Sl { char const* id; char const* t; char const* d; float mx; };
    for (Sl const sl : {Sl{"blur", "Background blur", "How strongly the glass frosts the world", 2}, Sl{"refraction", "Glass refraction", "How much the edges bend the world behind", 2.5f},
                        Sl{"frost", "Frost tint", "Dark violet haze on panels and buttons", 2}}) {
        if (auto* s = m->setting(sl.id); s && sc.fits(64)) slider(f, std::string("client_ui/") + sl.id, x, sc.y, wd, sl.t, sl.d, s->value, 0, sl.mx, 0.05f, "x", a, save);
        sc.y += 72;
    }
    struct Tg { char const* id; char const* t; char const* d; };
    for (Tg const tg : {Tg{"wheel", "Open with the wheel", "Right Shift shows the 4-button wheel first; off opens Mods directly"},
                        Tg{"worldblur", "Blur the world", "Frost the whole world behind the wheel and the window"},
                        Tg{"camswing", "Camera swing", "The camera swings round to face your player while the wheel is up"}}) {
        if (auto* s = m->setting(tg.id); s && sc.fits(50)) toggleRow(f, x, sc.y, wd, tg.t, tg.d, s->on, a, [s] { s->on = !s->on; Registry::get().save(); });
        sc.y += 58;
    }
    sc.end();
}

void drawLyrics(UiFrame& f, Win const& w, float a) {
    auto* m = Registry::get().find("lyricsflow");
    float x = w.mainX + 2, y = w.bodyY + 2, wd = w.mainW - 8;
    if (!m) { f.g.text("LyricsForge isn't part of this build.", x + 2, y + 12, 13, rgba(kLilac, 0.62f), a); return; }
    pageHeader(f, x, y - 4, wd, "LyricsForge", "Synced lyrics in the world - port of LyricsFlow by 374m (personal build)", a, m->enabled ? 1 : 0,
               [m] { Registry::get().setEnabled(*m, !m->enabled); });
    Scroller sc(f, w, a);
    bool shown = lyr::win::lyricsShown();
    if (sc.fits(50)) toggleRow(f, x, sc.y, wd, "Show lyrics", "Same as the O key in game", shown, a, [shown] { lyr::win::setLyricsShown(!shown); });
    sc.y += 58;
    if (sc.fits(50)) {
        float ry = sc.y;
        row(f, x, ry, wd, 50, "Open the LyricsForge panel", "Fonts, colours, sky, placement, FPS profiles (also: L in game)", a, false);
        f.g.icon("music", x + wd - 30, ry + 25, 20, rgba(kWhite, 1), a);
        f.hits.push_back({x, ry, wd, 50, [m](bool) {
            if (!m->enabled) Registry::get().setEnabled(*m, true);
            lyr::win::requestPanel();
            closeMenu();
        }});
    }
    sc.y += 58;
    if (sc.fits(50)) row(f, x, sc.y, wd, 50, "Keys", "L panel  |  O lyrics on / off  |  [ and ] nudge the timing of this song", a);
    sc.y += 58;
    if (!m->settings.empty()) {
        if (sc.fits(50)) {
            row(f, x, sc.y, wd, 50, "More LyricsForge settings", "Module options", a);
            f.hits.push_back({x, sc.y, wd, 50, [](bool) { gModPage = "lyricsflow"; showPage(Page::ModSettings); }});
        }
        sc.y += 58;
    }
    sc.end();
}

void drawWindow(UiFrame& f, float t) {
    auto& g = f.g;
    Win const w = winGeo(g);
    float const a = t;
    if (lookOn("worldblur", true)) g.backdrop(g.W / 2, g.H / 2, 0, 0, 0.45f * t, 0.10f, 1.0f);
    // drop shadow + window glass
    g.solid(w.x - 10, w.y + 4, w.w + 20, w.h + 20, 36, rgba({0, 0, 0}, 1), 0.22f * a, 36);
    g.glass(w.x, w.y, w.w, w.h, 26, kDark, 0.30f, 22, 16, 1.0f, 0.9f, 0.22f, a);

    // close
    float cx = w.x + w.w - 34, cy = w.y + 8;
    g.icon("close", cx + 12, cy + 12, 15, rgba(kWhite, f.over(cx, cy, 24, 24) ? 1.0f : 0.62f), a);
    f.hits.push_back({cx, cy, 24, 24, [](bool) { closeMenu(); }});

    // rail
    struct Rail { char const* icon; Page page; int tab; };
    constexpr Rail rail[4] = {{"home", Page::Mods, -1}, {"gear", Page::Client, -1}, {"sparkle", Page::Shaders, -1}, {"music", Page::Lyrics, -1}};
    float rx = w.x + 19, ry = w.y + 18;
    for (auto const& r : rail) {
        bool on = gPage == r.page || (r.page == Page::Mods && gPage == Page::ModSettings) || (r.page == Page::Shaders && gPage == Page::Loadout);
        pillGlass(g, rx, ry, 38, 38, 11, on, a, f.over(rx, ry, 38, 38) ? 1.0f : 0.0f);
        g.icon(r.icon, rx + 19, ry + 19, 18, rgba(kWhite, on ? 1.0f : 0.7f), a);
        Page p = r.page;
        f.hits.push_back({rx, ry, 38, 38, [p](bool) { showPage(p); }});
        ry += 48;
    }
    float hy = w.y + w.h - 16 - 38;
    pillGlass(g, rx, hy, 38, 38, 11, false, a, f.over(rx, hy, 38, 38) ? 1.0f : 0.0f);
    g.icon("pencil", rx + 19, hy + 19, 18, rgba(kWhite, 0.7f), a);
    f.hits.push_back({rx, hy, 38, 38, [](bool) { gTab = 2; showPage(Page::Mods); }});

    // top row: back, tabs, search
    float ty = w.y + 18;
    float bx = w.mainX;
    pillGlass(g, bx, ty, 34, 30, 15, false, a, f.over(bx, ty, 34, 30) ? 1.0f : 0.0f);
    g.icon("back", bx + 17, ty + 15, 17, rgba(kWhite, 0.85f), a);
    f.hits.push_back({bx, ty, 34, 30, [](bool) {
        if (gPage == Page::Loadout) { gPage = Page::Shaders; resetScroll(); }
        else if (gPage != Page::Mods) { gPage = Page::Mods; resetScroll(); }
        else if (lookOn("wheel", true)) setView(View::Wheel);
        else closeMenu();
    }});
    float const searchW = std::min(170.0f, w.mainW * 0.22f);
    float const sx = w.x + w.w - 18 - 22 - searchW;
    float tx = bx + 42;
    bool const tabsLive = gPage == Page::Mods && gSearch.empty();
    for (int i = 0; i < 6; ++i) {
        float tw = 13 + 15 + 7 + g.textW(kTabLabel[i], 12, true) + 13;
        if (tx + tw > sx - 8) break; // narrow screens: the tabs that fit
        bool on = tabsLive && gTab == i;
        pillGlass(g, tx, ty, tw, 30, 15, on, a, f.over(tx, ty, tw, 30) ? 1.0f : 0.0f);
        g.icon(kTabIcon[i], tx + 13 + 7.5f, ty + 15, 15, rgba(kWhite, 1), a);
        g.text(kTabLabel[i], tx + 35, ty + 15, 12, rgba(kWhite, 1), a, true, 0, on ? 0.6f : 0.25f);
        f.hits.push_back({tx, ty, tw, 30, [i](bool) { gTab = i; gSearch.clear(); showPage(Page::Mods); }});
        tx += tw + 8;
    }
    g.glass(sx, ty, searchW, 30, 15, kDark, 0.40f, 9, 5, 0.85f, 0.6f, 0.1f, a);
    g.icon("search", sx + 16, ty + 15, 14, rgba(kWhite, 0.62f), a);
    g.text(gSearch.empty() ? "type to search..." : gSearch + "_", sx + 30, ty + 15, 13, rgba(gSearch.empty() ? kLilac : kWhite, gSearch.empty() ? 0.62f : 1), a);

    // body
    if (!gSearch.empty() && gPage != Page::Mods) gPage = Page::Mods;
    switch (gPage) {
    case Page::Mods: drawMods(f, w, a); break;
    case Page::ModSettings: drawModSettings(f, w, a); break;
    case Page::Shaders: drawShaders(f, w, a); break;
    case Page::Loadout: drawLoadout(f, w, a); break;
    case Page::Client: drawClient(f, w, a); break;
    case Page::Lyrics: drawLyrics(f, w, a); break;
    }
}

void drawToast(UiFrame& f) {
    if (gToast.empty()) return;
    int64_t age = nowMs() - gToastMs;
    if (age > 2600) { gToast.clear(); return; }
    float a  = std::min(1.0f, std::min(float(age) / 150.0f, float(2600 - age) / 300.0f));
    auto& g  = f.g;
    float tw = g.textW(gToast, 13) + 32, x = (g.W - tw) / 2, y = 22;
    g.glass(x, y, tw, 38, 14, kDark, 0.45f, 9, 5, 1, 0.8f, 0.2f, a);
    g.text(gToast, g.W / 2, y + 19, 13, rgba(kWhite, 1), a, false, 1, 0.3f);
}

// ---------------------------------------------------------------- one frame
void render(ScreenView& view) {
    UiFrame f;
    auto& g = f.g;
    float const Wg = view.mSize->x, Hg = view.mSize->y;
    if (Hg < 1 || Wg < 1) return;
    float const toD = kH / Hg; // GUI units -> design units
    g.W = Wg * toD;
    g.H = kH;
    float inv = gClient ? gClient->getGuiData()->mInvGuiScale : 0.5f;
    g.px      = (Hg / std::max(inv, 1e-3f)) / kH; // real px per design unit
    g.blurK   = look("blur", 1);
    g.refrK   = look("refraction", 1);
    g.tintK   = look("frost", 1);
    g.a       = lyr::win::clientUiAssets();
    glm::vec2 const ptr = *view.mPointerLocationPrevious;
    f.mx = ptr.x * toD;
    f.my = ptr.y * toD;

    int64_t const now = nowMs();
    float const   t   = ease(float(now - gViewAtMs) / 240.0f);
    { // smooth scroll: ease the drawn position towards the target (~90 ms time constant, frame-rate independent)
        float dt = gScrollMs ? std::clamp(float(now - gScrollMs) / 1000.0f, 0.0f, 0.1f) : 0.0f;
        gScrollMs = now;
        gScrollAt += (gScroll - gScrollAt) * (1.0f - std::exp(-dt / 0.09f));
        if (std::abs(gScroll - gScrollAt) < 0.25f) gScrollAt = gScroll;
    }
    // mouse wheel since the last frame: moves the target before the page draws (and clamps it), so the eased list
    // never chases a target past the ends
    for (int d : gWheel) gScroll += float(d) * 52.0f;
    gWheel.clear();
    if (gView == View::Wheel) drawWheel(f, t);
    else drawWindow(f, t);
    drawToast(f);

    // input collected since the last frame (GUI units -> design units)
    auto clicks = std::move(gClicks);
    gClicks.clear();
    for (auto const& c : clicks) {
        float cx = c.x * toD, cy = c.y * toD;
        if (gView == View::Wheel) {
            int p = petalAt(g, cx, cy);
            if (p >= 0) openPetal(p);
            else {
                float wx, wy, r1, r2;
                petalGeo(g, wx, wy, r1, r2);
                if (std::hypot(cx - wx, cy - wy) > r2 + 30) closeMenu();
            }
            continue;
        }
        for (auto it = f.hits.rbegin(); it != f.hits.rend(); ++it)
            if (inside(cx, cy, it->x, it->y, it->w, it->h)) {
                if (c.middle) { // middle click on a module row = pin / unpin (Quick Access)
                    if (!it->mod.empty())
                        if (auto* m = Registry::get().find(it->mod)) { m->favorite = !m->favorite; Registry::get().save(); }
                    break;
                }
                gActionX = cx;
                if (!it->drag.empty()) gDrag = it->drag; // keeps following the pointer while the button is held
                if (it->action) it->action(c.right);
                break;
            }
        if (!gScene) break; // an action closed the menu
    }
    if (!gScene) return;
    // camera swing request for RF (after the clicks: a petal click that opened the window ends it this frame). 800 ms
    // so a loading hitch doesn't make it flicker; closeMenu() zeroes it at once
    gCamUntilMs = gView == View::Wheel && lookOn("camswing", true) ? now + 800 : 0;
    // publish: shapes first, then icons + text on top
    std::vector<BfGlyphV1> out = std::move(g.shapes);
    out.insert(out.end(), g.top.begin(), g.top.end());
    lyr::FrameStore::get().setOverlay(std::move(out));
}
} // namespace

int64_t camUntilMs() { return gCamUntilMs.load(); }

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
    gOpenedAt   = Clock::now();
    gDialogSeen = {};
    ++diag::menuOpens;
    gClicks.clear();
    gWheel.clear();
    gHeld = false;
    gDrag.clear();
    setView(lookOn("wheel", true) ? View::Wheel : View::Window);
    if (gView == View::Window) gPage = Page::Mods;
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
        auto const now = Clock::now();
        if (bf::hud::isHudScreen(ev.screenView())) {
            // only the HUD is rendering: once the dialog has been gone a while (Esc / OK), forget the menu
            if (now - gOpenedAt > std::chrono::milliseconds(800) && now - gDialogSeen > std::chrono::milliseconds(400)) {
                gScene.reset();
                gSearch.clear();
                lyr::FrameStore::get().setOverlay({});
            }
            return;
        }
        if (auto const& tree = ev.screenView().mVisualTree; tree) diag::noteScreen(*tree->mRootControlName);
        gDialogSeen = now;
        ev.cancel(); // the native dialog draws nothing: Renderer Foundation draws the glass menu at Present
        try { render(ev.screenView()); } catch (...) {}
    });

    gSubs.on<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
        std::lock_guard lock(gMutex);
        if (!gScene || !ownsTop()) return;
        int const k = ev.keyCode();
        if (k == 0x1B) {                                   // Esc: clear the search, leave a sub page, then close
            ev.cancel();
            if (!ev.isDown()) return;
            if (!gSearch.empty()) gSearch.clear();
            else if (gView == View::Window && gPage == Page::ModSettings) { gPage = Page::Mods; resetScroll(); }
            else if (gView == View::Window && gPage == Page::Loadout) { gPage = Page::Shaders; resetScroll(); }
            else closeMenu();
            return;
        }
        if (k == 0xA1 || k == 0xA0 || k == 0x10) return;  // shifts
        ev.cancel();                                       // the menu owns the keyboard while open
        if (!ev.isDown()) return;
        if (gView == View::Wheel) return;                  // typing only in the window
        if (k == 0x08) { if (!gSearch.empty()) gSearch.pop_back(); resetScroll(); return; }
        if (gSearch.size() >= 20) return;
        if ((k >= 0x41 && k <= 0x5A) || (k >= 0x30 && k <= 0x39)) { gSearch += char(std::tolower(k)); resetScroll(); gPage = Page::Mods; }
        else if (k == 0x20 && !gSearch.empty()) gSearch += ' ';
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
        if (a == MouseAction::ActionLeft) gHeld = d == MouseAction::DataDown;
        if (a == MouseAction::ActionLeft && d != MouseAction::DataDown) gDrag.clear(); // released
        if ((a == MouseAction::ActionLeft || a == MouseAction::ActionRight || a == MouseAction::ActionMiddle) && d == MouseAction::DataDown)
            gClicks.push_back({float(ev.x()) * inv, float(ev.y()) * inv, a == MouseAction::ActionRight, a == MouseAction::ActionMiddle});
        if (d == MouseAction::DataDown) ev.cancel();
    });
}

void uninstall() {
    gSubs.clear();
    std::lock_guard lock(gMutex);
    gScene.reset();
    lyr::FrameStore::get().setOverlay({});
}

} // namespace bedrock_forge::client::menu

// RF (SpatialCamera.cpp) polls this: 1 while the wheel is up and "Camera swing" is on, so the camera swings round in
// front of the player (third person) like the Blueberry wheel. Lock-free: called from RF's client tick.
extern "C" __declspec(dllexport) int BF_ClientMenuCamera() {
    using namespace std::chrono;
    int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    return now < bedrock_forge::client::menu::camUntilMs() ? 1 : 0;
}
