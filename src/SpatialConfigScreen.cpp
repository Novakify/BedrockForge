// Renderer Foundation - Spatial GUI: the in-game config screen (Java: SpatialGUIConfig's YACL screen, opened from Mod
// Menu). Same tabs, search box, a value and a Reset button per option, Cancel / Save & Quit. Opened from a button on
// the pause screen (Bedrock has no mod menu) or the F6 menu. Original code for Renderer Foundation.
#include "SpatialConfigScreen.h"
#include "SpatialCamera.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

namespace rf::sgconfig {
namespace {
using spatialcam::Config;

enum class T { Bool, Int, Float, Ease };
struct Field {
    int         tab;
    const char* label;
    const char* tip;
    T           type;
    bool Config::*b  = nullptr;
    int Config::*  i = nullptr;
    float Config::*f = nullptr;
    float       lo = 0, hi = 1, step = 0.05f;
};
const char* kTabs[] = {"General", "Screens", "Rendering", "TP Screen", "FP Screen", "TP Camera", "FP Camera", "Animations", "Auto FOV"};
constexpr int kTabN = int(sizeof kTabs / sizeof kTabs[0]);

Field B(int tab, const char* l, const char* tip, bool Config::*m) { Field x{tab, l, tip, T::Bool}; x.b = m; return x; }
Field I(int tab, const char* l, const char* tip, int Config::*m, float lo, float hi, float st = 1) {
    Field x{tab, l, tip, T::Int}; x.i = m; x.lo = lo; x.hi = hi; x.step = st; return x;
}
Field F(int tab, const char* l, const char* tip, float Config::*m, float lo, float hi, float st = 0.05f) {
    Field x{tab, l, tip, T::Float}; x.f = m; x.lo = lo; x.hi = hi; x.step = st; return x;
}

std::vector<Field> const& fields() {
    static std::vector<Field> v = {
        B(0, "Enabled", "Show container screens as a panel in the world", &Config::enabled),
        B(0, "First Person Mode (Inventory)", "The inventory floats in front of you instead of beside you", &Config::fpInventory),
        B(0, "First Person Mode (Other)", "Other containers float in front of you", &Config::fpContainers),
        B(0, "Hide Hotbar", "Hide the hotbar and hearts while a panel is open", &Config::hideHotbar),
        B(0, "Mirror Third Person", "The panel on the left, the camera over the other shoulder", &Config::mirrorThirdPerson),
        B(0, "Auto Calculate GUI Scale", "Java's automatic GUI scale for the panel", &Config::autoGuiScale),
        I(0, "GUI Scale", "Panel GUI scale when not automatic", &Config::guiScale, 1, 6),
        B(0, "Use Crosshair for First Person", "First person: the mouse turns the view, a crosshair picks slots", &Config::crosshairFp),
        B(0, "Auto Detect Camera Mode", "First person panels while the game is in first person, third person otherwise", &Config::autoDetectCameraMode),
        B(0, "Fallback to First Person on Block Collision", "Off: the camera moves in front of walls instead", &Config::fallbackFpOnBlock),
        B(1, "Pause Screen", "", &Config::pauseScreen), B(1, "All Screens", "", &Config::allScreens),
        B(1, "Inventory", "", &Config::inventory), B(1, "Creative Inventory", "", &Config::creativeInventory),
        B(1, "Chests", "", &Config::chests), B(1, "Shulker Boxes", "", &Config::shulkerBoxes),
        B(1, "Most Containers", "", &Config::mostContainers), B(1, "Crafting", "", &Config::crafting),
        B(1, "Furnaces", "", &Config::furnaces), B(1, "Anvils", "", &Config::anvils), B(1, "Enchanting", "", &Config::enchanting),
        B(1, "Beacons", "", &Config::beacons), B(1, "Brewing", "", &Config::brewing), B(1, "Villager Trading", "", &Config::villagerTrading),
        B(1, "Hoppers", "", &Config::hoppers), B(1, "Dispensers", "", &Config::dispensers), B(1, "Grindstone", "", &Config::grindstone),
        B(1, "Smithing", "", &Config::smithing), B(1, "Cartography", "", &Config::cartography), B(1, "Loom", "", &Config::loom),
        B(1, "Stonecutter", "", &Config::stonecutter), B(1, "Books", "", &Config::books), B(1, "Lectern", "", &Config::lectern),
        I(2, "Screen Alpha", "Panel opacity", &Config::screenAlpha, 0, 255),
        B(2, "Blur World Background", "Blur the world behind the panel", &Config::worldBlur),
        I(2, "World Blur Strength", "", &Config::worldBlurStrength, 1, 10),
        B(2, "Keep the Player Sharp", "Draw the player unblurred (Java 2.x); the clips blur it too", &Config::blurKeepPlayer),
        B(2, "Drop Shadow", "A soft shadow behind the panel", &Config::dropShadow),
        I(2, "Drop Shadow Alpha", "", &Config::dropShadowAlpha, 0, 255),
        B(2, "Override FOV", "Ease to the target FOV while a panel is open (Java framing)", &Config::overrideFov),
        I(2, "Target FOV", "", &Config::targetFov, 30, 110),
        F(2, "Recipe Book Shrink Factor", "", &Config::recipeBookShrink, 1, 3),
        B(2, "Hide Hands in First Person", "", &Config::hideHandsFp), B(2, "Hide Shield in First Person", "", &Config::hideShieldFp),
        B(2, "Swing Arm on First Person Click", "", &Config::swingArmFp),
        B(2, "Use Linear Filtering", "Off: crisp, pixel-sharp GUI on the panel", &Config::linearFiltering),
        B(2, "World Lighting Tint", "The panel darkens in dark places", &Config::lightTint),
        F(2, "Lighting Tint Strength", "", &Config::lightTintStrength, 0, 120, 1),
        F(3, "Screen Distance", "How far in front of the camera", &Config::screenDistance, 0.1f, 10),
        F(3, "Screen Side Offset", "Horizontal offset of the screen", &Config::screenSide, -5, 5),
        F(3, "Screen Height Offset", "", &Config::screenHeight, -3, 3),
        F(3, "Screen Yaw Offset", "", &Config::screenYaw, -360, 360, 1),
        F(3, "Screen Pitch Offset", "", &Config::screenPitch, -90, 90, 1),
        F(3, "Screen Scale", "", &Config::screenScale, 0.5f, 10),
        F(4, "Screen Distance", "", &Config::fpDistance, 0.1f, 10), F(4, "Screen Side Offset", "", &Config::fpSide, -3, 3),
        F(4, "Screen Height Offset", "", &Config::fpHeight, -3, 3), F(4, "Screen Yaw Offset", "", &Config::fpYaw, -360, 360, 1),
        F(4, "Screen Pitch Offset", "", &Config::fpPitch, -90, 90, 1), F(4, "Screen Scale", "", &Config::fpScale, 0.5f, 10),
        B(4, "Curved Screen", "", &Config::curvedScreen), I(4, "Curved Screen Arc", "", &Config::curvedArc, 5, 150),
        F(5, "Distance", "Camera distance behind the player", &Config::camDistance, 0.5f, 6),
        F(5, "Horizontal Offset", "Camera sideways (negative = over the right shoulder)", &Config::camSide, -4, 4),
        F(5, "Vertical Offset", "Camera height over the feet", &Config::camHeight, -1, 4),
        F(5, "Target Pitch", "", &Config::camPitch, -90, 90, 1),
        I(5, "Transition Duration (ms)", "", &Config::transitionMs, 1, 5000, 10),
        I(5, "Transition Skip Percentage", "", &Config::transitionSkipPct, 0, 100),
        F(5, "Head Base Yaw Offset", "Avatar head turn toward the panel", &Config::avatarBaseYaw, -180, 180, 1),
        F(5, "Avatar Body Rotation Offset", "", &Config::avatarBodyRot, -180, 180, 1),
        I(5, "Avatar Max Yaw Offset", "How far the head follows the pointer sideways", &Config::avatarMaxYaw, 0, 90),
        I(5, "Avatar Max Pitch Offset", "", &Config::avatarMaxPitch, 0, 90),
        B(5, "Mirror Head Movement", "", &Config::mirrorHeadMovement),
        F(5, "Elytra Flying Side Offset", "", &Config::elytraSide, -2, 2),
        F(5, "Elytra Flying Height Offset", "", &Config::elytraHeight, -2, 2),
        F(5, "Mouse Sensitivity Yaw", "Camera parallax with the pointer", &Config::tpSensYaw, 0.01f, 1, 0.01f),
        F(5, "Mouse Sensitivity Pitch", "", &Config::tpSensPitch, 0.01f, 1, 0.01f),
        B(5, "Disable Parallax", "", &Config::tpParallaxOff), B(5, "Lerp Camera Pitch", "", &Config::lerpXRot),
        F(6, "Mouse Sensitivity Yaw", "", &Config::fpSensYaw, 0.01f, 1, 0.01f),
        F(6, "Mouse Sensitivity Pitch", "", &Config::fpSensPitch, 0.01f, 1, 0.01f),
        I(6, "Pitch Clamp", "", &Config::fpPitchClamp, 0, 90), B(6, "Disable Parallax", "", &Config::fpParallaxOff),
        B(6, "Keep First Person Camera Angle", "", &Config::keepFpAngle),
        B(6, "Lock First Person Yaw", "Crosshair free look stays within 90 degrees", &Config::lockFpYaw),
        B(7, "Enable Close Animation", "", &Config::closeAnim), B(7, "Enable Fade Animation", "", &Config::fade),
        I(7, "Fade Duration (ms)", "", &Config::fadeMs, 0, 1000, 10), B(7, "Enable Scale Animation", "", &Config::scaleAnim),
        I(7, "Scale Duration (ms)", "", &Config::openMs, 50, 1000, 10), Field{7, "Scale Easing", "", T::Ease},
        I(7, "Start Scale (%)", "", &Config::startScalePct, 1, 100),
        B(8, "Auto Scale by FOV", "", &Config::autoScaleByFov), F(8, "Baseline FOV", "", &Config::baselineFov, 30, 110, 1),
        F(8, "Screen Power", "", &Config::screenPower, 0.1f, 3), F(8, "Side Offset Multiplier", "", &Config::sideMult, -2, 2),
        F(8, "Distance Multiplier", "", &Config::distMult, 0.1f, 3), F(8, "Third Person Screen Multiplier", "", &Config::tpScreenMult, 0.1f, 2, 0.01f),
    };
    return v;
}

std::atomic_bool gOpen{false}, gPause{false};
Config           gSnapshot;
int              gTab = 0;
char             gSearch[64] = "";

bool lower_has(const char* hay, const char* needle) {
    if (!*needle) return true;
    std::string h(hay), n(needle);
    for (auto& c : h) c = char(std::tolower((unsigned char)c));
    for (auto& c : n) c = char(std::tolower((unsigned char)c));
    return h.find(n) != std::string::npos;
}

const char* kEase[] = {"Back", "Cubic", "Elastic", "Bounce", "Expo", "Quad", "Quart"};

// Minecraft-like colours
const ImU32 kDim = IM_COL32(8, 8, 12, 215), kPanel = IM_COL32(0, 0, 0, 110), kText = IM_COL32(224, 224, 224, 255),
            kTextDim = IM_COL32(160, 160, 160, 255), kYes = IM_COL32(85, 255, 85, 255), kNo = IM_COL32(255, 85, 85, 255);

void styleBegin(float s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.0f, s * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * s, 3 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * s, 2 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 4 * s);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 200));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(20, 20, 20, 220));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(30, 30, 30, 230));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(160, 160, 160, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(111, 111, 111, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(126, 136, 191, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 100, 160, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(198, 198, 198, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 80));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(128, 128, 128, 255));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
}
void styleEnd() {
    ImGui::PopStyleColor(14);
    ImGui::PopStyleVar(7);
}

// a Minecraft button: grey with a dark outline and a light top edge
bool mcButton(const char* label, ImVec2 size, bool active = false) {
    ImVec2 p  = ImGui::GetCursorScreenPos();
    bool   hit = ImGui::InvisibleButton(label, size);
    bool   hov = ImGui::IsItemHovered();
    auto*  dl  = ImGui::GetWindowDrawList();
    ImU32  fill = active ? IM_COL32(60, 60, 60, 255) : hov ? IM_COL32(126, 136, 191, 255) : IM_COL32(111, 111, 111, 255);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(0, 0, 0, 255));
    dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + size.x - 1, p.y + size.y - 1), fill);
    if (!active) dl->AddLine(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + size.x - 1, p.y + 1), IM_COL32(170, 170, 170, 255));
    if (hov && !active) dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(255, 255, 255, 255));
    const char* end = nullptr;
    ImVec2      ts  = ImGui::CalcTextSize(label);
    ImVec2      tp(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f);
    dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(63, 63, 63, 255), label, end); // Minecraft text shadow
    ImU32 tc = ImGui::GetColorU32(ImGuiCol_Text);
    if (tc == kText) tc = IM_COL32(255, 255, 255, 255); // plain buttons: white text; Yes / No keep their colour
    dl->AddText(tp, active ? IM_COL32(255, 255, 160, 255) : tc, label, end);
    return hit;
}

void shadowText(ImVec2 p, ImU32 c, const char* t) {
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(40, 40, 40, 255), t);
    dl->AddText(p, c, t);
}
} // namespace

void open() {
    if (gOpen.load()) return;
    gSnapshot = spatialcam::cfg();
    gOpen     = true;
}
bool isOpen() { return gOpen.load(); }
void notePause(bool pauseOnTop) {
    gPause = pauseOnTop;
    if (!pauseOnTop && gOpen.load()) { spatialcam::cfg() = gSnapshot; gOpen = false; } // the pause screen went away: cancel
}
bool wantsInput() { return gOpen.load() || gPause.load(); }
bool blocksGameMouse() { return gOpen.load() || (gPause.load() && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse); }

void draw() {
    ImGuiIO& io = ImGui::GetIO();
    float    W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 10 || H < 10) return;
    float s = std::max(1.0f, std::floor(H / 360.0f)); // integer scale like Minecraft's GUI scale
    styleBegin(s);
    if (!gOpen.load()) {
        if (gPause.load()) { // pause screen: the way in (Java: Mod Menu)
            ImGui::SetNextWindowPos(ImVec2(8 * s, H - 30 * s));
            ImGui::SetNextWindowSize(ImVec2(130 * s, 26 * s));
            ImGui::Begin("##sgcfgbtn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                                      ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::SetWindowFontScale(s * 0.5f);
            if (mcButton("Spatial GUI Settings", ImVec2(120 * s, 18 * s))) open();
            ImGui::End();
        }
        styleEnd();
        return;
    }
    // full screen: dim the game (pause screen underneath), title, tabs, search, rows, Cancel / Save & Quit
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##sgcfg", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SetWindowFontScale(s * 0.5f);
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), kDim);
    const char* title = "Spatial GUI Config";
    ImVec2      ts    = ImGui::CalcTextSize(title);
    shadowText(ImVec2((W - ts.x) * 0.5f, 6 * s), kText, title);

    float margin = 10 * s, top = 18 * s;
    // tabs (Java: the YACL category tabs with scroll arrows)
    ImGui::SetCursorScreenPos(ImVec2(margin, top));
    float tabW = (W - 2 * margin - (kTabN - 1) * 2 * s) / kTabN;
    for (int t = 0; t < kTabN; ++t) {
        if (t) ImGui::SameLine(0, 2 * s);
        if (mcButton(kTabs[t], ImVec2(tabW, 16 * s), t == gTab)) gTab = t;
    }
    // search
    ImGui::SetCursorScreenPos(ImVec2(margin, top + 22 * s));
    ImGui::SetNextItemWidth(W - 2 * margin);
    ImGui::InputTextWithHint("##search", "Search...", gSearch, sizeof gSearch);
    bool searching = gSearch[0] != 0;

    // option rows
    float listTop = top + 44 * s, listBottom = H - 34 * s;
    ImGui::SetCursorScreenPos(ImVec2(margin, listTop));
    ImGui::BeginChild("##rows", ImVec2(W - 2 * margin, listBottom - listTop), ImGuiChildFlags_None); // font scale inherited
    Config&       c   = spatialcam::cfg();
    Config const  def{};
    float         rowH = 20 * s, valueW = 90 * s, resetW = 70 * s, rowW = ImGui::GetContentRegionAvail().x;
    int           shownTab = -1;
    for (auto const& f : fields()) {
        if (searching ? !lower_has(f.label, gSearch) : f.tab != gTab) continue;
        if (searching && f.tab != shownTab) { // results grouped under their tab names
            shownTab = f.tab;
            ImGui::TextColored(ImVec4(1, 1, 0.5f, 1), "%s", kTabs[f.tab]);
        }
        ImGui::PushID(&f);
        ImVec2 p = ImGui::GetCursorScreenPos();
        // the row: hover highlight only (no item, so the value and Reset buttons over it get the clicks)
        bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + rowW, p.y + rowH));
        auto* cdl = ImGui::GetWindowDrawList();
        if (hov) cdl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH), IM_COL32(255, 255, 255, 18));
        ImVec2 ls = ImGui::CalcTextSize(f.label);
        shadowText(ImVec2(p.x + 6 * s, p.y + (rowH - ls.y) * 0.5f), kText, f.label);
        if (hov && f.tip && *f.tip) ImGui::SetTooltip("%s", f.tip);
        // value
        float vx = p.x + rowW - valueW - resetW - 6 * s, vy = p.y + 2 * s;
        ImGui::SetCursorScreenPos(ImVec2(vx, vy));
        bool isDefault = true;
        switch (f.type) {
        case T::Bool: {
            bool& v = c.*f.b;
            isDefault = v == def.*f.b;
            ImGui::PushStyleColor(ImGuiCol_Text, v ? kYes : kNo);
            if (mcButton(v ? "Yes" : "No", ImVec2(valueW, rowH - 4 * s))) v = !v;
            ImGui::PopStyleColor();
            break;
        }
        case T::Int: {
            int& v = c.*f.i;
            isDefault = v == def.*f.i;
            ImGui::SetNextItemWidth(valueW);
            ImGui::DragInt("##v", &v, std::max(0.2f, f.step * 0.25f), int(f.lo), int(f.hi), "%d", ImGuiSliderFlags_AlwaysClamp);
            break;
        }
        case T::Float: {
            float& v = c.*f.f;
            isDefault = std::fabs(v - def.*f.f) < 1e-4f;
            ImGui::SetNextItemWidth(valueW);
            ImGui::DragFloat("##v", &v, f.step * 0.25f, f.lo, f.hi, f.step >= 1 ? "%.0f" : f.step >= 0.05f ? "%.2f" : "%.3f", ImGuiSliderFlags_AlwaysClamp);
            break;
        }
        case T::Ease: {
            int e = int(c.easing);
            isDefault = c.easing == def.easing;
            if (mcButton(kEase[std::clamp(e, 0, 6)], ImVec2(valueW, rowH - 4 * s))) c.easing = spatialcam::Easing((e + 1) % 7);
            break;
        }
        }
        // Reset (Java: "Reset" next to each option)
        ImGui::SetCursorScreenPos(ImVec2(vx + valueW + 4 * s, vy));
        ImGui::BeginDisabled(isDefault);
        if (mcButton("Reset", ImVec2(resetW, rowH - 4 * s))) {
            switch (f.type) {
            case T::Bool: c.*f.b = def.*f.b; break;
            case T::Int: c.*f.i = def.*f.i; break;
            case T::Float: c.*f.f = def.*f.f; break;
            case T::Ease: c.easing = def.easing; break;
            }
        }
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
        ImGui::Dummy(ImVec2(rowW, rowH)); // reserve the row (scrolling)
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + 1 * s));
        ImGui::PopID();
    }
    ImGui::EndChild();

    // bottom buttons
    float bw = std::min(150 * s, (W - 3 * margin) * 0.5f), by = H - 26 * s;
    ImGui::SetCursorScreenPos(ImVec2(W * 0.5f - bw - 2 * s, by));
    bool cancel = mcButton("Cancel", ImVec2(bw, 20 * s)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::SetCursorScreenPos(ImVec2(W * 0.5f + 2 * s, by));
    bool save = mcButton("Save & Quit", ImVec2(bw, 20 * s));
    ImGui::End();
    styleEnd();
    if (save) { spatialcam::saveConfig(); gOpen = false; }
    else if (cancel) { c = gSnapshot; gOpen = false; }
}

} // namespace rf::sgconfig
