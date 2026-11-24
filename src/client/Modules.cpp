// Bedrock Forge client - built-in modules + the runtime that drives them.
// Feature set modelled on what Blueberry-class PvP clients offer (docs/REFERENCE_CLIENT_ANALYSIS.md),
// implemented from scratch on LeviLamina. Display/quality-of-life only: no module gives a gameplay advantage
// that vanilla doesn't (no reach/aim/velocity changes).

#include "core/ImpactFrames.h"
#include "client/Module.h"
#include "client/Diag.h"
#include "client/AsyncFile.h"
#include <sstream>
#include <filesystem>
#include <fstream>
#include "ll/api/mod/NativeMod.h"

#include <algorithm>
#include <array>
#include <atomic>
#include "client/CostProbe.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HudCursorRenderer.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/options/option_types/BoolOption.h"
#include "mc/options/option_types/OptionID.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/SurvivalMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"
#include "mc/world/level/storage/LevelData.h"

#include "bf/Events.h"
#include "bf/Hud.h"
#include "client/Menu.h"
#include "client/HudEdit.h"
#include "client/ForgeRender.h"
#include "client/ShaderPacks.h"
#include "client/PostFx.h"
#include "client/ServerAuto.h"
#include "client/MatchFx.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "client/Cosmetics.h"
#include "core/Win32Input.h"
#include "lyrics/win/Win.h"
namespace bf::lyrics::win { float cinemaFovScale(); }

// Bedrock Forge -> Renderer Foundation: little world events for the hit / jump effects (Jump Circles, Hit Splash,
// Ripple, Shockwave). A ring of the last 32; Renderer Foundation keeps the ones still young (steady_clock ms - one
// process, one clock). Written on the client thread; atMs is written last.
extern "C" {
struct BfFxEventsV1 {
    uint32_t size, version, count, frame;
    struct E { float x, y, z; uint32_t kind; int64_t atMs; float a, b; } e[32];   // kind: 1 jump, 2 hit, 3 crit, 4 landing (a = fall speed)
    // version 2: you (Trail / Aura / Halo / Wings / Cape). Since version 4 these are where the game DRAWS you this frame
    // (between the last two ticks, like its own interpolation) and pyaw is your BODY's yaw - not the 20 Hz tick
    // position and head yaw, which made the wings and cape shake when running, jumping and turning (2026-11-17)
    float    px, py, pz, pyaw;   // your feet (world), yaw (degrees)
    uint32_t pflags;             // 1 = known, 2 = on the ground
    uint32_t pad2;
    int64_t  pAtMs;
    // version 3: the block you aim at (Block Outline)
    int32_t  bx, by, bz;         // block position (world)
    uint32_t bface;              // 0..5 the face you look at (Bedrock facing), 255 = no block
    int64_t  bAtMs;
    // version 4: how fast you move (blocks / s, world) - RF moves px..pz on by the few ms between this write and its pass
    float    pvx, pvy, pvz, pyawRate;   // pyawRate: degrees / s of the body
};
__declspec(dllexport) BfFxEventsV1 const* BF_GetFxEvents();
}
namespace { BfFxEventsV1 gFxEv{sizeof(BfFxEventsV1), 4, 0, 0, {}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0}; }
extern "C" __declspec(dllexport) BfFxEventsV1 const* BF_GetFxEvents() { return &gFxEv; }
namespace bedrock_forge::client {
void pushFxEvent(uint32_t kind, float x, float y, float z, float a = 0.f) {
    auto& e = gFxEv.e[gFxEv.frame % 32];
    e.atMs = 0;
    e.x = x; e.y = y; e.z = z; e.kind = kind; e.a = a;
    e.atMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ++gFxEv.frame;
    gFxEv.count = std::min<uint32_t>(gFxEv.frame, 32);
}
} // namespace bedrock_forge::client // LyricsForge Director "FOV breath" (1 = off)

namespace bedrock_forge::client {

void installVisualHooks();
void setHandHook(bool on); // VisualModules.cpp (Hand Movement)
extern std::atomic<int> gSkyWorldPick;   // Runtime_win.cpp (Sky module Shuffle: Cloud Sea / Novak Glass turn)
namespace keysounds { void press(int vk); } // KeySounds_win.cpp (Creamy Keys)
namespace weathersounds { void rain(float level); void thunder(float volume, float closeK); } // WeatherSounds_win.cpp
extern std::atomic<float> gTimeOfDay; // VisualModules.cpp
extern std::mutex  gNickMutex;          // ChatCompact.cpp (Nick Hider)
extern std::string gNickReal;
std::string hideNick(std::string s);
void installChatHooks();
void uninstallChatHooks();
void uninstallVisualHooks();
void noteRenderThread();
void fullbrightTick(IClientInstance& client);

namespace {
using Clock = std::chrono::steady_clock;
using bf::hud::Rgba;
namespace w32 = bedrock_forge::win32;

int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

bf::events::Subscriptions gSubs;
std::mutex                gMutex;

// ---- shared runtime state (render thread writes most of it) ----
std::atomic<void*>    gLocalPlayer{nullptr};
std::atomic<int64_t>  gLocalId{0};
std::atomic<int64_t>  gLastNonHudFrameMs{0};
std::deque<int64_t>   gLeftClicks, gRightClicks; // guarded by gMutex
std::atomic<float>    gLastReach{0};
std::atomic<int64_t>  gLastReachAt{0};
std::atomic<bool>     gLastHitCrit{false};   // [Custom Crosshair] the last hit was a critical (gold marker)
std::atomic<int64_t>  gLastHitEvAt{0};       // when / which Fx event your last hit was (a real crit upgrades it)
std::atomic<uint32_t> gLastHitEvIdx{0};
// The game reported a real critical hit (its crit particles / the server's crit packet): the hit you just made was one.
void onRealCrit() {
    int64_t const at = gLastHitEvAt.load();
    if (at <= 0 || nowMs() - at > 600) return;
    auto& e = gFxEv.e[gLastHitEvIdx.load() % 32];
    if (e.kind == 2u && e.atMs >= at - 5) e.kind = 3u;
    gLastHitCrit = true;
}
bool const gCritListenerSet = (bedrock_forge::impact::setCritListener(&onRealCrit), true);
std::atomic<int64_t>  gVanillaCrossMs{0};     // [Custom Crosshair] the last frame the game wanted its crosshair (first person, GUI shown)
std::atomic<int64_t>  gTargetId{0}, gTargetAt{0};
std::atomic<int>      gTargetHits{0};
std::atomic<int>      gCombo{0};
std::atomic<int64_t>  gComboAt{0};
std::atomic<float>    gZoomNow{1.0f};
int64_t               gSessionStart = nowMs();

struct Fps {
    int64_t start = nowMs();
    int     frames = 0, value = 0;
    // frame times for the graph (ms), a ring of the last 120 HUD frames
    std::array<float, 120> ms{};
    size_t                 head = 0;
    Clock::time_point      lastT{};
    void    frame() {
        ++frames;
        auto const t = Clock::now();
        if (lastT.time_since_epoch().count()) ms[head++ % ms.size()] = std::chrono::duration<float, std::milli>(t - lastT).count();
        lastT = t;
        if (auto n = nowMs(); n - start >= 1000) { value = frames; frames = 0; start = n; }
    }
} gFps;

struct Speed {
    Vec3    last{};
    int64_t lastAt = 0;
    float   bps    = 0;
    void    sample(Vec3 const& p) {
        int64_t n = nowMs();
        if (lastAt && n - lastAt >= 250) {
            float dx = p.x - last.x, dz = p.z - last.z;
            bps    = std::sqrt(dx * dx + dz * dz) * 1000.0f / float(n - lastAt);
            last   = p; lastAt = n;
        } else if (!lastAt) { last = p; lastAt = n; }
    }
} gSpeed;

int clicksPerSecond(std::deque<int64_t>& q) {
    int64_t n = nowMs();
    while (!q.empty() && n - q.front() > 1000) q.pop_front();
    return static_cast<int>(q.size());
}

char const* compass(float yaw) {
    static constexpr std::array<char const*, 8> names{"S", "SW", "W", "NW", "N", "NE", "E", "SE"};
    float y = std::fmod(yaw + 360.0f * 4 + 22.5f, 360.0f);
    return names[static_cast<size_t>(y / 45.0f) % 8];
}
std::string roman(int n) {
    static constexpr std::array<char const*, 10> r{"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    return n >= 1 && n <= 10 ? r[n - 1] : std::to_string(n);
}
float textWidth(std::string const& s, float scale = 1.0f) { return 6.0f * scale * static_cast<float>(s.size()); }

// the info panel's box last frame (x, y, w, h): elements that sit near it step below it instead of covering its lines
float gInfoRect[4]{0, 0, 0, 0};
void avoidInfo(float x, float& y, float w, float h) {
    float const* r = gInfoRect;
    if (r[2] <= 0) return;
    bool const hit = x < r[0] + r[2] && x + w > r[0] && y < r[1] + r[3] && y + h > r[1];
    if (hit) y = r[1] + r[3] + 3.f;
}
void panel(MinecraftUIRenderContext& ctx, float x, float y, std::vector<std::string> const& lines, float opacity, Rgba color) {
    if (lines.empty()) return;
    float w = 0;
    for (auto& l : lines) w = std::max(w, textWidth(l));
    bf::hud::fillBox(ctx, x - 3, y - 2, w + 4, 10.5f * float(lines.size()) + 3, {0.f, 0.f, 0.f, opacity});
    for (size_t i = 0; i < lines.size(); ++i) bf::hud::drawLabel(ctx, x, y + 10.5f * float(i), lines[i], color);
}

void keycap(MinecraftUIRenderContext& ctx, float x, float y, float w, float h, std::string const& label, bool down) {
    bf::hud::fillBox(ctx, x, y, w, h, down ? Rgba{1.f, 1.f, 1.f, 0.78f} : Rgba{0.f, 0.f, 0.f, 0.45f});
    bf::hud::drawLabel(ctx, x + w / 2 - textWidth(label) / 2 + 0.5f, y + h / 2 - 4.5f, label, down ? Rgba{0, 0, 0, 1} : Rgba{1, 1, 1, 1});
}

// [BF] "in gameplay": the game hides the cursor while you play and shows it on every screen that takes the mouse.
// (Checking "only the HUD screen drew" failed on Hive: its own overlay screens draw all the time, which silently turned
// off Zoom, Auto Sprint and the other in-game keys.)
bool hudInputAllowed() { return w32::gameFocused() && (w32::cursorHidden() || nowMs() - gLastNonHudFrameMs.load() > 150); }

// ---------------------------------------------------------------- Memory (no <windows.h> here: declared by hand)
struct BfPmc { unsigned long cb, pageFaults; size_t peakWs, ws, qPeakPaged, qPaged, qPeakNonPaged, qNonPaged, pagefile, peakPagefile; };
extern "C" __declspec(dllimport) int __stdcall K32GetProcessMemoryInfo(void* process, BfPmc* pmc, unsigned long cb);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentProcess();
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void* module, wchar_t* name, unsigned long size);
double gameMemoryMb() {
    static int64_t at = 0;
    static double  mb = 0;
    if (int64_t n = nowMs(); n - at > 1000) {   // once a second is plenty
        at = n;
        BfPmc pmc{};
        pmc.cb = sizeof pmc;
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) mb = double(pmc.ws) / (1024.0 * 1024.0);
    }
    return mb;
}

// ---------------------------------------------------------------- Glass (Purxiy "glass": Frosted / Glow / Shimmer / Colour)
// Renderer Foundation's Glass Armor reads mods/RendererFoundation/config/GlassFrost.txt about once a second; this
// module is its menu. Off = the approved clear crystal (frost 0, no glow, no shimmer, white).
constexpr char const* kGlassTints[] = {"FFFFFF", "E6D9FF", "C77BFF", "BFE8FF", "FFC9E3", "FFE2A8"};
/// "Custom" in a Palette / Colour list: any colour from three sliders (the hue wheel 0..360, how strong, how bright).
/// which = 1: the lighter companion colour some effects use as their second one.
std::array<float, 3> customColour(Module& m, int which) {
    float const h = std::fmod(std::max(m.num("custom_hue", 280.f), 0.f), 360.f) / 60.f, sat = std::clamp(m.num("custom_sat", 0.8f), 0.f, 1.f),
                v = std::clamp(m.num("custom_bright", 1.f), 0.f, 1.f);
    float const c = v * sat, x = c * (1.f - std::fabs(std::fmod(h, 2.f) - 1.f)), lo = v - c;
    std::array<float, 3> o = h < 1 ? std::array<float, 3>{c, x, 0} : h < 2 ? std::array<float, 3>{x, c, 0} : h < 3 ? std::array<float, 3>{0, c, x}
                           : h < 4 ? std::array<float, 3>{0, x, c} : h < 5 ? std::array<float, 3>{x, 0, c} : std::array<float, 3>{c, 0, x};
    for (auto& k : o) { k += lo; if (which == 1) k += (1.f - k) * 0.45f; }
    return o;
}
void writeGlassFrost(float frost, float glow, bool shimmer, int tint, std::string const& custom = {}) {
    try {
        auto const path = ll::mod::NativeMod::current()->getModDir().parent_path() / "RendererFoundation" / "config" / "GlassFrost.txt";
        std::error_code ec;
        if (!std::filesystem::exists(path.parent_path(), ec)) return;   // no Renderer Foundation here
        std::ofstream(path) << std::format("frost {:.2f}\nglow {:.2f}\nshimmer {}\ntint {}\n", frost, glow, shimmer ? 1 : 0,
                                           !custom.empty() ? custom.c_str() : kGlassTints[std::clamp(tint, 0, int(std::size(kGlassTints)) - 1)]);
    } catch (...) {}
}
// Purxiy's Glass also says WHAT is glass (You / Other players / Items / Armour): Renderer Foundation reads
// config/GlassBody.txt about once a second. You / Other players off and Armour on = the Glass Armor of before.
void writeGlassBody(bool self, bool players, bool armour, bool items) {
    try {
        auto const path = ll::mod::NativeMod::current()->getModDir().parent_path() / "RendererFoundation" / "config" / "GlassBody.txt";
        std::error_code ec;
        if (!std::filesystem::exists(path.parent_path(), ec)) return;
        std::ofstream(path) << std::format("self {}\nplayers {}\narmour {}\nitems {}\n", self ? 1 : 0, players ? 1 : 0, armour ? 1 : 0, items ? 1 : 0);
    } catch (...) {}
}
std::string gGlassLast; // last written settings ("" = write on the next frame)
void glassTick(Module& m) {
    std::string& last = gGlassLast;
    std::string const now = std::format("{:.2f}/{:.2f}/{}/{}/{}/{}/{}/{}/{:.0f}/{:.2f}/{:.2f}", m.num("frost", 0.35f), m.num("glow", 0.2f), m.flag("shimmer", true),
                                        int(m.num("tint", 0)), m.flag("self", false), m.flag("players", false), m.flag("armour", true),
                                        m.flag("items", false), m.num("custom_hue", 280.f), m.num("custom_sat", 0.5f), m.num("custom_bright", 1.f));
    if (now == last) return;
    static int64_t at = 0;
    if (nowMs() - at < 150) return;   // dragging a slider: a few writes a second at most
    at = nowMs();
    last = now;
    std::string custom;
    if (int(m.num("tint", 0)) == 6) { auto const c = customColour(m, 0); custom = std::format("{:02X}{:02X}{:02X}", int(c[0] * 255.f + 0.5f), int(c[1] * 255.f + 0.5f), int(c[2] * 255.f + 0.5f)); }
    writeGlassFrost(m.num("frost", 0.35f), m.num("glow", 0.2f), m.flag("shimmer", true), int(m.num("tint", 0)), custom);
    writeGlassBody(m.flag("self", false), m.flag("players", false), m.flag("armour", true), m.flag("items", false));
}

// the client's accent (Client UI -> Accent colour; Novak purple by default), refreshed every HUD frame
Rgba kNeon{0.78f, 0.42f, 1.00f, 1.0f};
void refreshAccent() {
    static constexpr float kAcc[8][3] = {{0.78f, 0.42f, 1.00f}, {1.00f, 0.45f, 0.70f}, {0.30f, 0.65f, 1.00f}, {0.35f, 1.00f, 0.72f},
                                         {1.00f, 0.55f, 0.30f}, {0.70f, 0.92f, 1.00f}, {1.00f, 0.80f, 0.35f}, {1.00f, 0.30f, 0.42f}};
    auto* m = Registry::get().find("client_ui");
    int const i = std::clamp(m ? int(m->num("accent", 0)) : 0, 0, 7);
    kNeon = {kAcc[i][0], kAcc[i][1], kAcc[i][2], 1.0f};
}
float smooth01(float t) { t = std::clamp(t, 0.f, 1.f); return t * t * (3 - 2 * t); }

// ---------------------------------------------------------------- Haze + Mist (Renderer Foundation draws them)
// Purxiy's Haze ("a soft haze of colour over the world around you") and Mist ("soft autumn mist that hangs low in the
// world"): the same settings; Renderer Foundation reads config/Atmos.txt about once a second (shaders/worldfx_atmos.hlsl).
struct Pal { char const* name; float a[3], b[3]; };
constexpr Pal kHazePals[] = {
    {"Rose", {1.00f, 0.60f, 0.72f}, {1.00f, 0.78f, 0.70f}},   {"Lilac", {0.78f, 0.64f, 1.00f}, {0.93f, 0.80f, 1.00f}},
    {"Peach", {1.00f, 0.72f, 0.55f}, {1.00f, 0.86f, 0.72f}},  {"Sunset", {1.00f, 0.55f, 0.36f}, {0.86f, 0.46f, 0.78f}},
    {"Mint", {0.58f, 1.00f, 0.82f}, {0.70f, 0.92f, 1.00f}},   {"Ocean", {0.38f, 0.68f, 1.00f}, {0.34f, 0.94f, 0.96f}},
    {"Aurora", {0.36f, 1.00f, 0.66f}, {0.72f, 0.46f, 1.00f}}, {"Novak", {0.78f, 0.42f, 1.00f}, {0.95f, 0.92f, 1.00f}}};
constexpr Pal kMistPals[] = {
    {"Autumn", {1.00f, 0.86f, 0.70f}, {}}, {"Lilac", {0.86f, 0.80f, 1.00f}, {}}, {"Pearl", {0.92f, 0.94f, 1.00f}, {}}, {"Rose", {1.00f, 0.84f, 0.88f}, {}}};
// [Main Menu] the panorama pack RF writes (development_resource_packs/BF Menu World) must be in the global pack list of
// the profile you play on: the most recently used one. Added once (the old list is kept next to it), at the top.
// The game's own data folder: LeviLauncher keeps each instance's next to its exe (<exe dir>\Minecraft Bedrock); the Store
// install uses %APPDATA%\Minecraft Bedrock. (Writing to the wrong one is why the menu world never showed.)
std::filesystem::path gameDataRoot() {
    std::error_code ec;
    wchar_t exe[1024]{};
    if (GetModuleFileNameW(nullptr, exe, 1024)) {
        auto const local = std::filesystem::path(exe).parent_path() / L"Minecraft Bedrock";
        if (std::filesystem::exists(local / L"Users", ec)) return local;
    }
    wchar_t const* ad = _wgetenv(L"APPDATA");
    return ad ? std::filesystem::path(ad) / L"Minecraft Bedrock" : std::filesystem::path();
}
void registerMenuPack() {
    try {
        auto const root = gameDataRoot();
        if (root.empty()) return;
        auto const users = root / L"Users";
        std::filesystem::path best; std::filesystem::file_time_type bestT{};
        std::error_code ec;
        for (auto const& u : std::filesystem::directory_iterator(users, ec)) {   // the profile played most recently
            if (u.path().filename() == L"Shared") continue;
            auto const pe = u.path() / L"games" / L"com.mojang" / L"minecraftpe";
            if (!std::filesystem::exists(pe / L"options.txt", ec)) continue;
            auto const t = std::filesystem::last_write_time(pe / L"options.txt", ec);
            if (best.empty() || t > bestT) { best = pe / L"global_resource_packs.json"; bestT = t; }
        }
        if (best.empty()) return;
        std::string txt;
        if (std::filesystem::exists(best, ec)) { std::ifstream in(best, std::ios::binary); txt.assign(std::istreambuf_iterator<char>(in), {}); }
        else txt = "[\n]\n";                               // no global packs yet: start the list
        if (txt.find("b7f1e4a2-6c3d-4e58-9a71-2f0d5c8e3b14") != std::string::npos) return;
        auto const bak = best.parent_path() / L"global_resource_packs.before-bf-menu-world.json";
        if (std::filesystem::exists(best, ec) && !std::filesystem::exists(bak, ec)) std::filesystem::copy_file(best, bak, ec);
        auto const at = txt.find('[');
        if (at == std::string::npos) return;
        bool const empty = txt.find('{', at) == std::string::npos;
        std::string const entry = "\n\t{\n\t\t\"pack_id\" : \"b7f1e4a2-6c3d-4e58-9a71-2f0d5c8e3b14\",\n\t\t\"version\" : [ 1, 0, 0 ]\n\t}" + std::string(empty ? "" : ",");
        txt.insert(at + 1, entry);
        std::ofstream(best, std::ios::binary) << txt;
    } catch (...) {}
}
std::string gAtmosLast;
void atmosTick() {
    auto& r  = Registry::get();
    auto* hz = r.find("haze");
    auto* ms = r.find("mist");
    bool const h = hz && hz->enabled, m = ms && ms->enabled;
    float const tod = gTimeOfDay.load(std::memory_order_relaxed);
    std::string out;
    if (h || m || (r.find("ambience") && r.find("ambience")->enabled)) {
        auto const& hp = kHazePals[std::clamp(int(hz ? hz->num("palette", 0) : 0), 0, int(std::size(kHazePals)) - 1)];
        auto const& mp = kMistPals[std::clamp(int(ms ? ms->num("palette", 0) : 0), 0, int(std::size(kMistPals)) - 1)];
        out = std::format("haze {}\nhaze_a {:.3f} {:.3f} {:.3f}\nhaze_b {:.3f} {:.3f} {:.3f}\nhaze_strength {:.2f}\nhaze_height {:.0f}\nhaze_clear {:.0f}\n"
                          "haze_drift {:.2f}\nhaze_glow {:.2f}\nhaze_creatures {}\n",
                          h ? 1 : 0, hp.a[0], hp.a[1], hp.a[2], hp.b[0], hp.b[1], hp.b[2], hz ? hz->num("strength", 0.45f) : 0.f, hz ? hz->num("height", 24) : 24.f,
                          hz ? hz->num("clear", 8) : 8.f, hz ? hz->num("drift", 1) : 1.f, hz ? hz->num("glow", 0.6f) : 0.6f, hz && hz->flag("creatures", true) ? 1 : 0);
        out += std::format("mist {}\nmist_color {:.3f} {:.3f} {:.3f}\nmist_density {:.2f}\nmist_height {:.0f}\nmist_clear {:.0f}\nmist_drift {:.2f}\n"
                           "mist_rays {:.2f}\nmist_creatures {}\ntod {:.3f}\n",
                           m ? 1 : 0, mp.a[0], mp.a[1], mp.a[2], ms ? ms->num("density", 0.5f) : 0.f, ms ? ms->num("height", 10) : 10.f, ms ? ms->num("clear", 6) : 6.f,
                           ms ? ms->num("drift", 1) : 1.f, ms ? ms->num("rays", 0.6f) : 0.6f, ms && ms->flag("creatures", true) ? 1 : 0, tod < 0 ? 0.f : tod);
    } else out = "haze 0\nmist 0\n";
    if (auto* am = r.find("ambience"); am && am->enabled) {
        // palettes: Accent = the client's neon purple + white, Purxiy = their warm peach + lilac, Dream = pink + sky blue
        static constexpr float kAmb[3][2][3] = {{{0.78f, 0.42f, 1.00f}, {0.95f, 0.92f, 1.00f}},
                                                {{1.00f, 0.71f, 0.55f}, {0.80f, 0.62f, 1.00f}},
                                                {{1.00f, 0.62f, 0.86f}, {0.55f, 0.82f, 1.00f}}};
        int const p = std::clamp(int(am->num("palette", 0)), 0, 2);
        bool const amCustom = int(am->num("palette", 0)) == 3;
        auto const ca = amCustom ? customColour(*am, 0) : std::array<float, 3>{kAmb[p][0][0], kAmb[p][0][1], kAmb[p][0][2]};
        auto const cb = amCustom ? customColour(*am, 1) : std::array<float, 3>{kAmb[p][1][0], kAmb[p][1][1], kAmb[p][1][2]};
        out += std::format("amb 1\namb_a {:.3f} {:.3f} {:.3f}\namb_b {:.3f} {:.3f} {:.3f}\namb_amount {:.2f}\namb_size {:.2f}\namb_glow {:.2f}\n"
                           "amb_speed {:.2f}\namb_twinkle {:.2f}\namb_range {:.0f}\namb_style {}\namb_dof {}\namb_react {}\namb_daylight {:.2f}\n",
                           ca[0], ca[1], ca[2], cb[0], cb[1], cb[2], am->num("amount", 1.f),
                           am->num("size", 1.f), am->num("glow", 0.6f), am->num("speed", 1.f), am->num("twinkle", 0.5f), am->num("range", 24.f),
                           int(am->num("style", 0)), am->flag("depth_of_field", true) ? 1 : 0, am->flag("react", true) ? 1 : 0, am->num("daylight", 0.5f));
        if (out.find("tod ") == std::string::npos) out += std::format("tod {:.3f}\n", tod < 0 ? 0.f : tod);
    } else out += "amb 0\n";
    {   // Jump Circles / Hit Splash / Ripple / Shockwave (Purxiy settings): RF draws them from BF_GetFxEvents()
        static constexpr float kPal[3][2][3] = {{{0.78f, 0.42f, 1.00f}, {0.95f, 0.92f, 1.00f}},
                                                {{1.00f, 0.71f, 0.55f}, {0.80f, 0.62f, 1.00f}},
                                                {{1.00f, 0.62f, 0.86f}, {0.55f, 0.82f, 1.00f}}};
        auto pal = [&](Module* m, int which) {   // Accent follows the colour picked in Client UI (kNeon), not a fixed purple
            if (int(m->num("palette", 0)) == 3) return customColour(*m, which);   // Custom: any colour
            int const p = std::clamp(int(m->num("palette", 0)), 0, 2);
            if (p == 0 && which == 0) return std::array<float, 3>{kNeon.r, kNeon.g, kNeon.b};
            return std::array<float, 3>{kPal[p][which][0], kPal[p][which][1], kPal[p][which][2]};
        };
        auto* jc = r.find("jump_circles"); auto* hs = r.find("hit_splash"); auto* rp = r.find("ripple"); auto* sw = r.find("shockwave");
        if (jc && jc->enabled) { auto c = pal(jc, 0); out += std::format("jc 1\njc_c {:.3f} {:.3f} {:.3f}\njc_size {:.2f}\njc_dur {:.2f}\njc_glow {:.2f}\n", c[0], c[1], c[2], jc->num("size", 1.2f), jc->num("duration", 1.1f), jc->num("glow", 0.6f)); }
        else out += "jc 0\n";
        if (hs && hs->enabled) { auto a = pal(hs, 0); auto b = pal(hs, 1); out += std::format("hs 1\nhs_a {:.3f} {:.3f} {:.3f}\nhs_b {:.3f} {:.3f} {:.3f}\nhs_size {:.2f}\nhs_dur {:.2f}\nhs_glow {:.2f}\n", a[0], a[1], a[2], b[0], b[1], b[2], hs->num("size", 1.f), hs->num("duration", 8.f), hs->num("glow", 0.8f)); out += std::format("hs_mode {}\n", int(hs->num("colour_mode", 1.f))); }
        else out += "hs 0\n";
        if (rp && rp->enabled) { auto c = pal(rp, 0); out += std::format("rp 1\nrp_c {:.3f} {:.3f} {:.3f}\nrp_glow {:.2f}\nrp_dur {:.2f}\nrp_crits {}\nrp_ring {}\n", c[0], c[1], c[2], rp->num("glow", 0.8f), rp->num("duration", 0.8f), rp->flag("crits", true) ? 1 : 0, rp->flag("ring", true) ? 1 : 0); }
        else out += "rp 0\n";
        auto* tr = r.find("trail"); auto* au = r.find("aura"); auto* ha = r.find("halo");
        if (tr && tr->enabled) { auto c = pal(tr, 0); out += std::format("trail 1\ntrail_c {:.3f} {:.3f} {:.3f}\ntrail_dur {:.2f}\ntrail_size {:.2f}\ntrail_glow {:.2f}\ntrail_style {}\n", c[0], c[1], c[2], tr->num("duration", 1.2f), tr->num("size", 1.f), tr->num("glow", 0.6f), int(tr->num("style", 0))); }
        else out += "trail 0\n";
        if (au && au->enabled) { auto c = pal(au, 0); out += std::format("aura 1\naura_c {:.3f} {:.3f} {:.3f}\naura_size {:.2f}\naura_speed {:.2f}\naura_glow {:.2f}\naura_style {}\naura_fp {}\n", c[0], c[1], c[2], au->num("size", 1.f), au->num("speed", 1.f), au->num("glow", 0.65f), int(au->num("style", 0)), au->flag("first_person", false) ? 1 : 0); }
        else out += "aura 0\n";
        if (ha && ha->enabled) { auto c = pal(ha, int(ha->num("palette", 0)) == 3 ? 0 : 1); out += std::format("halo 1\nhalo_c {:.3f} {:.3f} {:.3f}\nhalo_size {:.2f}\nhalo_glow {:.2f}\nhalo_height {:.2f}\nhalo_design {}\nhalo_fp 0\n", c[0], c[1], c[2], ha->num("size", 1.f), ha->num("glow", 0.7f), ha->num("height", 1.f), int(ha->num("design", 0))); }
        else out += "halo 0\n";
        // Wings / Cape (Purxiy cosmetics) and Block Outline (Purxiy render): RF draws them in the atmos pass
        if (auto* wg = r.find("wings"); wg && wg->enabled) { auto c = pal(wg, 0); out += std::format("wg 1\nwg_c {:.3f} {:.3f} {:.3f}\nwg_design {}\nwg_size {:.2f}\nwg_glow {:.2f}\nwg_flap {:.2f}\nwg_fp {}\n", c[0], c[1], c[2], int(wg->num("design", 0)), wg->num("size", 1.f), wg->num("glow", 0.6f), wg->num("flap", 1.f), wg->flag("first_person", false) ? 1 : 0); }
        else out += "wg 0\n";
        if (auto* cp = r.find("cape"); cp && cp->enabled) { auto c = pal(cp, 0); auto c2 = pal(cp, 1); out += std::format("cp 1\ncp_c {:.3f} {:.3f} {:.3f}\ncp_c2 {:.3f} {:.3f} {:.3f}\ncp_design {}\ncp_glow {:.2f}\ncp_sway {:.2f}\n", c[0], c[1], c[2], c2[0], c2[1], c2[2], int(cp->num("design", 0)), cp->num("glow", 0.5f), cp->num("sway", 1.f)); }
        else out += "cp 0\n";
        {   // Main Menu: "Capture this view" -> RF turns the camera through the cube's faces and writes the menu's panorama
            static int panoId = 0;
            if (auto* mm = r.find("main_menu"); mm && mm->enabled) {
                if (auto* cap = mm->setting("capture"); cap && cap->on) {
                    cap->on = false; ++panoId; r.save();
                    registerMenuPack();
                }
            }
            out += std::format("pano_capture {}\n", panoId);
            if (auto* fl = r.find("freelook"); fl && fl->enabled) out += std::format("freelook {} {} {:.1f}\n", int(fl->num("key", 0)), int(fl->num("mode", 0)), fl->num("distance", 4.f));
            else out += "freelook 0 0 4\n";
        }
        // Cyberspace / Cute World (Purxiy world looks), drawn by RF's atmos pass
        if (auto* cy = r.find("cyberspace"); cy && cy->enabled) {
            static constexpr float kCy[4][3] = {{0.2f, 0.9f, 1.0f}, {0.78f, 0.42f, 1.0f}, {0.35f, 1.0f, 0.55f}, {1.0f, 0.3f, 0.55f}};
            int const pi = std::clamp(int(cy->num("palette", 0)), 0, 3);
            auto const c = pi == 1 ? std::array<float, 3>{kNeon.r, kNeon.g, kNeon.b} : std::array<float, 3>{kCy[pi][0], kCy[pi][1], kCy[pi][2]};
            out += std::format("cy 1\ncy_c {:.3f} {:.3f} {:.3f}\ncy_reality {:.2f}\ncy_glow {:.2f}\ncy_circuits {:.2f}\ncy_scan {:.1f}\ncy_sky {}\n", c[0], c[1], c[2],
                               cy->num("reality", 0.85f), cy->num("glow", 1.f), cy->num("circuits", 1.f), cy->num("interval", 6.f), cy->flag("sky", true) ? 1 : 0);
        } else out += "cy 0\n";
        if (auto* cu = r.find("cute_world"); cu && cu->enabled) {
            static constexpr float kCu[3][3] = {{1.0f, 0.55f, 0.75f}, {0.78f, 0.62f, 1.0f}, {1.0f, 0.75f, 0.45f}};
            int const pi = std::clamp(int(cu->num("palette", 0)), 0, 2);
            out += std::format("cu 1\ncu_c {:.3f} {:.3f} {:.3f}\ncu_soft {:.2f}\ncu_hearts {:.2f}\ncu_sparkle {:.2f}\ncu_pastel {:.2f}\n", kCu[pi][0], kCu[pi][1], kCu[pi][2],
                               cu->num("softness", 0.7f), cu->num("hearts", 1.f), cu->num("sparkles", 1.f), cu->num("pastel", 1.f));
            out += std::format("cu_eyes {}\n", cu->flag("eyes", true) ? 1 : 0);
        } else out += "cu 0\n";
        if (auto* bo = r.find("block_outline"); bo && bo->enabled) { auto c = pal(bo, 0); out += std::format("bo 1\nbo_c {:.3f} {:.3f} {:.3f}\nbo_style {}\nbo_w {:.2f}\nbo_glow {:.2f}\nbo_fill {:.2f}\nbo_face {}\nbo_glide {}\n", c[0], c[1], c[2], int(bo->num("style", 0)), bo->num("width", 2.f), bo->num("glow", 0.6f), bo->num("fill", 0.12f), bo->flag("face", true) ? 1 : 0, bo->flag("glide", true) ? 1 : 0); }
        else out += "bo 0\n";
        auto* ch = r.find("chroma"); auto* ra = r.find("radiance"); auto* wx = r.find("weather");
        if (ch && ch->enabled) {
            static constexpr float kTone[3][3] = {{0.80f, 0.74f, 0.92f}, {0.95f, 0.90f, 0.80f}, {0.70f, 0.70f, 0.72f}};   // Lilac, Cream, Noir
            static constexpr float kMem[4] = {30.f, 120.f, 600.f, 3600.f};
            auto const& tn = kTone[std::clamp(int(ch->num("tone", 0)), 0, 2)];
            out += std::format("chroma 1\nch_tone {:.3f} {:.3f} {:.3f}\nch_hint {:.2f}\nch_brush {:.1f}\nch_memory {:.0f}\nch_living {}\n", tn[0], tn[1], tn[2],
                               ch->num("hint", 0.06f), ch->num("brush", 6.f), kMem[std::clamp(int(ch->num("memory", 1)), 0, 3)], ch->flag("living", true) ? 1 : 0);
        } else out += "chroma 0\n";
        if (ra && ra->enabled) { auto c = pal(ra, 0); out += std::format("rad 1\nrad_c {:.3f} {:.3f} {:.3f}\nrad_width {:.1f}\nrad_glow {:.2f}\nrad_fill {:.2f}\nrad_speed {:.2f}\n", c[0], c[1], c[2], ra->num("width", 2.f), ra->num("glow", 0.6f), ra->num("fill", 0.2f), ra->num("speed", 1.f)); }
        else out += "rad 0\n";
        if (wx && wx->enabled) {
            out += std::format("wx 1\nwx_mode {}\nwx_intensity {:.2f}\nwx_wind {:.2f}\nwx_size {:.2f}\nwx_range {:.0f}\n", int(wx->num("mode", 0)), wx->num("intensity", 1.f), wx->num("wind", 0.45f), wx->num("size", 1.f), wx->num("range", 24.f));
            out += std::format("wx_wet {:.2f}\n", int(wx->num("mode", 0)) == 0 ? wx->num("wet", 0.6f) : 0.f);
        } else out += "wx 0\n";
        // lightning + sounds (rain only): the strike times are ours, so the flash (RF, from the wall clock) and the
        // thunder (here, a moment later: sound is slower than light) agree
        {
            bool const rainOn = wx && wx->enabled && int(wx->num("mode", 0)) == 0;
            float const vol = rainOn ? wx->num("sounds", 0.5f) : 0.f;
            // (rain loop removed - Fernando 2026-11-20: "a weird noise that sounds nothing like rain"; thunder stays)
            try { weathersounds::rain(0.f); } catch (...) {}
            static int64_t nextStrike = 0, lastStrike = 0, thunderAt = 0;
            static float az = 0.f, nearK = 0.5f;
            static uint32_t rs = 0x9e3779b9u;
            auto rnd = [] { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return float(rs & 0xffffu) / 65535.f; };
            int64_t const wall = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (rainOn && wx->flag("lightning", true)) {
                if (nextStrike == 0) nextStrike = wall + 6000 + int64_t(rnd() * 10000.f);
                if (wall >= nextStrike) {
                    lastStrike = wall; az = rnd() * 6.2831853f; nearK = rnd();
                    thunderAt = wall + int64_t(400.f + (1.f - nearK) * 2600.f);
                    nextStrike = wall + int64_t((14000.f + rnd() * 26000.f) / std::max(wx->num("intensity", 1.f), 0.3f));
                }
                out += std::format("wx_light 1\nwx_strike {} {:.3f} {:.2f}\n", lastStrike, az, nearK);
                if (thunderAt && wall >= thunderAt) { thunderAt = 0; try { weathersounds::thunder(vol, nearK); } catch (...) {} }
            } else { nextStrike = 0; out += "wx_light 0\n"; }
        }
        if (auto* bk = r.find("bokeh"); bk && bk->enabled)
            out += std::format("bk 1\nbk_mode {}\nbk_dist {:.1f}\nbk_band {:.2f}\nbk_blur {:.2f}\nbk_hi {:.2f}\nbk_near {}\nbk_fringe {:.2f}\n", int(bk->num("focus", 0)),
                               bk->num("distance", 24.f), bk->num("band", 0.26f), bk->num("blur", 0.5f), bk->num("highlights", 0.6f), bk->flag("near", false) ? 1 : 0, bk->num("fringe", 0.45f));
        else out += "bk 0\n";
        if (auto* pm = r.find("place_animation"); pm && pm->enabled) { auto c = pal(pm, 0); out += std::format("pa 1\npa_c {:.3f} {:.3f} {:.3f}\npa_dur {:.2f}\npa_glow {:.2f}\npa_break {}\n", c[0], c[1], c[2], pm->num("duration", 0.34f) * 1.6f, pm->num("glow", 0.6f), pm->flag("breaking", true) ? 1 : 0); }
        else out += "pa 0\n";
        if (sw && sw->enabled) { auto c = pal(sw, 0); out += std::format("sw 1\nsw_c {:.3f} {:.3f} {:.3f}\nsw_size {:.2f}\nsw_glow {:.2f}\n", c[0], c[1], c[2], sw->num("size", 1.f), sw->num("glow", 0.7f)); }
        else out += "sw 0\n";
    }
    {   // [Sky] the Sky module picks whose sky owns the screen: Renderer Foundation applies it when it changes (Ctrl+F6 still works)
        auto* sk = r.find("sky");
        int const mode = sk && sk->enabled ? int(sk->num("mode", 0) + 0.5f) : -1;
        static int const kRf[4] = {3, 1, 2, 0};   // Living Skies, Cloud Sea, Novak Glass, Vanilla -> RF's skymode
        int const pick = gSkyWorldPick.load();   // Shuffle landed on Cloud Sea (1) / Novak Glass (2)
        out += std::format("skymode {}\n", mode < 0 ? -1 : mode == 0 && pick ? pick : kRf[std::clamp(mode, 0, 3)]);
        int const q = sk ? int(sk->num("quality", 0) + 0.5f) : 0;   // Fast = 2x2, Balanced = 2x1, Sharp = full (RF's VRS on the dome)
        out += std::format("livesky_rate {}\n", q <= 0 ? 2 : q == 1 ? 1 : 0);
    }
    if (out == gAtmosLast) return;
    static int64_t at = 0;
    if (nowMs() - at < 250) return;   // dragging a slider: a few writes a second; the sun moves slowly (tod in steps of 0.001)
    at = nowMs();
    gAtmosLast = out;
    try {
        auto const path = ll::mod::NativeMod::current()->getModDir().parent_path() / "RendererFoundation" / "config" / "Atmos.txt";
        std::error_code ec;
        if (std::filesystem::exists(path.parent_path(), ec)) std::ofstream(path) << out;
    } catch (...) {}
}

// ---------------------------------------------------------------- HUD hider (Purxiy "choose which Minecraft HUD elements to hide")
// Walks the HUD screen's control tree and switches the matching controls invisible every frame (the game's bindings
// may switch them back on). v1 also writes the tree's control names once to logs/hud_controls.txt, so the name lists
// below can be checked against the real 1.26.51 HUD.
struct HideGroup { char const* id; char const* label; std::vector<char const*> names; };
std::vector<HideGroup> const& hideGroups() {
    static std::vector<HideGroup> const g = {
        // names from the real 1.26.51 HUD (logs/hud_controls.txt). The XP bar is left out: on desktop the hotbar sits
        // INSIDE exp_rend / its progress bars, so hiding the bar hid the hotbar with it.
        {"hotbar", "Hotbar", {"hotbar"}},
        {"hearts", "Hearts", {"heart_rend", "horse_heart_rend_0", "horse_heart_rend_1"}},
        {"hunger", "Hunger", {"hunger_rend"}},
        {"armor", "Armor bar", {"armor_rend"}},
        {"bubbles", "Air bubbles", {"bubbles_rend_0", "bubbles_rend_1"}},
        {"item_name", "Held item name", {"item_text_factory"}},
        {"boss", "Boss bar", {"boss_health_panel"}},
        {"tips", "Action bar / tips", {"hud_actionbar_text_area", "hud_tip_text_factory"}},
    };
    return g;
}
void dumpControls(UIControl const& c, int depth, std::ostringstream& out, int& n) {
    if (n > 4000 || depth > 24) return;
    ++n;
    out << std::string(size_t(depth) * 2, ' ') << *c.mName << "\n";
    for (auto const& ch : *c.mChildren)
        if (ch) dumpControls(*ch, depth + 1, out, n);
}
// what we switched off, so it can be switched back on (seen in game: unticking Hotbar left it hidden until a restart)
struct HiddenCtl { std::weak_ptr<UIControl> c; ::ui::VisibilityFlag vis; float alpha; std::string name; };
std::vector<HiddenCtl> gHidden;
void hideControls(std::shared_ptr<UIControl> const& sp, std::vector<std::string> const& names, int depth) {
    if (!sp || depth > 24) return;
    UIControl& c = *sp;
    std::string const& nm = *c.mName;
    for (auto const& want : names)
        if (nm == want) {
            bool known = false;
            for (auto const& h : gHidden) if (h.c.lock() == sp) { known = true; break; }
            if (!known) gHidden.push_back({sp, c.mVisible, c.mAlpha, nm});
            c.mVisible = ::ui::VisibilityFlag::None;
            c.mAlpha   = 0.0f;
            return;
        }
    for (auto const& ch : *c.mChildren) hideControls(ch, names, depth + 1);
}
void hudHiderFrame(ScreenView& view) {
    auto const& tree = view.mVisualTree;
    if (!tree) return;
    auto const& rootSp = tree->mRootControl;
    UIControl* root = rootSp.get();
    if (!root) return;
    static bool dumped = false;
    if (!dumped) {
        dumped = true;
        std::ostringstream out;
        int n = 0;
        out << "HUD control tree (" << *tree->mRootControlName << ")\n";
        dumpControls(*root, 0, out, n);
        asyncfile::write(ll::mod::NativeMod::current()->getModDir() / "logs" / "hud_controls.txt", out.str());
    }
    auto* m = Registry::get().find("hud_hider");
    std::vector<std::string> names;
    if (m && m->enabled)
        for (auto const& g : hideGroups())
            if (m->flag(g.id, false)) for (auto* n2 : g.names) names.emplace_back(n2);
    // switch back on what is no longer wanted hidden (or gone with the tree)
    for (auto it = gHidden.begin(); it != gHidden.end();) {
        auto sp = it->c.lock();
        bool const still = sp && std::find(names.begin(), names.end(), it->name) != names.end();
        if (!sp) { it = gHidden.erase(it); continue; }
        if (!still) { sp->mVisible = it->vis; sp->mAlpha = it->alpha; it = gHidden.erase(it); continue; }
        ++it;
    }
    if (!names.empty()) hideControls(rootSp, names, 0);
}

// ---------------------------------------------------------------- Now Playing (Purxiy "Spotify player": your music on screen,
// with lyrics to sing along). Uses LyricsForge's media session + lyrics; the in-world lyrics stay LyricsForge's own.
[[maybe_unused]] std::string mmss(int64_t ms) { ms = std::max<int64_t>(ms, 0) / 1000; return std::format("{}:{:02}", ms / 60, ms % 60); }
[[maybe_unused]] void drawNowPlaying(Frame& f, Module& m) {
    ::bf::lyrics::win::NowPlaying np;
    if (!::bf::lyrics::win::nowPlaying(np) || !np.hasTrack) return;
    int const paused = int(m.num("paused", 1));                   // Show / Dim / Hide
    if (!np.playing && paused == 2) return;
    float const dim = !np.playing && paused == 1 ? 0.55f : 1.f;
    int const layout = int(m.num("layout", 0));                  // Split / Combined / Lyrics only / Player only
    Rgba acc = kNeon;
    if (m.num("colors", 0) > 0.5f && np.accent) acc = {float((np.accent >> 16) & 255) / 255.f, float((np.accent >> 8) & 255) / 255.f, float(np.accent & 255) / 255.f, 1};
    auto ell = [](std::string t, size_t n) { if (t.size() > n) { t.resize(n - 1); t += "~"; } return t; };
    // the player card
    if (layout != 2) {
        float const s = m.num("player_size", 1.f), W = 150.f * s, H = (layout == 1 ? 50.f : 38.f) * s;
        float x, y;
        hudedit::place(m.num("player_x", 1.f), m.num("player_y", 0.92f), W, H, f.width, f.height, x, y);
        hudedit::note("now_playing", "Player", "player_x", "player_y", "player_size", x, y, W, H, f.width, f.height);
        if (m.flag("player_background", true)) bf::hud::fillBox(f.ctx, x, y, W, H, {0.06f, 0.04f, 0.10f, m.num("player_opacity", 0.6f) * dim});
        bf::hud::fillBox(f.ctx, x, y, 2.f * s, H, {acc.r, acc.g, acc.b, dim});
        bf::hud::drawLabel(f.ctx, x + 6.f * s, y + 3.f * s, ell(np.title, 24), {1, 1, 1, dim}, 0.9f * s);
        bf::hud::drawLabel(f.ctx, x + 6.f * s, y + 13.f * s, ell(np.artist, 28), {0.80f, 0.74f, 0.92f, 0.9f * dim}, 0.75f * s);
        if (layout == 1 && !np.line.empty()) bf::hud::drawLabel(f.ctx, x + 6.f * s, y + 23.f * s, ell(np.line, 30), {acc.r, acc.g, acc.b, dim}, 0.75f * s);
        float const k = np.durationMs > 0 ? std::clamp(float(np.positionMs) / float(np.durationMs), 0.f, 1.f) : 0.f;
        float const by = y + H - 9.f * s, bw = W - 12.f * s;
        bf::hud::fillBox(f.ctx, x + 6.f * s, by, bw, 1.5f * s, {1, 1, 1, 0.18f * dim});
        bf::hud::fillBox(f.ctx, x + 6.f * s, by, bw * k, 1.5f * s, {acc.r, acc.g, acc.b, dim});
        bf::hud::drawLabel(f.ctx, x + 6.f * s, by + 2.5f * s, mmss(np.positionMs), {0.8f, 0.8f, 0.85f, 0.8f * dim}, 0.55f * s);
        if (m.flag("visualizer", true)) {   // little bars that move with the music (LyricsForge's loopback bands)
            auto const band = ::bf::lyrics::win::audioLatest();
            for (int i = 0; i < 12; ++i) {
                float const v = band.hasSpec ? band.spec[i + 2] : 0.08f + 0.05f * std::sin(float(nowMs()) * 0.004f + float(i));
                float const bh = std::clamp(v, 0.05f, 1.f) * 9.f * s;
                bf::hud::fillBox(f.ctx, x + W - 6.f * s - float(12 - i) * 3.f * s, y + 13.f * s - bh + 9.f * s, 2.f * s, bh, {acc.r, acc.g, acc.b, 0.85f * dim});
            }
        }
    }
    // the lyrics card (top centre by default): the line being sung, the next one dimmer
    if ((layout == 0 || layout == 2) && (!np.line.empty() || !np.nextLine.empty())) {
        float const s = m.num("lyrics_size", 1.f);
        int const lines = std::clamp(int(m.num("lines", 2)), 1, 3);
        std::string const a = ell(np.line, 60), b = ell(np.nextLine, 60);
        float const w = std::max(textWidth(a, s), lines > 1 ? textWidth(b, 0.8f * s) : 0.f) + 16.f * s;
        float const h = (lines > 1 ? 24.f : 14.f) * s;
        float x, y;
        hudedit::place(m.num("lyrics_x", 0.5f), m.num("lyrics_y", 0.06f), w, h, f.width, f.height, x, y);
        hudedit::note("now_playing", "Lyrics", "lyrics_x", "lyrics_y", "lyrics_size", x, y, w, h, f.width, f.height);
        if (m.flag("lyrics_background", true)) bf::hud::fillBox(f.ctx, x, y, w, h, {0.06f, 0.04f, 0.10f, m.num("lyrics_opacity", 0.6f) * dim});
        if (m.flag("glow", true) && !a.empty()) bf::hud::fillBox(f.ctx, x, y + 3.f * s, 2.f * s, 9.f * s, {acc.r, acc.g, acc.b, dim});
        bf::hud::drawLabel(f.ctx, x + 8.f * s, y + 3.f * s, a, {1, 1, 1, dim}, s);
        if (lines > 1 && !b.empty()) bf::hud::drawLabel(f.ctx, x + 8.f * s, y + 14.f * s, b, {0.75f, 0.70f, 0.85f, 0.75f * dim}, 0.8f * s);
    }
}

} // namespace
namespace hudedit {
namespace {
std::mutex                    gEdMutex;
std::vector<std::pair<Elem, int64_t>> gEd;
}
void note(char const* mod, char const* label, char const* xKey, char const* yKey, char const* sizeKey, float x, float y, float w, float h,
          float W, float H, bool pixels) {
    if (W <= 0 || H <= 0) return;
    int64_t const now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard l(gEdMutex);
    for (auto& [e, at] : gEd)
        if (e.mod == mod && e.xKey == xKey) { e.x = x / W; e.y = y / H; e.w = w / W; e.h = h / H; at = now; return; }
    gEd.push_back({Elem{mod, label, xKey, yKey, sizeKey ? sizeKey : "", x / W, y / H, w / W, h / H, pixels}, now});
}
std::vector<Elem> elems() {
    int64_t const now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard l(gEdMutex);
    std::vector<Elem> out;
    for (auto const& [e, at] : gEd) if (now - at < 120000) out.push_back(e);   // drawn in the last two minutes
    return out;
}
} // namespace hudedit
namespace {

// ---------------------------------------------------------------- Notifications + Active Modules (Purxiy schema)
// Purxiy (buddies' client, used as reference only - its code is locked): "A quiet message when a keybind toggles a
// module" and "Every module you have switched on, as a list". Same settings (size, background, opacity, x/y as a
// fraction of the screen, duration / sort, gradient, hud); drawn our way in the Novak neon purple + white.
struct Note { std::string name; bool on; int64_t t; };
float   gPlayerListBottom = 0; // set by List Players each frame (top right)
int64_t gPlayerListFrame  = 0;
std::deque<Note> gNotes; // guarded by gMutex

void notifyToggle(Module const& m) {
    if (!Registry::get().on("notifications")) return;
    std::lock_guard lock(gMutex);
    for (auto it = gNotes.begin(); it != gNotes.end(); ++it)
        if (it->name == m.name) { gNotes.erase(it); break; } // one card per module: a re-toggle replaces it
    gNotes.push_back({m.name, m.enabled, nowMs()});
    while (gNotes.size() > 5) gNotes.pop_front();
}


void drawNotifications(Frame& f, Module& m) {
    float const s    = m.num("size", 1.0f), dur = m.num("duration", 2.5f) * 1000.f;
    float const op   = m.flag("background", true) ? m.num("opacity", 0.6f) : 0.f;
    float const fx   = m.num("x", 1.0f), fy = m.num("y", 1.0f);
    float const h    = 18.f * s, gap = 3.f * s, pad = 6.f * s;
    int64_t const now = nowMs();
    std::vector<Note> notes;
    {
        std::lock_guard lock(gMutex);
        while (!gNotes.empty() && float(now - gNotes.front().t) > dur + 300.f) gNotes.pop_front();
        notes.assign(gNotes.begin(), gNotes.end());
    }
    // the stack's box: room for five cards, anchored by x / y like every element (cards hug its outer side)
    float const boxW = 160.f * s, boxH = 5.f * (h + gap);
    float bx, by;
    hudedit::place(fx, fy, boxW, boxH, f.width, f.height, bx, by);
    hudedit::note("notifications", "Notifications", "x", "y", "size", bx, by, boxW, boxH, f.width, f.height);
    float y = fy > 0.5f ? by + boxH - h : by;
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {   // newest nearest the anchor
        float const age = float(now - it->t);
        float const in  = smooth01(age / 180.f), out = 1.f - smooth01((age - dur) / 300.f);
        float const a   = in * out;
        if (a <= 0.01f) continue;
        std::string const state = it->on ? "enabled" : "disabled";
        float const w  = pad * 2 + 4.f * s + textWidth(it->name, s) + 6.f * s + textWidth(state, s);
        float const slide = (1.f - in) * (w * 0.6f);                  // glides in from the screen edge
        float x = fx > 0.5f ? bx + boxW - w + slide : bx - slide;
        x = std::clamp(x, -w, f.width);
        if (op > 0) bf::hud::fillBox(f.ctx, x, y, w, h, {0.06f, 0.04f, 0.10f, op * a});
        Rgba const bar = it->on ? kNeon : Rgba{0.55f, 0.52f, 0.60f, 1};
        bf::hud::fillBox(f.ctx, x, y, 2.f * s, h, {bar.r, bar.g, bar.b, a});        // accent edge
        float const ty = y + (h - 9.f * s) / 2;
        bf::hud::drawLabel(f.ctx, x + pad + 2.f * s, ty, it->name, {1, 1, 1, a}, s);
        bf::hud::drawLabel(f.ctx, x + pad + 2.f * s + textWidth(it->name, s) + 6.f * s, ty, state,
                           it->on ? Rgba{0.86f, 0.70f, 1.0f, a} : Rgba{0.62f, 0.60f, 0.66f, a}, s);
        y += fy > 0.5f ? -(h + gap) : (h + gap);
    }
}

void drawActiveModules(Frame& f, Module& m) {
    float const s  = m.num("size", 1.0f);
    float const op = m.flag("background", true) ? m.num("opacity", 0.6f) : 0.f;
    float const fx = m.num("x", 1.0f), fy = m.num("y", 0.0f);
    bool const  grad = m.flag("gradient", true), withHud = m.flag("hud", false), alpha = m.num("sort", 0) > 0.5f;
    std::vector<std::string> names;
    for (auto& o : Registry::get().all()) {
        if (!o->enabled || o->planned || o->id == "active_modules" || o->id == "notifications") continue;
        if (!withHud && o->category == Category::Hud) continue;
        names.push_back(o->name);
    }
    if (names.empty()) return;
    if (alpha) std::sort(names.begin(), names.end());
    else std::stable_sort(names.begin(), names.end(), [](auto const& a, auto const& b) { return a.size() > b.size(); });
    float const h = 11.f * s, pad = 3.f * s;
    float boxW = 0;
    for (auto const& n : names) boxW = std::max(boxW, textWidth(n, s) + pad * 2 + 2.f * s);
    float const boxH = h * float(names.size());
    float bx, y;
    hudedit::place(fx, fy, boxW, boxH, f.width, f.height, bx, y);
    if (fx > 0.5f && y < gPlayerListBottom + 2.f && nowMs() - gPlayerListFrame < 500) y = gPlayerListBottom + 2.f;   // don't sit on the player list
    hudedit::note("active_modules", "Active Modules", "x", "y", "size", bx, y, boxW, boxH, f.width, f.height);
    float const t = float(nowMs() % 6000) / 6000.f;
    for (size_t i = 0; i < names.size(); ++i) {
        float const w = textWidth(names[i], s) + pad * 2 + 2.f * s;
        float const x = fx > 0.5f ? bx + boxW - w : bx;
        // gradient: neon purple -> white down the list, drifting slowly
        float k = grad ? 0.5f + 0.5f * std::sin((float(i) / float(std::max<size_t>(names.size(), 2)) - t) * 6.2831f) : 1.f;
        Rgba c{kNeon.r + (1 - kNeon.r) * k, kNeon.g + (1 - kNeon.g) * k, kNeon.b + (1 - kNeon.b) * k, 1};
        if (op > 0) bf::hud::fillBox(f.ctx, x, y, w, h, {0.06f, 0.04f, 0.10f, op});
        bf::hud::fillBox(f.ctx, fx > 0.5f ? x + w - 1.5f * s : x, y, 1.5f * s, h, c);
        bf::hud::drawLabel(f.ctx, x + pad + (fx > 0.5f ? 0.f : 1.5f * s), y + (h - 9.f * s) / 2, names[i], c, s);
        y += h;
    }
}

/// Module toggle keys (Module::key, set in each module's page). Edge-triggered; not while typing or in menus.
std::array<bool, 256> gKeyHeld{};
void onModuleKey(int vk, bool down) {
    if (vk <= 0 || vk > 255) return;
    bool const was = gKeyHeld[size_t(vk)];
    gKeyHeld[size_t(vk)] = down;
    if (down && !was) { try { keysounds::press(vk); } catch (...) {} }   // Creamy Keys: every press, menus too
    if (!down || was || menu::isOpen() || !hudInputAllowed()) return;
    if (auto* np = Registry::get().find("now_playing"); np && np->enabled) {   // LyricsForge Player media keys
        if (int(np->num("key_play", 0)) == vk) { ::bf::lyrics::win::mediaCommand(0); return; }
        if (int(np->num("key_next", 0)) == vk) { ::bf::lyrics::win::mediaCommand(1); return; }
        if (int(np->num("key_prev", 0)) == vk) { ::bf::lyrics::win::mediaCommand(2); return; }
    }
    for (auto& m : Registry::get().all())
        if (m->key == vk && !m->planned) {
            Registry::get().setEnabled(*m, !m->enabled);
            notifyToggle(*m);
        }
}
} // namespace

// ============================================================================ hooks
LL_TYPE_INSTANCE_HOOK(BfFovHook, ll::memory::HookPriority::Normal, LevelRendererPlayer, &LevelRendererPlayer::getFov, float,
                      float alpha, bool variable) {
    ++diag::fovHook;
    float fov = origin(alpha, variable);
    auto& reg = Registry::get();
    if (auto* m = reg.find("fov_changer"); m && m->enabled) fov *= m->num("scale", 1.0f);
    float target = 1.0f;
    if (auto* z = reg.find("zoom"); z && z->enabled && hudInputAllowed() && w32::keyDown(int(z->num("key", 0x43)))) target = z->num("factor", 4.0f);
    float now  = gZoomNow.load();
    auto* z    = reg.find("zoom");
    float lerp = z && z->flag("smooth", true) ? 0.25f : 1.0f;
    now += (target - now) * lerp;
    if (std::abs(now - target) < 0.01f) now = target;
    gZoomNow = now;
    fov *= ::bf::lyrics::win::cinemaFovScale();
    postfx::noteFov(fov / now);
    return fov / now;
}

// [Custom Crosshair] the vanilla crosshair steps aside while ours is on (otherwise both draw on top of each other)
LL_TYPE_INSTANCE_HOOK(BfCrosshairHook, ll::memory::HookPriority::Normal, HudCursorRenderer, &HudCursorRenderer::$render, void,
                      MinecraftUIRenderContext& ctx, IClientInstance& ci, UIControl& owner, int pass) {
    gVanillaCrossMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (Registry::get().on("crosshair")) return;
    origin(ctx, ci, owner, pass);
}

LL_TYPE_INSTANCE_HOOK(BfTurnHook, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::_applyTurnDelta, void,
                      Vec2 const& delta) {
    float scale = 1.0f;
    if (auto* m = Registry::get().find("sensitivity"); m && m->enabled) scale *= m->num("multiplier", 1.0f);
    if (auto* z = Registry::get().find("zoom"); z && z->enabled && z->flag("slow_turn", true)) scale /= std::max(1.0f, gZoomNow.load());
    if (scale == 1.0f) return origin(delta);
    Vec2 scaled = delta;
    scaled.x *= scale;
    scaled.z *= scale;
    origin(scaled);
}

namespace {
void onLocalAttack(Player& p, Actor& target, Vec3 const& hit) {
    if (static_cast<void*>(&p) != gLocalPlayer.load()) return;
    Vec3 const& eye = p.getPosition();
    float dx = hit.x - eye.x, dy = hit.y - eye.y, dz = hit.z - eye.z;
    gLastReach   = std::sqrt(dx * dx + dy * dy + dz * dz);
    gLastReachAt = nowMs();
    if (nowMs() - gComboAt.load() > 2500) gCombo = 0;
    ++gCombo;
    gComboAt = nowMs();
    ::bf::lyrics::win::noteCombat(); // LyricsFlow Combat Safe: dim lyrics for a moment after a hit
    try { matchfx::noteHit(uint64_t(target.getRuntimeID().rawID)); } catch (...) {}   // [Cute World] their face reacts
    try {   // Target HUD: who we are fighting
        int64_t const id = target.getOrCreateUniqueID().rawID;
        if (id != gTargetId.load()) gTargetHits = 0;
        gTargetId = id;
        ++gTargetHits;
        gTargetAt = nowMs();
    } catch (...) {}
    try {   // Hit Splash / Ripple: where the hit landed. A plain hit for now - it becomes a critical only when the game
            // itself reports one (onRealCrit below). The old guess (falling = crit) fired crit effects on Hive, where
            // falling hits are not crits (2026-11-24).
        gLastHitCrit = false;
        gLastHitEvAt  = nowMs();
        gLastHitEvIdx = gFxEv.frame;
        pushFxEvent(2u, hit.x, hit.y, hit.z);
    } catch (...) {}
    try { cosmetics::noteLocalHit(false); } catch (...) {}   // Knives: sparks + cut on your hit
}
} // namespace

LL_TYPE_INSTANCE_HOOK(BfAttackHook, ll::memory::HookPriority::Normal, GameMode, &GameMode::$attack, bool, Actor& entity,
                      Vec3 const& hitPosition) {
    ++diag::attackHook;
    onLocalAttack(mPlayer, entity, hitPosition);
    return origin(entity, hitPosition);
}
LL_TYPE_INSTANCE_HOOK(BfSurvivalAttackHook, ll::memory::HookPriority::Normal, SurvivalMode, &SurvivalMode::$attack, bool,
                      Actor& entity, Vec3 const& hitPosition) {
    onLocalAttack(mPlayer, entity, hitPosition);
    return origin(entity, hitPosition);
}

// ============================================================================ modules
namespace diag {
namespace {
std::mutex               gScreensMutex;
std::vector<std::string> gScreens;
} // namespace
void noteScreen(std::string const& name) {
    std::lock_guard lock(gScreensMutex);
    if (gScreens.size() < 24 && std::find(gScreens.begin(), gScreens.end(), name) == gScreens.end()) gScreens.push_back(name);
}
void write() {
    std::ostringstream out; // [BF] built here, written off the game thread (asyncfile)
    out << "light hook " << lightHook.load() << " (brightened " << lightBrightened.load() << ", image " << lightImage.load() << ")\n"
        << "overlay hook " << overlayHook.load() << "\nbob hook " << bobHook.load() << "\ntime hook " << timeHook.load() << " (match time: render " << timeRender.load() << ", client level " << timeClient.load() << ", refused " << timeOther.load() << ")"
        << "\nfov hook " << fovHook.load() << "\nattack hook " << attackHook.load() << "\nchat hook " << chatHook.load()
        << "\nmenu opens " << menuOpens.load() << "\ntessellator light " << tessLight.load() << " (relights " << fbRelights.load()
        << ")\npack shaders cached " << packShaders.load() << "\nmodules on:";
    for (auto& m : Registry::get().all())
        if (m->enabled && !m->planned) out << " " << m->id;
    out << "\nscreens under the menu:";
    {
        std::lock_guard lock(gScreensMutex);
        for (auto& n : gScreens) out << " " << n;
    }
    out << "\n";
    asyncfile::write(ll::mod::NativeMod::current()->getModDir() / "logs" / "diag.txt", out.str());
}
} // namespace diag

void registerBuiltins() {
    auto& r = Registry::get();
    static bool done = false;
    if (done) return;
    done = true;

    // ---------------- HUD
    // [Purxiy "server_info"] Bedrock tells the client the server's world name and who is on, not its address; and it
    // keeps no ping for the player list (Purxiy's Java "ping" reads the tab list's latency - Bedrock has none to read)
    r.add("server_info", "Server Info", Category::Hud, "The world / server you are on and how many are there", false).hud = [](Frame& f) {
        if (!f.player) return;
        std::string name;
        size_t n = 0;
        try { auto& lv = f.player->getLevel(); name = *lv.getLevelData().mLevelName; n = lv.getPlayerList().size(); } catch (...) {}
        if (name.empty()) name = "this world";
        for (size_t i; (i = name.find("\xC2\xA7")) != std::string::npos;) name.erase(i, std::min<size_t>(3, name.size() - i));
        if (name.size() > 28) { name.resize(27); name += "~"; }
        f.infoLines.push_back(std::format("{} | {} online", name, n));
    };
    r.add("memory", "Memory", Category::Hud, "How much memory the game is using", false).hud = [](Frame& f) {
        double const mb = gameMemoryMb();
        f.infoLines.push_back(mb >= 1024 ? std::format("Mem {:.2f} GB", mb / 1024.0) : std::format("Mem {:.0f} MB", mb));
    };
    {   // Watermark: the Bedrock Forge mark in a corner, drifting neon purple -> white
        auto& wm = r.add("watermark", "Watermark", Category::Hud, "The Bedrock Forge mark in your corner", false);
        wm.slider("size", "Size", 1.0f, 0.5f, 2.0f, 0.05f).slider("x", "X (0 left .. 1 right)", 0.f, 0.f, 1.f, 0.01f)
            .slider("y", "Y (0 top .. 1 bottom)", 0.f, 0.f, 1.f, 0.01f).toggleSetting("fps", "Show FPS", true);
        wm.hud = [](Frame& f) {
            auto* m = Registry::get().find("watermark");
            if (!m) return;
            float const s = m->num("size", 1.f) * 1.3f;
            std::string const t1 = "BEDROCK FORGE", t2 = m->flag("fps", true) ? std::format("  {} fps", gFps.value) : std::string();
            float const w = textWidth(t1, s) + textWidth(t2, s * 0.75f) + 10.f;
            float x, y;
            hudedit::place(m->num("x", 0.f), m->num("y", 0.f), w, 10.f * s + 2.f, f.width, f.height, x, y);
            x += 3.f; y += 3.f;
            hudedit::note("watermark", "Watermark", "x", "y", "size", x - 3.f, y - 3.f, w + 2.f, 10.f * s + 5.f, f.width, f.height);
            bf::hud::fillBox(f.ctx, x - 3, y - 3, w + 2, 10.f * s + 5, {0.06f, 0.04f, 0.10f, 0.5f});
            float const k = 0.5f + 0.5f * std::sin(float(nowMs() % 4000) / 4000.f * 6.2831f);
            bf::hud::drawLabel(f.ctx, x, y, t1, {0.78f + 0.22f * k, 0.42f + 0.58f * k, 1.0f, 1.0f}, s);
            if (!t2.empty()) bf::hud::drawLabel(f.ctx, x + textWidth(t1, s), y + 2.f * s, t2, {1, 1, 1, 0.75f}, s * 0.75f);
        };
    }
    {   // [Purxiy schema] notifications on by default (they only show when a keybind toggles something)
        auto& n = r.add("notifications", "Notifications", Category::Hud, "A quiet message when a keybind toggles a module", true);
        n.slider("size", "Size", 1.0f, 0.5f, 2.0f, 0.05f).toggleSetting("background", "Background", true)
            .slider("opacity", "Background opacity", 0.6f, 0.f, 1.f, 0.05f).slider("x", "X (0 left .. 1 right)", 1.f, 0.f, 1.f, 0.01f)
            .slider("y", "Y (0 top .. 1 bottom)", 1.f, 0.f, 1.f, 0.01f).slider("duration", "Duration (s)", 2.5f, 1.5f, 6.f, 0.5f);
        n.hud = [](Frame& f) { if (auto* m = Registry::get().find("notifications")) drawNotifications(f, *m); };
        // off by default here (Purxiy has it on): our top right already holds the player list
        auto& am = r.add("active_modules", "Active Modules", Category::Hud, "Every module you have switched on, as a list", false);
        am.slider("size", "Size", 1.0f, 0.5f, 2.0f, 0.05f).toggleSetting("background", "Background", true)
            .slider("opacity", "Background opacity", 0.6f, 0.f, 1.f, 0.05f).slider("x", "X (0 left .. 1 right)", 1.f, 0.f, 1.f, 0.01f)
            .slider("y", "Y (0 top .. 1 bottom)", 0.f, 0.f, 1.f, 0.01f).choice("sort", "Order", {"Length", "Alphabetical"}, 0)
            .toggleSetting("gradient", "Gradient", true).toggleSetting("hud", "HUD elements", false);
        am.hud = [](Frame& f) { if (auto* m = Registry::get().find("active_modules")) drawActiveModules(f, *m); };
    }
    r.add("fps", "FPS", Category::Hud, "Frames per second, with a live frame graph", true)
        .toggleSetting("graph", "Frame graph", false).slider("size", "Graph size", 1.0f, 0.5f, 2.0f, 0.05f)
        .slider("opacity", "Background opacity", 0.55f, 0.f, 1.f, 0.05f).slider("x", "X (0 left .. 1 right)", 0.f, 0.f, 1.f, 0.01f)
        .slider("y", "Y (0 top .. 1 bottom)", 0.3f, 0.f, 1.f, 0.01f).hud = [](Frame& f) {
        f.infoLines.push_back(std::format("FPS {}", gFps.value));
        auto* m = Registry::get().find("fps");
        if (!m || !m->flag("graph", false)) return;
        // [Purxiy-style] live frame graph: one bar per frame, height = frame time (16.7 ms = half), spikes go purple
        float const s = m->num("size", 1.0f), W = 120.f * s, H = 28.f * s;
        float x, y;
        hudedit::place(m->num("x", 0.0f), m->num("y", 0.3f), W + 4.f, H + 14.f * s, f.width, f.height, x, y);
        avoidInfo(x, y, W + 4.f, H + 14.f * s);   // audit: Server Info / Session lines grew the info panel under it
        x += 2.f; y += 2.f;
        hudedit::note("fps", "Frame graph", "x", "y", "size", x - 2.f, y - 2.f, W + 4.f, H + 4.f + 10.f * s, f.width, f.height);
        bf::hud::fillBox(f.ctx, x - 2, y - 2, W + 4, H + 4 + 10.f * s, {0.06f, 0.04f, 0.10f, m->num("opacity", 0.55f)});
        float worst = 0;
        for (size_t i = 0; i < gFps.ms.size(); ++i) {
            float const v  = gFps.ms[(gFps.head + i) % gFps.ms.size()];
            worst = std::max(worst, v);
            float const bh = std::clamp(v / 33.3f, 0.02f, 1.f) * H;
            bool const spike = v > 25.f;
            bf::hud::fillBox(f.ctx, x + float(i) * (W / float(gFps.ms.size())), y + H - bh, std::max(W / float(gFps.ms.size()) - 0.2f, 0.4f), bh,
                             spike ? Rgba{0.78f, 0.42f, 1.0f, 0.95f} : Rgba{1, 1, 1, 0.55f});
        }
        bf::hud::fillBox(f.ctx, x, y + H * 0.5f, W, 0.5f, {1, 1, 1, 0.25f});   // the 60 fps line
        bf::hud::drawLabel(f.ctx, x, y + H + 2.f, std::format("{} fps  worst {:.1f} ms", gFps.value, worst), {1, 1, 1, 0.9f}, 0.75f * s);
    };
    r.add("cps", "CPS", Category::Hud, "Left | right clicks per second", true).hud = [](Frame& f) {
        std::lock_guard lock(gMutex);
        f.infoLines.push_back(std::format("CPS {} | {}", clicksPerSecond(gLeftClicks), clicksPerSecond(gRightClicks)));
    };
    r.add("coordinates", "Coordinates", Category::Hud, "Your X Y Z", true).hud = [](Frame& f) {
        if (!f.player) return;
        auto const& p = f.player->getPosition();
        f.infoLines.push_back(std::format("XYZ {:.0f} {:.0f} {:.0f}", std::floor(p.x), std::floor(p.y - 1.62f), std::floor(p.z)));
    };
    r.add("direction", "Direction", Category::Hud, "Facing and yaw, or a compass strip that glides with your view", true)
        .toggleSetting("strip", "Compass strip (top centre)", false).slider("size", "Strip size", 1.0f, 0.5f, 2.0f, 0.05f)
        .slider("opacity", "Strip background", 0.45f, 0.f, 1.f, 0.05f).hud = [](Frame& f) {
        if (!f.player) return;
        float yaw = f.player->getRotation().z;
        auto* dm = Registry::get().find("direction");
        if (dm && dm->flag("strip", false)) {
            // [Purxiy-style] the strip: 180 degrees across, ticks every 15, letters at the 8 points, a neon caret in the middle
            float const s = dm->num("size", 1.f), W = 180.f * s, H = 16.f * s, cx = f.width / 2, y = 3.f;
            bf::hud::fillBox(f.ctx, cx - W / 2, y, W, H, {0.06f, 0.04f, 0.10f, dm->num("opacity", 0.45f)});
            static constexpr char const* kPts[8] = {"S", "SW", "W", "NW", "N", "NE", "E", "SE"};
            for (int d = -90; d <= 90; d += 5) {
                float const ang = std::floor(yaw / 5.f) * 5.f + float(d);
                float const px  = cx + (ang - yaw) * (W / 180.f);
                if (px < cx - W / 2 + 2 || px > cx + W / 2 - 2) continue;
                float const fall = 1.f - std::abs(px - cx) / (W / 2);           // fade toward the ends
                int const   a360 = ((int(std::lround(ang)) % 360) + 360) % 360;
                if (a360 % 45 == 0) {
                    std::string const t = kPts[a360 / 45];
                    bf::hud::drawLabel(f.ctx, px - textWidth(t, 0.8f * s) / 2, y + 4.f * s, t,
                                       a360 == 180 ? Rgba{kNeon.r, kNeon.g, kNeon.b, fall} : Rgba{1, 1, 1, fall}, 0.8f * s);   // north in neon
                } else if (a360 % 15 == 0)
                    bf::hud::fillBox(f.ctx, px, y + H - 5.f * s, 0.6f, 4.f * s, {1, 1, 1, 0.6f * fall});
            }
            bf::hud::fillBox(f.ctx, cx - 0.5f, y + H, 1.f, 3.f * s, {kNeon.r, kNeon.g, kNeon.b, 1});
            return;
        }
        f.infoLines.push_back(std::format("Facing {} ({:.0f})", compass(yaw), yaw));
    };
    r.add("speed", "Speed Display", Category::Hud, "Horizontal speed in blocks per second").hud = [](Frame& f) {
        if (!f.player) return;
        gSpeed.sample(f.player->getPosition());
        f.infoLines.push_back(std::format("Speed {:.2f} b/s", gSpeed.bps));
    };
    r.add("clock", "Clock", Category::Hud, "Local time").hud = [](Frame& f) {
        int h = 0, m = 0;
        w32::localTime(h, m);
        f.infoLines.push_back(std::format("Time {:02}:{:02}", h, m));
    };
    r.add("session", "Session Timer", Category::Hud, "How long you have been playing (and in this world)")
        .toggleSetting("world", "Time in this world too", true).hud = [](Frame& f) {
        static int64_t worldStart = 0;
        static void*   worldOf    = nullptr;
        void* const lp = f.player ? static_cast<void*>(f.player) : nullptr;
        if (lp != worldOf) { worldOf = lp; worldStart = nowMs(); }   // a new world / server joined
        int64_t s = (nowMs() - gSessionStart) / 1000;
        auto* m = Registry::get().find("session");
        if (m && m->flag("world", true) && f.player) {
            int64_t const w = (nowMs() - worldStart) / 1000;
            f.infoLines.push_back(std::format("Session {}:{:02}:{:02} | here {}:{:02}", s / 3600, (s / 60) % 60, s % 60, w / 60, w % 60));
        } else f.infoLines.push_back(std::format("Session {}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60));
    };
    r.add("xp_info", "XP Info", Category::Hud, "XP earned / needed for the next level").hud = [](Frame& f) {
        if (!f.player) return;
        f.infoLines.push_back(std::format("XP {} / {}", f.player->getXpEarnedAtCurrentLevel(), f.player->getXpNeededForNextLevel()));
    };
    r.add("item_counter", "Item Counter", Category::Hud, "How many of the held item you carry").hud = [](Frame& f) {
        if (!f.player) return;
        auto const& held = f.player->getSelectedItem();
        if (held.isNull()) return;
        auto& inv   = f.player->getInventory();
        int   count = 0;
        for (int i = 0; i < inv.getContainerSize(); ++i) {
            auto const& s = inv.getItem(i);
            if (!s.isNull() && s.getId() == held.getId() && s.getAuxValue() == held.getAuxValue()) count += s.mCount;
        }
        f.infoLines.push_back(std::format("{} x{}", held.getName(), count));
    };
    r.add("keystrokes", "Keystrokes", Category::Hud, "W A S D, Space and mouse buttons", true)
        .slider("x", "X (negative = from right)", 4, -2000, 2000, 2)
        .slider("y", "Y (negative = from bottom)", -76, -2000, 2000, 2)
        .hud = [](Frame& f) {
        auto* m  = Registry::get().find("keystrokes");
        float k  = 18, g = 2, wTot = 3 * k + 2 * g;
        float sx = m->num("x"), sy = m->num("y");
        float bx = sx >= 0 ? sx : f.width + sx - wTot, by = sy >= 0 ? sy : f.height + sy;
        bx = std::clamp(bx, 0.f, std::max(f.width - wTot, 0.f)); by = std::clamp(by, 0.f, std::max(f.height - (3 * (k + g) + 8), 0.f));
        // the HUD editor box (pixels mode: it writes the top-left back, + the 3 / 2 px it takes off here)
        hudedit::note("keystrokes", "Keystrokes", "x", "y", "", bx - 3, by - 2, wTot, 3 * (k + g) + 8, f.width, f.height, true);
        bool  ok = w32::gameFocused();
        auto  d  = [&](int vk) { return ok && w32::keyDown(vk); };
        int   l = 0, rr = 0;
        {
            std::lock_guard lock(gMutex);
            l  = clicksPerSecond(gLeftClicks);
            rr = clicksPerSecond(gRightClicks);
        }
        keycap(f.ctx, bx + k + g, by, k, k, "W", d(w32::vk::W));
        keycap(f.ctx, bx, by + k + g, k, k, "A", d(w32::vk::A));
        keycap(f.ctx, bx + k + g, by + k + g, k, k, "S", d(w32::vk::S));
        keycap(f.ctx, bx + 2 * (k + g), by + k + g, k, k, "D", d(w32::vk::D));
        float half = (wTot - g) / 2;
        keycap(f.ctx, bx, by + 2 * (k + g), half, k, std::format("L {}", l), d(w32::vk::LButton));
        keycap(f.ctx, bx + half + g, by + 2 * (k + g), half, k, std::format("R {}", rr), d(w32::vk::RButton));
        keycap(f.ctx, bx, by + 3 * (k + g), wTot, 8, "", d(w32::vk::Space));
    };
    // [Purxiy "armor_status" / "potion_effects"] glass cards with durability / time bars; placed by the HUD editor
    r.add("armor_hud", "Armor Status", Category::Hud, "Durability of everything you wear (and hold)")
        .slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f).slider("opacity", "Background opacity", 0.55f, 0.f, 1.f, 0.05f)
        .slider("x", "X (0 left .. 1 right)", 1.f, 0.f, 1.f, 0.01f).slider("y", "Y (0 top .. 1 bottom)", 0.55f, 0.f, 1.f, 0.01f)
        .toggleSetting("percent", "Show percent", true).toggleSetting("held", "Held item too", true)
        .hud = [](Frame& f) {
        if (!f.player) return;
        auto* m = Registry::get().find("armor_hud");
        struct Row { std::string name; int dmg = 0, max = 0; };
        std::vector<Row> rows;
        auto add = [&](ItemStack const& st) {
            if (st.isNull()) return;
            Row r2{st.getName()};
            try { if (st.isDamageableItem()) { r2.dmg = st.getDamageValue(); if (auto* it = st.mItem.get()) r2.max = it->getMaxDamage(); } } catch (...) {}
            rows.push_back(std::move(r2));
        };
        using Slot = SharedTypes::Legacy::ArmorSlot;
        for (Slot sl : {Slot::Head, Slot::Torso, Slot::Legs, Slot::Feet}) add(f.player->getArmor(sl));
        if (m->flag("held", true)) add(f.player->getSelectedItem());
        if (rows.empty()) return;
        float const s = m->num("size", 1.f), rh = 14.f * s;
        float nameW = 0;   // audit: sized to the longest name ("Netherite Chestplate" was cut to "Netherite Chest~")
        for (auto& r2 : rows) nameW = std::max(nameW, textWidth(r2.name.size() > 26 ? r2.name.substr(0, 25) + "~" : r2.name, 0.75f * s));
        float const W = std::max(110.f * s, nameW + (m->flag("percent", true) ? textWidth("100%", 0.7f * s) + 6.f * s : 0.f) + 10.f * s), H = rh * float(rows.size()) + 4.f * s;
        float x, y;
        hudedit::place(m->num("x", 1.f), m->num("y", 0.55f), W, H, f.width, f.height, x, y);
        hudedit::note("armor_hud", "Armor Status", "x", "y", "size", x, y, W, H, f.width, f.height);
        bf::hud::fillBox(f.ctx, x, y, W, H, {0.06f, 0.04f, 0.10f, m->num("opacity", 0.55f)});
        for (size_t i = 0; i < rows.size(); ++i) {
            auto const& r2 = rows[i];
            float const ry = y + 2.f * s + rh * float(i);
            float const k = r2.max > 0 ? std::clamp(1.f - float(r2.dmg) / float(r2.max), 0.f, 1.f) : 1.f;
            // green -> amber -> red as it wears; the accent while it is near full
            Rgba const c = k > 0.85f ? kNeon : k > 0.5f ? Rgba{0.55f, 1.f, 0.6f, 1} : k > 0.25f ? Rgba{1.f, 0.78f, 0.3f, 1} : Rgba{1.f, 0.35f, 0.35f, 1};
            std::string nm = r2.name.size() > 26 ? r2.name.substr(0, 25) + "~" : r2.name;
            bf::hud::drawLabel(f.ctx, x + 4.f * s, ry + 1.f, nm, {1, 1, 1, 0.95f}, 0.75f * s);
            if (r2.max > 0) {
                float const bw = W - 8.f * s;
                bf::hud::fillBox(f.ctx, x + 4.f * s, ry + rh - 4.f * s, bw, 1.5f * s, {1, 1, 1, 0.15f});
                bf::hud::fillBox(f.ctx, x + 4.f * s, ry + rh - 4.f * s, bw * k, 1.5f * s, c);
                if (m->flag("percent", true)) {
                    std::string const pc = std::format("{}%", int(std::round(k * 100.f)));
                    bf::hud::drawLabel(f.ctx, x + W - 4.f * s - textWidth(pc, 0.7f * s), ry + 1.f, pc, {c.r, c.g, c.b, 1}, 0.7f * s);
                }
            }
        }
    };
    r.add("potion_hud", "Potion Effects", Category::Hud, "Active effects and their timers")
        .slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f).slider("opacity", "Background opacity", 0.55f, 0.f, 1.f, 0.05f)
        .slider("x", "X (0 left .. 1 right)", 0.f, 0.f, 1.f, 0.01f).slider("y", "Y (0 top .. 1 bottom)", 0.55f, 0.f, 1.f, 0.01f)
        .hud = [](Frame& f) {
        if (!f.player) return;
        auto* m = Registry::get().find("potion_hud");
        static std::map<uint32_t, int> longest;   // the longest time seen per effect = the full bar
        struct Row { std::string t; int ticks; uint32_t id; };
        std::vector<Row> rows;
        for (auto const& e : f.player->getAllEffects()) {
            int const ticks = e.mDuration->mValue;
            uint32_t const id = e.mId;
            int& lg = longest[id];
            if (ticks > lg || ticks <= 0) lg = std::max(ticks, 1);
            rows.push_back({std::format("{} {}", e.getDisplayName(), roman(e.mAmplifier + 1)), ticks, id});
        }
        if (rows.empty()) { longest.clear(); return; }
        float const s = m->num("size", 1.f), rh = 14.f * s, W = 120.f * s, H = rh * float(rows.size()) + 4.f * s;
        float x, y;
        hudedit::place(m->num("x", 0.f), m->num("y", 0.55f), W, H, f.width, f.height, x, y);
        hudedit::note("potion_hud", "Potion Effects", "x", "y", "size", x, y, W, H, f.width, f.height);
        bf::hud::fillBox(f.ctx, x, y, W, H, {0.06f, 0.04f, 0.10f, m->num("opacity", 0.55f)});
        for (size_t i = 0; i < rows.size(); ++i) {
            auto const& r2 = rows[i];
            float const ry = y + 2.f * s + rh * float(i);
            int const secs = r2.ticks / 20;
            bool const inf = r2.ticks > 20 * 60 * 60 * 2 || r2.ticks < 0;
            std::string const tm = inf ? std::string("**") : std::format("{}:{:02}", secs / 60, secs % 60);
            bool const ending = !inf && secs <= 10;
            float const blink = ending ? 0.55f + 0.45f * std::sin(float(nowMs()) * 0.012f) : 1.f;
            bf::hud::drawLabel(f.ctx, x + 4.f * s, ry + 1.f, r2.t.substr(0, 20), {1, 1, 1, 0.95f * blink}, 0.75f * s);
            bf::hud::drawLabel(f.ctx, x + W - 4.f * s - textWidth(tm, 0.7f * s), ry + 1.f, tm, ending ? Rgba{1.f, 0.55f, 0.45f, blink} : Rgba{0.85f, 0.8f, 1.f, 1}, 0.7f * s);
            float const k = inf ? 1.f : std::clamp(float(r2.ticks) / float(std::max(longest[r2.id], 1)), 0.f, 1.f);
            float const bw = W - 8.f * s;
            bf::hud::fillBox(f.ctx, x + 4.f * s, ry + rh - 4.f * s, bw, 1.5f * s, {1, 1, 1, 0.15f});
            bf::hud::fillBox(f.ctx, x + 4.f * s, ry + rh - 4.f * s, bw * k, 1.5f * s, kNeon);
        }
    };
    r.add("player_list", "List Players", Category::Hud, "Everyone on the server (top right)").hud = [](Frame& f) {
        if (!f.player) return;
        std::vector<std::string> lines{"Players"};
        for (auto const& [uuid, entry] : f.player->getLevel().getPlayerList()) {
            (void)uuid;
            lines.push_back(hideNick(*entry.mName));
            if (lines.size() > 20) break;
        }
        float w = 0;
        for (auto& l : lines) w = std::max(w, textWidth(l));
        panel(f.ctx, f.width - w - 4, 4, lines, 0.35f, {1, 1, 1, 1});
        gPlayerListBottom = 4.f + 10.5f * float(lines.size()) + 4.f;   // Active Modules starts below this (top right)
        gPlayerListFrame  = nowMs();
    };
    // [Purxiy "custom_crosshair": "your aim, in Purxiy colours"] shapes, colour, outline + glow, spread while you move, a
    // kick when you swing, a tint while you aim at someone, a hit marker (gold on a critical). Drawn over the vanilla one
    r.add("crosshair", "Custom Crosshair", Category::Hud, "Your aim, in your colours: shapes, spread, hit markers")
        .choice("style", "Style", {"Cross", "Plus gap", "Dot", "Circle", "Circle + dot", "T"}, 1)
        .slider("size", "Length", 4, 1, 14, 0.5f).slider("thickness", "Thickness", 1, 0.5f, 4, 0.5f).slider("gap", "Gap", 2, 0, 8, 0.5f)
        .toggleSetting("dot", "Centre dot", false).toggleSetting("outline", "Outline", true).slider("glow", "Glow", 0.4f, 0.f, 1.f, 0.05f)
        .slider("opacity", "Opacity", 0.95f, 0.1f, 1.f, 0.05f)
        .choice("color", "Colour", {"Accent", "White", "Ice", "Mint", "Rose", "Gold"}, 0)
        .slider("spread", "Spread while moving", 0.5f, 0.f, 1.5f, 0.05f).slider("kick", "Kick on swing", 0.5f, 0.f, 1.5f, 0.05f)
        .toggleSetting("target_tint", "Tint on a target", true).toggleSetting("hit_marker", "Hit marker", true)
        .toggleSetting("breathing", "Breathing", false)
        .hud = [](Frame& f) {
        auto* m  = Registry::get().find("crosshair");
        if (!f.player) return;
        int64_t const now = nowMs();
        {   // only where the game would draw its own (not in third person, not with the GUI hidden); no hook seen yet = always
            int64_t const v = gVanillaCrossMs.load();
            int64_t const st = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (v > 0 && st - v > 250) return;
        }
        float cx = std::round(f.width / 2), cy = std::round(f.height / 2);
        int const style = std::clamp(int(m->num("style", 1)), 0, 5);
        float len = m->num("size", 4), th = m->num("thickness", 1), gap = m->num("gap", 2);
        float const op = m->num("opacity", 0.95f);
        static constexpr Rgba kCols[6] = {{0, 0, 0, 0}, {1, 1, 1, 1}, {0.6f, 0.9f, 1.f, 1}, {0.45f, 1.f, 0.75f, 1}, {1.f, 0.55f, 0.75f, 1}, {1.f, 0.82f, 0.35f, 1}};
        int const ci = std::clamp(int(m->num("color", 0)), 0, 5);
        Rgba c = ci == 0 ? kNeon : kCols[ci];
        // aiming at someone: tint toward a warm red
        bool onTarget = false;
        try { auto& hr = f.player->getLevel().getHitResult(); onTarget = hr.mType == HitResultType::Entity; } catch (...) {}
        if (onTarget && m->flag("target_tint", true)) c = Rgba{1.f, 0.38f, 0.42f, 1};
        // spread with your speed, a kick right after a swing, breathing
        static float sp = 0;
        gSpeed.sample(f.player->getPosition());
        sp += (std::clamp(gSpeed.bps / 5.6f, 0.f, 1.f) - sp) * 0.2f;
        float const swing = std::clamp(1.f - float(now - gLastReachAt.load()) / 180.f, 0.f, 1.f);
        gap += sp * 3.f * m->num("spread", 0.5f) + swing * 3.f * m->num("kick", 0.5f);
        if (m->flag("breathing", false)) gap += 0.6f * (0.5f + 0.5f * std::sin(float(now % 100000) * 0.0025f));
        c.a = op;
        float const glow = m->num("glow", 0.4f);
        bool const outl = m->flag("outline", true);
        auto bar = [&](float x, float y, float w, float h) {
            if (glow > 0) bf::hud::fillBox(f.ctx, x - 1.5f, y - 1.5f, w + 3, h + 3, {c.r, c.g, c.b, 0.18f * glow * op});
            if (outl) bf::hud::fillBox(f.ctx, x - 0.5f, y - 0.5f, w + 1, h + 1, {0, 0, 0, 0.55f * op});
            bf::hud::fillBox(f.ctx, x, y, w, h, c);
        };
        float const ht = th / 2;
        auto ring = [&](float r) {
            int const n = std::max(16, int(r * 6.f));
            for (int i = 0; i < n; ++i) {
                float const an = 6.2831853f * float(i) / float(n);
                bar(cx + std::cos(an) * r - ht, cy + std::sin(an) * r - ht, th, th);
            }
        };
        switch (style) {
        case 0:  // cross: arms joined in the middle
            bar(cx - len - ht, cy - ht, 2 * len + th, th); bar(cx - ht, cy - len - ht, th, 2 * len + th); break;
        case 1:  // plus with a gap
        case 5:  // T: no top arm
            bar(cx - gap - len, cy - ht, len, th); bar(cx + gap, cy - ht, len, th);
            if (style == 1) bar(cx - ht, cy - gap - len, th, len);
            bar(cx - ht, cy + gap, th, len); break;
        case 2: bar(cx - th, cy - th, 2 * th, 2 * th); break;
        case 3:
        case 4: ring(gap + len * 0.5f); if (style == 4) bar(cx - ht, cy - ht, th, th); break;
        }
        if (m->flag("dot", false) && style != 2 && style != 4) bar(cx - ht, cy - ht, th, th);
        // the hit marker: four short diagonal ticks; gold on a critical
        float const hk = std::clamp(1.f - float(now - gLastReachAt.load()) / 260.f, 0.f, 1.f);
        if (hk > 0 && m->flag("hit_marker", true)) {
            Rgba const hc = gLastHitCrit.load() ? Rgba{1.f, 0.82f, 0.3f, hk} : Rgba{1.f, 1.f, 1.f, hk};
            float const r0 = gap + 3.f, r1 = r0 + 4.f;
            for (int q = 0; q < 4; ++q) {
                float const dx = (q & 1) ? 1.f : -1.f, dy = (q & 2) ? 1.f : -1.f;
                for (float r = r0; r <= r1; r += 1.f) bf::hud::fillBox(f.ctx, cx + dx * r * 0.7071f - 0.75f, cy + dy * r * 0.7071f - 0.75f, 1.5f, 1.5f, hc);
            }
        }
    };
    // [Purxiy "name_tags": clean tags for players] a glass card over everyone near you: name (Nick Hider aware), health
    // and distance, scaled by distance and faded out at the edge of its range. Projected with the camera's real FOV.
    r.add("name_tags", "Name Tags", Category::Hud, "Clean tags over the players near you: name, health and distance", false)
        .toggleSetting("health", "Health bar", true).toggleSetting("distance", "Distance", true)
        .slider("size", "Size", 1.f, 0.6f, 1.6f, 0.05f).slider("opacity", "Background opacity", 0.55f, 0.f, 1.f, 0.05f)
        .slider("range", "Range (blocks)", 48.f, 8.f, 96.f, 4.f)
        .hud = [](Frame& f) {
        if (!f.player || f.width <= 0 || f.height <= 0) return;
        auto* m = Registry::get().find("name_tags");
        BfPostFxV1 const* fx = BF_GetPostFx();
        float const fovY = fx && fx->fov > 10.f ? fx->fov : 70.f;
        float const ty = std::tan(fovY * 0.5f * 3.14159265f / 180.f), tx = ty * f.width / f.height;
        Vec3 const eye = f.player->getPosition();
        float const yw = f.player->getRotation().y * 3.14159265f / 180.f, pt = f.player->getRotation().x * 3.14159265f / 180.f;
        float const fw[3] = {-std::sin(yw) * std::cos(pt), -std::sin(pt), std::cos(yw) * std::cos(pt)};
        float rt[3] = {-fw[2], 0.f, fw[0]};                                       // forward x up (the game's handedness)
        float const rl = std::max(std::hypot(rt[0], rt[2]), 1e-4f); rt[0] /= rl; rt[2] /= rl;
        float const up[3] = {rt[1] * fw[2] - rt[2] * fw[1], rt[2] * fw[0] - rt[0] * fw[2], rt[0] * fw[1] - rt[1] * fw[0]};
        float const S = m->num("size", 1.f), op = m->num("opacity", 0.55f), range = m->num("range", 48.f);
        bool const hp = m->flag("health", true), dist = m->flag("distance", true);
        Actor* self = f.player;
        try {
            f.player->getLevel().forEachPlayer([&](Player& p) {
                if (static_cast<Actor*>(&p) == self) return true;
                // vanilla parity: nobody sneaking or invisible gets a tag (no extra info over the game's own tags)
                try { if (p.isSneaking() || p.isInvisible()) return true; } catch (...) { return true; }
                Vec3 const pos = p.getPosition();
                float const d[3] = {pos.x - eye.x, pos.y + 0.62f - eye.y, pos.z - eye.z};   // a little over the head (getPosition is the eye)
                float const dz = d[0] * fw[0] + d[1] * fw[1] + d[2] * fw[2];
                float const dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (dz < 0.3f || dl > range) return true;
                float const sx = (d[0] * rt[0] + d[1] * rt[1] + d[2] * rt[2]) / (dz * tx), sy = (d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / (dz * ty);
                if (std::fabs(sx) > 1.2f || std::fabs(sy) > 1.2f) return true;
                float const X = (sx * 0.5f + 0.5f) * f.width, Y = (0.5f - sy * 0.5f) * f.height;
                float const k = S * std::clamp(1.25f - dl / 40.f, 0.55f, 1.15f);       // nearer = a little bigger
                float const fade = std::clamp((range - dl) / 6.f, 0.f, 1.f);
                std::string const name = hideNick(p.getRealName());
                std::string const ds = dist ? std::format("{:.0f}m", dl) : std::string();
                float const nw = textWidth(name, 0.8f * k), dw = dist ? textWidth(ds, 0.65f * k) + 5.f * k : 0.f;
                float const W = nw + dw + 10.f * k, H = (hp ? 15.f : 12.f) * k;
                float const x0 = X - W / 2, y0 = Y - H;
                bf::hud::fillBox(f.ctx, x0, y0, W, H, {0.06f, 0.04f, 0.10f, op * fade});
                bf::hud::fillBox(f.ctx, x0, y0, W, 1.f * k, {kNeon.r, kNeon.g, kNeon.b, 0.9f * fade});      // the accent line on top
                bf::hud::drawLabel(f.ctx, x0 + 5.f * k, y0 + 2.5f * k, name, {1, 1, 1, fade}, 0.8f * k);
                if (dist) bf::hud::drawLabel(f.ctx, x0 + 5.f * k + nw + 5.f * k, y0 + 3.2f * k, ds, {0.85f, 0.8f, 0.95f, 0.8f * fade}, 0.65f * k);
                if (hp) {
                    float hv = 1.f;
                    try { hv = std::clamp(float(p.getHealth()) / std::max(1.f, float(p.getMaxHealth())), 0.f, 1.f); } catch (...) {}
                    Rgba const hc = hv > 0.5f ? Rgba{0.45f, 1.f, 0.6f, fade} : hv > 0.25f ? Rgba{1.f, 0.78f, 0.3f, fade} : Rgba{1.f, 0.35f, 0.35f, fade};
                    bf::hud::fillBox(f.ctx, x0 + 5.f * k, y0 + H - 3.5f * k, W - 10.f * k, 1.5f * k, {1, 1, 1, 0.15f * fade});
                    bf::hud::fillBox(f.ctx, x0 + 5.f * k, y0 + H - 3.5f * k, (W - 10.f * k) * hv, 1.5f * k, hc);
                }
                return true;
            });
        } catch (...) {}
    };
    r.add("debug_menu", "Debug Menu", Category::Hud, "Java F3-style details").hud = [](Frame& f) {
        if (!f.player) return;
        auto const& p = f.player->getPosition();
        int bx = int(std::floor(p.x)), by = int(std::floor(p.y - 1.62f)), bz = int(std::floor(p.z));
        std::vector<std::string> lines{
            std::format("Bedrock Forge | {} fps", gFps.value),
            std::format("XYZ: {:.3f} / {:.3f} / {:.3f}", p.x, p.y - 1.62f, p.z),
            std::format("Block: {} {} {}", bx, by, bz),
            std::format("Chunk: {} {} in {} {}", bx & 15, bz & 15, bx >> 4, bz >> 4),
            std::format("Facing: {} ({:.1f} / {:.1f})", compass(f.player->getRotation().z), f.player->getRotation().z, f.player->getRotation().x),
            std::format("Dimension: {}", static_cast<int>(f.player->getDimensionId())),
            std::format("Health: {}", f.player->getHealth()),
            std::format("Effects: {}", f.player->getActiveEffectCount()),
            std::format("Speed: {:.2f} b/s", gSpeed.bps),
        };
        panel(f.ctx, 4, 4, lines, 0.5f, {1, 1, 1, 1});
    };

    // ---------------- Combat (display only)
    {   // Target HUD (Purxiy "health and fight stats of whoever you aim at"): the one you last hit, for 4 s
        auto& th = r.add("target_hud", "Target HUD", Category::Combat, "Health and fight stats of whoever you are fighting", false);
        th.slider("size", "Size", 1.0f, 0.5f, 2.0f, 0.05f).slider("opacity", "Background opacity", 0.6f, 0.f, 1.f, 0.05f)
            .slider("x", "X (0 left .. 1 right)", 0.56f, 0.f, 1.f, 0.01f).slider("y", "Y (0 top .. 1 bottom)", 0.56f, 0.f, 1.f, 0.01f);
        th.hud = [](Frame& f) {
            auto* m = Registry::get().find("target_hud");
            if (!m || !f.player) return;
            int64_t const age = nowMs() - gTargetAt.load();
            if (age > 4000 || gTargetId.load() == 0) return;
            Actor* t = nullptr;
            try { ActorUniqueID id; id.rawID = gTargetId.load(); t = f.player->getLevel().fetchEntity(id, false); } catch (...) { t = nullptr; }
            if (!t) return;
            std::string name;
            try { name = t->getNameTag(); } catch (...) {}
            for (size_t i; (i = name.find("\xC2\xA7")) != std::string::npos;) name.erase(i, std::min<size_t>(3, name.size() - i));   // drop colour codes
            if (auto nl = name.find('\n'); nl != std::string::npos) name.resize(nl);
            if (name.empty()) name = "Target";
            int hp = 0, mx = 20;
            try { hp = t->getHealth(); mx = std::max(1, t->getMaxHealth()); } catch (...) {}
            auto const& a = f.player->getPosition(); auto const& b = t->getPosition();
            float const dist = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            float const s = m->num("size", 1.f), W = 120.f * s, H = 34.f * s;
            float const fade = 1.f - smooth01((float(age) - 3400.f) / 600.f);
            float x, y;
            hudedit::place(m->num("x", 0.56f), m->num("y", 0.56f), W, H, f.width, f.height, x, y);
            hudedit::note("target_hud", "Target HUD", "x", "y", "size", x, y, W, H, f.width, f.height);
            bf::hud::fillBox(f.ctx, x, y, W, H, {0.06f, 0.04f, 0.10f, m->num("opacity", 0.6f) * fade});
            bf::hud::fillBox(f.ctx, x, y, 2.f * s, H, {kNeon.r, kNeon.g, kNeon.b, fade});
            bf::hud::drawLabel(f.ctx, x + 6.f * s, y + 3.f * s, name.substr(0, 18), {1, 1, 1, fade}, s);
            float const k = std::clamp(float(hp) / float(mx), 0.f, 1.f), bw = W - 12.f * s;
            bf::hud::fillBox(f.ctx, x + 6.f * s, y + 15.f * s, bw, 4.f * s, {1, 1, 1, 0.15f * fade});
            bf::hud::fillBox(f.ctx, x + 6.f * s, y + 15.f * s, bw * k, 4.f * s, {kNeon.r + (1 - kNeon.r) * k * 0.5f, kNeon.g + (1 - kNeon.g) * k * 0.5f, 1.f, fade});
            bf::hud::drawLabel(f.ctx, x + 6.f * s, y + 22.f * s, std::format("{:.1f} hp  {} hits  {:.1f}m", hp / 2.0, gTargetHits.load(), dist), {0.86f, 0.80f, 0.95f, fade}, 0.75f * s);
        };
    }
    r.add("reach_display", "Reach Display", Category::Combat, "Distance of your last hit").hud = [](Frame& f) {
        if (nowMs() - gLastReachAt.load() > 3000) return;
        f.infoLines.push_back(std::format("Reach {:.2f}", gLastReach.load()));
    };
    r.add("combo", "Combo Counter", Category::Combat, "Hits in a row without taking damage").hud = [](Frame& f) {
        if (gCombo.load() < 2 || nowMs() - gComboAt.load() > 2500) return;
        f.infoLines.push_back(std::format("Combo x{}", gCombo.load()));
    };
    r.add("cps_cap", "CPS Cap", Category::Combat, "Drop clicks above a limit (for servers with CPS rules)")
        .slider("limit", "Max CPS", 15, 5, 30, 1);
    r.add("hit_color", "Hit Color", Category::Combat, "Recolour the red flash on hurt mobs")
        .slider("r", "Red", 0.2f, 0, 1, 0.05f).slider("g", "Green", 0.6f, 0, 1, 0.05f).slider("b", "Blue", 1.0f, 0, 1, 0.05f);
    r.plan("hit_particles", "Hit Particles", Category::Combat, "Extra crit / sharpness particles on every hit");
    r.plan("hitboxes", "Hitboxes", Category::Combat, "Entity bounding boxes (Lamium has this today)");

    // ---------------- Camera
    // [Purxiy "freelook": look around without turning] Renderer Foundation turns the camera round you while the key is
    // held (or toggled); you keep running the way you were facing. No key until you pick one (your controls stay yours).
    r.add("freelook", "Freelook", Category::Camera, "Look around without turning: hold your key and the camera orbits you", false)
        .slider("key", "Freelook key", 0, 0, 254, 1).choice("mode", "Mode", {"Hold", "Toggle"}, 0)
        .slider("distance", "Distance (blocks, 0 = from your eyes)", 4.f, 0.f, 10.f, 0.5f);
    r.add("zoom", "Zoom", Category::Camera, "Hold C to zoom", true)
        .slider("factor", "Zoom factor", 4, 1.5f, 12, 0.5f).slider("key", "Key", 0x43, 8, 254, 1)
        .toggleSetting("smooth", "Smooth", true).toggleSetting("slow_turn", "Slower turning while zoomed", true);
    r.add("fov_changer", "FOV Changer", Category::Camera, "Scale the field of view beyond the settings range")
        .slider("scale", "FOV scale", 1.15f, 0.5f, 1.8f, 0.05f);
    r.add("sensitivity", "Sensitivity Multiplier", Category::Camera, "Multiply mouse sensitivity")
        .slider("multiplier", "Multiplier", 1.0f, 0.1f, 4.0f, 0.05f);
    r.plan("snap_look", "Snap Look", Category::Camera, "Hold a key for third-person front view");
    r.add("view_bobbing", "No View Bobbing", Category::Camera, "Turn off the walking camera bob");
    {   // [Purxiy "hand_movement"] where your held item sits and how big it is
        auto& hm = r.add("hand_movement", "Hand Movement", Category::Camera, "Move and scale the item in your hand", false);
        hm.slider("x", "Left / right", 0.f, -1.f, 1.f, 0.02f).slider("y", "Down / up", 0.f, -1.f, 1.f, 0.02f)
            .slider("z", "Back / forward", 0.f, -1.f, 1.f, 0.02f).slider("scale", "Size", 1.f, 0.4f, 1.6f, 0.02f);
        hm.onToggle = [](bool on) { try { setHandHook(on); } catch (...) {} };
    }
    r.add("no_hurt_cam", "No Hurt Cam", Category::Camera, "No camera tilt when you take damage");
    {
        auto& gl = r.add("glass_frost", "Glass", Category::Visual, "Players, items and armour made of glass: clear as a window, or soft and milky (Renderer Foundation)", false);
        gl.slider("frost", "Frosted (0 clear .. 1 milky)", 0.35f, 0.f, 1.f, 0.05f).slider("glow", "Glow", 0.2f, 0.f, 1.f, 0.05f)
            .toggleSetting("shimmer", "Shimmer (a soft sheen now and then)", true)
            .choice("tint", "Colour", {"White", "Lilac", "Neon purple", "Ice blue", "Rose", "Gold", "Custom"}, 0)
            .slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.5f, 0.f, 1.f, 0.05f)
            .slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .toggleSetting("self", "You (your body in third person, your arm in first person)", false)
            .toggleSetting("players", "Other players", false)
            .toggleSetting("items", "Items (held, dropped and in item frames)", false)
            .toggleSetting("armour", "Armour (off: it keeps its own look)", true);
        gl.hud      = [](Frame&) { if (auto* m = Registry::get().find("glass_frost")) glassTick(*m); };
        gl.onToggle = [](bool on) {
            gGlassLast.clear();
            if (!on) { writeGlassFrost(0.f, 0.f, false, 0); writeGlassBody(false, false, true, false); }   // off: the glass armour as before, no glass body
        };
    }
    {
        auto& hz = r.add("haze", "Haze", Category::Visual, "A soft haze of colour over the world around you (Renderer Foundation)", false);
        hz.choice("palette", "Colour", {"Rose", "Lilac", "Peach", "Sunset", "Mint", "Ocean", "Aurora", "Novak"}, 0)
            .slider("strength", "Strength", 0.45f, 0.1f, 1.0f, 0.05f).slider("height", "Height (blocks above the ground)", 24, 6, 64, 1)
            .slider("clear", "Clear around you", 8, 0, 32, 1).slider("drift", "Drift", 1.0f, 0.f, 3.f, 0.05f)
            .slider("glow", "Glow toward the sun and moon", 0.6f, 0.f, 1.f, 0.05f).toggleSetting("creatures", "Players and mobs keep their colours", true);
        auto& ms = r.add("mist", "Mist", Category::Visual, "Soft autumn mist that hangs low in the world (Renderer Foundation)", false);
        ms.choice("palette", "Colour", {"Autumn", "Lilac", "Pearl", "Rose"}, 0)
            .slider("density", "Density", 0.5f, 0.1f, 1.0f, 0.05f).slider("height", "Height (blocks above the ground)", 10, 3, 32, 1)
            .slider("clear", "Clear around you", 6, 0, 24, 1).slider("drift", "Drift", 1.0f, 0.f, 3.f, 0.05f)
            .slider("rays", "Sun rays", 0.6f, 0.f, 1.f, 0.05f).toggleSetting("creatures", "Players and mobs show clearly through it", true);
        auto& am = r.add("ambience", "Ambience", Category::Visual, "Glowing particles drifting through the world around you (Renderer Foundation)", false);
        am.choice("style", "Style", {"Motes", "Stardust", "Petals", "Snow"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("amount", "Amount", 1.0f, 0.1f, 2.0f, 0.05f).slider("size", "Size", 1.0f, 0.5f, 2.0f, 0.05f)
            .slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f).slider("speed", "Speed", 1.0f, 0.f, 3.f, 0.05f)
            .slider("twinkle", "Twinkle", 0.5f, 0.f, 1.f, 0.05f).slider("range", "Range (blocks)", 24, 8, 40, 1)
            .toggleSetting("depth_of_field", "Near ones melt into soft bokeh", true).toggleSetting("react", "They swirl aside as you move", true)
            .slider("daylight", "How many stay out by day", 0.5f, 0.f, 1.f, 0.05f);
        am.onToggle = [](bool) { gAtmosLast.clear(); };
        // Purxiy hit / jump effects (their settings; "others" needs other players' events: not yet)
        auto& jc = r.add("jump_circles", "Jump Circles", Category::Visual, "A ring of light where you jump (Renderer Foundation)", false);
        jc.choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f).slider("size", "Size", 1.2f, 0.5f, 3.f, 0.05f)
            .slider("duration", "Duration (s)", 1.1f, 0.3f, 3.f, 0.05f).slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f);
        auto& hs = r.add("hit_splash", "Hit Splash", Category::Visual, "Glowing splashes where your hits land (Renderer Foundation)", false);
        hs.choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f).slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f)
            .slider("duration", "Duration (s)", 8.f, 1.f, 20.f, 0.5f).slider("glow", "Glow", 0.8f, 0.f, 1.f, 0.05f)
            .choice("colour_mode", "Colour mode", {"Single", "Gradient", "Neon mix", "Dark"}, 1);
        auto& rp = r.add("ripple", "Ripple", Category::Visual, "A wave of light where every hit lands (Renderer Foundation)", false);
        rp.choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f).slider("glow", "Glow", 0.8f, 0.f, 1.f, 0.05f)
            .slider("duration", "Duration (s)", 0.8f, 0.3f, 2.f, 0.05f).toggleSetting("crits", "A burst of stars on critical hits", true)
            .toggleSetting("ring", "A ring that rolls over the ground", true);
        auto& sw = r.add("shockwave", "Shockwave", Category::Visual, "Land, and a wave of light runs through the ground (Renderer Foundation)", false);
        sw.choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f).slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f)
            .slider("glow", "Glow", 0.7f, 0.f, 1.f, 0.05f);
        // cosmetics (Purxiy trail / aura / halo): seen in third person (F5), the aura first person too if you want
        auto& tr = r.add("trail", "Trail", Category::Visual, "Leave a trail of light behind you (Renderer Foundation)", false);
        tr.choice("style", "Style", {"Line", "Path", "Helix", "Stardust"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("duration", "Duration (s)", 1.2f, 0.3f, 3.f, 0.05f).slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f).slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f);
        auto& au = r.add("aura", "Aura", Category::Visual, "Soft light that lives around you (Renderer Foundation)", false);
        au.choice("style", "Style", {"Orbit", "Fireflies", "Spiral", "Constellation", "Ascension"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f).slider("speed", "Speed", 1.f, 0.f, 3.f, 0.05f).slider("glow", "Glow", 0.65f, 0.f, 1.f, 0.05f)
            .toggleSetting("first_person", "See it from your own eyes too", false);
        auto& ha = r.add("halo", "Halo", Category::Visual, "A gently glowing halo (Renderer Foundation)", false);
        ha.choice("design", "Design", {"Seraph", "Sigil", "Crown", "Stardust", "Eclipse", "Fallen"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f).slider("glow", "Glow", 0.7f, 0.f, 1.f, 0.05f).slider("height", "Height", 1.f, 0.f, 2.f, 0.05f);
        // [Purxiy "wings" / "cape" / "block_outline"] original looks, drawn by Renderer Foundation like the halo
        auto& wgm = r.add("wings", "Wings", Category::Visual, "A pair of softly animated wings (Renderer Foundation)", false);
        wgm.choice("design", "Design", {"Seraph", "Butterfly", "Prism"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("size", "Size", 1.f, 0.5f, 1.6f, 0.05f).slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f).slider("flap", "Flap", 1.f, 0.f, 2.f, 0.05f)
            .toggleSetting("first_person", "See them in first person", false);
        auto& cpm = r.add("cape", "Cape", Category::Visual, "An animated glowing cape that flows as you move (Renderer Foundation)", false);
        cpm.choice("design", "Design", {"Silk", "Aurora", "Starfield"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("glow", "Glow", 0.5f, 0.f, 1.f, 0.05f).slider("sway", "Sway", 1.f, 0.f, 2.f, 0.05f);
        cosmetics::registerModules(r);   // 3D model cosmetics (Knives), Cosmetics tab
        // [Purxiy "main_menu": the world behind the menus] the title screen's panorama, captured from where you stand
        r.add("main_menu", "Main Menu", Category::Visual, "Your own world behind the title screen: stand somewhere beautiful and capture it (shows from the next start)", false)
            .toggleSetting("capture", "Capture this view now", false);
        r.add("cyberspace", "Cyberspace", Category::Visual, "Step into a digital world of light, wires and data (Renderer Foundation)", false)
            .choice("palette", "Colour", {"Cyan", "Accent", "Matrix", "Neon pink"}, 0).slider("reality", "Digital", 0.85f, 0.1f, 1.f, 0.05f)
            .slider("glow", "Glow", 1.f, 0.f, 2.f, 0.05f).slider("circuits", "Circuits", 1.f, 0.f, 1.f, 0.05f)
            .slider("interval", "Scan every (s)", 6.f, 2.f, 20.f, 1.f).toggleSetting("sky", "Data sky", true);
        r.add("cute_world", "Cute World", Category::Visual, "A soft pastel world with drifting hearts and little sparkles (Renderer Foundation)", false)
            .choice("palette", "Hearts", {"Rose", "Lilac", "Peach"}, 0).slider("pastel", "Pastel", 1.f, 0.f, 1.f, 0.05f)
            .slider("softness", "Softness", 0.7f, 0.f, 1.f, 0.05f).slider("hearts", "Hearts", 1.f, 0.f, 1.5f, 0.05f).slider("sparkles", "Sparkles", 1.f, 0.f, 1.f, 0.05f)
            .toggleSetting("eyes", "Anime eyes on players and mobs", true);
        auto& bom = r.add("block_outline", "Block Outline", Category::Visual, "A cleaner, glowing outline on the block you aim at (Renderer Foundation)", false);
        bom.choice("style", "Style", {"Glow", "Clean", "Corners"}, 0).choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f)
            .slider("width", "Width (px)", 2.f, 1.f, 5.f, 0.5f).slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f).slider("fill", "Fill", 0.12f, 0.f, 0.6f, 0.02f)
            .toggleSetting("face", "Light the face you look at", true).toggleSetting("glide", "Glide between blocks", true);
        auto& chm = r.add("chroma", "Chroma", Category::Visual, "The world waits in grey, and colour blooms wherever you walk (Renderer Foundation)", false);
        chm.choice("tone", "Tone", {"Lilac", "Cream", "Noir"}, 0).slider("hint", "Colour it keeps", 0.06f, 0.f, 0.5f, 0.01f)
            .toggleSetting("living", "Players, mobs and items keep their colour", true).slider("brush", "Brush (blocks)", 6.f, 2.f, 16.f, 0.5f)
            .choice("memory", "Memory", {"30 s", "2 min", "10 min", "1 hour"}, 1);
        auto& rad = r.add("radiance", "Radiance", Category::Visual, "Glowing outlines on players, mobs and items (Renderer Foundation)", false);
        rad.choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f).slider("width", "Width", 2.f, 1.f, 5.f, 0.5f)
            .slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f).slider("fill", "Fill", 0.2f, 0.f, 1.f, 0.05f).slider("speed", "Flow speed", 1.f, 0.f, 3.f, 0.05f);
        auto& wxm = r.add("weather", "Weather", Category::Visual, "Sculpt the weather: glassy rain, soft snowfall and ember showers (Renderer Foundation)", false);
        wxm.choice("mode", "Mode", {"Rain", "Snow", "Fire"}, 0).slider("intensity", "Intensity", 1.f, 0.1f, 2.f, 0.05f)
            .slider("wind", "Wind", 0.45f, 0.f, 1.5f, 0.05f).slider("size", "Size", 1.f, 0.5f, 2.f, 0.05f).slider("range", "Range (blocks)", 24.f, 8.f, 40.f, 1.f)
            .slider("wet", "Wet ground (rain)", 0.6f, 0.f, 1.f, 0.05f).toggleSetting("lightning", "Lightning (rain)", true)
            .slider("sounds", "Sounds (rain, thunder)", 0.5f, 0.f, 1.f, 0.05f);
        auto& bkm = r.add("bokeh", "Bokeh", Category::Visual, "Sharp where you look, creamy light behind (Renderer Foundation)", false);
        bkm.choice("focus", "Focus", {"Auto", "Distance", "Tilt-shift"}, 0).slider("distance", "Sharp up to (blocks, Distance)", 24.f, 4.f, 96.f, 1.f)
            .slider("band", "How much stays sharp", 0.26f, 0.05f, 1.f, 0.01f).slider("blur", "Blur", 0.5f, 0.1f, 1.f, 0.05f)
            .slider("highlights", "Highlights", 0.6f, 0.f, 1.f, 0.05f).toggleSetting("near", "Nearer than the focus blurs too", false)
            .slider("fringe", "Lilac fringe", 0.45f, 0.f, 1.f, 0.05f);
        // (the block itself cannot grow / pop: its geometry is the game's chunk mesh - only the light is ours)
        auto& pam = r.add("place_animation", "Place Animation", Category::Visual, "A soft light on blocks as you build and break (Renderer Foundation)", false);
        pam.choice("palette", "Palette", {"Accent", "Orchid", "Dream", "Custom"}, 0).slider("custom_hue", "Custom colour", 280.f, 0.f, 360.f, 5.f).slider("custom_sat", "Custom: how strong", 0.8f, 0.f, 1.f, 0.05f).slider("custom_bright", "Custom: brightness", 1.f, 0.2f, 1.f, 0.05f).slider("duration", "Duration (s)", 0.34f, 0.15f, 1.f, 0.01f)
            .slider("glow", "Glow", 0.6f, 0.f, 1.f, 0.05f).toggleSetting("breaking", "Blocks you break too", true);
        for (Module* m : {&jc, &hs, &rp, &sw, &tr, &au, &ha, &chm, &rad, &wxm, &bkm, &pam}) m->onToggle = [](bool) { gAtmosLast.clear(); };
        hz.onToggle = [](bool) { gAtmosLast.clear(); };
        ms.onToggle = [](bool) { gAtmosLast.clear(); };
    }
    {
        auto& hh = r.add("hud_hider", "HUD Hider", Category::Hud, "Choose which Minecraft HUD elements to hide", false);
        for (auto const& g : hideGroups()) hh.toggleSetting(g.id, g.label, false);
    }
    // (2026-11-23, Fernando: "two versions of the mini player with LyricsForge on - remove the second, keep LyricsForge's
    //  own") The separate "LyricsForge Player" HUD module (player card bottom-right + lyrics top-middle) is gone; the
    //  player, lyrics and top bar of LyricsForge itself (toggles in its panel) are the only ones.
    r.add("creamy_keys", "Keyboard Sounds", Category::Misc, "Your keyboard, with a sound of its own", false)
        .choice("profile", "Sound", {"Silk", "Stone", "Glass"}, 0).slider("volume", "Volume", 0.6f, 0.f, 1.f, 0.05f);
    r.add("nick_hider", "Nick Hider", Category::Misc, "Hide your name in screenshots and streams (chat, the player list)", false)
        .choice("alias", "Show it as", {"You", "Player", "Hidden", "Novak"}, 0);
    r.add("motion_blur", "Motion Blur", Category::Camera, "Smooth camera blur when you turn and move. With a World FX sky on, your hand and the HUD stay sharp (drawn by Renderer Foundation)")
        .choice("style", "Style", {"Smooth", "Streak"}, 0)
        .slider("strength", "Strength", 1.0f, 0.1f, 3.0f, 0.05f)
        .slider("moving", "Blur from moving (Streak; turning is always full)", 0.6f, 0.0f, 1.5f, 0.05f);

    // ---------------- Visual
    r.add("fullbright", "Fullbright", Category::Visual, "See in the dark: lifts the light baked into nearby terrain (client-side, relights chunks on toggle)")
        .slider("strength", "Strength", 1.0f, 0.1f, 1.0f, 0.05f);
    r.add("pvp_preset", "PvP Preset", Category::Visual,
          "Turns Smooth Lighting off while on (cheaper chunk building: fewer stutters when chunks load); your own setting comes back when you turn it off")
        .toggleSetting("restore_smooth", "Smooth Lighting when the preset is off", true);
    r.add("time_changer", "Time Changer", Category::Visual, "Client-side time of day (rendering only)")
        .slider("time", "Time of day (0-1)", 0.0f, 0, 1, 0.01f);
    r.plan("fog", "Fog", Category::Visual, "Remove or recolour fog");
    r.plan("low_fire", "Low Fire", Category::Visual, "Smaller first-person fire overlay");
    r.plan("chunk_borders", "Chunk Borders", Category::Visual, "Chunk edges (Lamium has this today)");
    r.add("post_processing", "Post Processing", Category::Visual, "Bloom, colour fringe and sharpening on the whole frame (overrides the shaderpack's)")
        .slider("bloom", "Bloom", 0.12f, 0.0f, 0.5f, 0.01f)
        .slider("sharpen", "Sharpen", 0.5f, 0.0f, 1.0f, 0.05f)
        .slider("ca", "Colour fringe", 0.0f, 0.0f, 10.0f, 0.5f)
        .choice("ca_style", "Fringe style", {"Edges", "Toward centre"}, 0);
    r.plan("dark_mode", "Dark Mode", Category::Visual, "Dark UI theme");
    r.plan("gui_scale", "GUI Scale", Category::Visual, "Finer GUI scale steps");
    r.plan("projectile_scaler", "Projectile Scaler", Category::Visual, "Bigger arrows / pearls / snowballs");
    r.plan("no_death_rotation", "No Death Rotation", Category::Visual, "Mobs fall flat instead of rotating");
    r.plan("left_hand", "Left Hand", Category::Visual, "Render the held item in the left hand");

    // ---------------- Movement / input
    r.add("scroll_disabler", "Scroll Wheel Disabler", Category::Movement, "Stop the wheel changing hotbar slots");
    r.add("toggle_sprint", "Auto Sprint", Category::Movement, "Always sprint when you walk forward (same as holding the sprint key)");
    r.plan("toggle_sneak", "Auto Sneak", Category::Movement, "Toggle sneaking (Lamium: Permanent Sneak)");
    r.plan("null_movement", "Null Movement", Category::Movement, "Last pressed of A/D (W/S) wins");
    r.plan("modern_keyboard", "Modern Keyboard Handling", Category::Movement, "Java-like key repeat / chat input");

    // ---------------- World / server
    r.plan("dc_prevent", "DC Prevent", Category::World, "Confirm before disconnecting");
    r.plan("pack_switcher", "Pack Switcher", Category::World, "Switch resource packs without leaving the world");
    r.plan("waypoints", "Waypoints", Category::World, "Saved points in the world (Lamium has this today)");
    r.plan("minimap", "Minimap", Category::World, "Minimap + radar (Lamium has this today)");
    r.add("auto_gg", "Auto GG", Category::World, "Says gg in chat when a game ends (Hive, CubeCraft, Lifeboat, Zeqa, Galaxite, Mineville)")
        .choice("message", "Message", serverauto::ggMessages(), 0)
        .slider("delay", "Delay (s)", 1.0f, 0.0f, 5.0f, 0.5f);
    r.add("auto_requeue", "Auto Re-queue", Category::World, "Joins the next game of the same mode when one ends (The Hive)")
        .slider("delay", "Delay (s)", 2.0f, 0.5f, 10.0f, 0.5f)
        .toggleSetting("ondeath", "Also when you die", false);
    r.plan("replay", "Replay", Category::World, "Record and replay gameplay");
    r.plan("proximity_chat", "Proximity Chat", Category::World, "Voice/text by distance");

    // ---------------- Misc
    r.add("chat_compact", "Chat Compact", Category::Misc, "Stack repeated chat messages as (x3)", true);
    r.add("hud_layout", "HUD Layout", Category::Misc, "Where the info panel sits and how dark it is", true)
        .slider("x", "Info panel X (negative = from right)", 4, -400, 400, 2)
        .slider("y", "Info panel Y (negative = from bottom)", 96, -300, 300, 2)
        .slider("opacity", "Background opacity", 0.42f, 0, 1, 0.05f);
    r.plan("movable_hud", "Movable HUD", Category::Misc, "Drag scoreboard, boss bar, chat, paper doll");
    r.plan("cosmetics", "Cosmetics", Category::Misc, "Client-side capes and cloaks");
    r.plan("skin_grabber", "Skin Grabber", Category::Misc, "Save a player's public skin");
    r.plan("auto_hotbar", "Auto Hotbar", Category::Misc, "Refill hotbar slots from inventory");
    // the glass client menu's own look (Settings page of the menu; hidden from the module grid)
    r.add("client_ui", "Client UI", Category::Misc, "Glass menu look", true)
        .slider("blur", "Background blur", 1, 0, 2, 0.05f)
        .slider("refraction", "Glass refraction", 1, 0, 2.5f, 0.05f)
        .slider("frost", "Frost tint", 1, 0, 2, 0.05f)
        .toggleSetting("wheel", "Open with the wheel", true)
        .toggleSetting("worldblur", "Blur the world", true)
        .toggleSetting("camswing", "Camera swing", true)
        // [Purxiy "interface" / "motion"] your colour across the menu and the HUD elements, and how fast things animate
        .choice("accent", "Accent colour", {"Novak purple", "Rose", "Ocean", "Mint", "Sunset", "Ice", "Gold", "Cherry"}, 0)
        .slider("motion", "Animation speed", 1.0f, 0.5f, 2.0f, 0.05f)
        .choice("font", "Font", {"Outfit", "Inter"}, 0)
        // [Prism] Purxiy "interface" / "motion" / "sound": the look, the world behind the menu, cascade, interface sounds
        .choice("style", "Look", {"Prism", "Glass"}, 0)
        .slider("scale", "Menu scale", 1.0f, 0.75f, 1.4f, 0.05f)
        .slider("dim", "Dim the world", 0.25f, 0.f, 1.f, 0.05f)
        .slider("stars", "Stars", 0.5f, 0.f, 1.f, 0.05f)
        .toggleSetting("cascade", "Cascade", true)
        .toggleSetting("reduce_motion", "Reduce motion", false)
        .toggleSetting("sounds", "Interface sounds", true)
        .toggleSetting("hover_sounds", "Hover sounds", true)
        .toggleSetting("slider_ticks", "Slider ticks", true)
        .slider("sound_volume", "Interface sound volume", 0.5f, 0.f, 1.f, 0.05f);
}

// ============================================================================ PvP preset
// [BF] manual toggle (never automatic): Smooth Lighting off while the module is on. The game option is set without
// saving, so options.txt keeps your own value; what to restore is also kept in the module ("restore_smooth").
namespace pvppreset {
bool gApplied = false, gFirst = true;
BoolOption* smooth(IClientInstance& c) {
    auto o = c.getOptions().getIfValid(OptionID::SmoothLighting);
    return o && *o ? static_cast<BoolOption*>(*o) : nullptr;
}
void tick(IClientInstance& c) {
    auto* m = Registry::get().find("pvp_preset");
    bool const want = m && m->enabled;
    if (want == gApplied) {
        if (want) if (auto* b = smooth(c); b && bool(b->mValue)) b->set(false, false); // something turned it back on
        gFirst = false;
        return;
    }
    auto* b = smooth(c);
    if (!b) return;
    if (want) {
        // turned on now: remember your setting. Already on at startup: the game may have saved our "off" last time,
        // so the value kept from when you turned it on wins.
        if (!gFirst) if (auto* st = m->setting("restore_smooth")) st->on = bool(b->mValue);
        Registry::get().save();
        b->set(false, false);
    } else {
        bool back = true;
        if (m) if (auto* st = m->setting("restore_smooth")) back = st->on;
        b->set(back, false);
    }
    gApplied = want;
    gFirst   = false;
}
} // namespace pvppreset

// ============================================================================ hitch recorder
// [BF] logs/hitches.txt: every HUD-to-HUD frame over 30 ms, with what our code did in the frame before it (reports,
// fullbright, forgerender, serverauto, the slowest module hud + total, the panel) and how many tessellator calls ran
// in between (chunk building). Our work small + many tess calls = chunks; both small = the game / GPU / the PC.
namespace hitch {
struct Cost { double reports = 0, fb = 0, fr = 0, sa = 0, mods = 0, modMax = 0, panel = 0; std::string modName; };
Cost gLast, gCur;
std::chrono::steady_clock::time_point gPrev{};
uint64_t gTessPrev = 0;
int gLines = 0;
double since(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
std::filesystem::path file() {
    std::error_code ec;
    auto dir = ll::mod::NativeMod::current()->getModDir() / "logs";
    std::filesystem::create_directories(dir, ec);
    return dir / "hitches.txt";
}
void check(bool inGame) {
    auto const now = std::chrono::steady_clock::now();
    uint64_t const tess = diag::tessLight.load();
    bool const had = gPrev.time_since_epoch().count() != 0;
    double const dt = had ? std::chrono::duration<double, std::milli>(now - gPrev).count() : 0;
    if (!had) { std::ofstream(file(), std::ios::app | std::ios::binary) << "---- session (times local; ms = our work in the frame before the hitch)\n"; }
    // in a menu (cursor showing) a long frame isn't a gameplay hitch
    // 30 ms catches the small repeating hiccups too; over 3 s is a pause / loading gap (no HUD frames), not a hitch
    if (had && dt > 30.0 && dt < 3000.0 && inGame && gLines < 5000) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
        double const ours = gLast.reports + gLast.fb + gLast.fr + gLast.sa + gLast.mods + gLast.panel;
        std::ofstream out(file(), std::ios::app | std::ios::binary);
        out << std::format("{:02}:{:02}:{:02}.{:03} frame {:6.1f} ms | ours {:5.2f} (reports {:.2f} fb {:.2f} forgerender {:.2f} serverauto {:.2f} "
                           "modules {:.2f} max {} {:.2f} panel {:.2f}) | tess {}\n",
                           tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms), dt, ours, gLast.reports, gLast.fb, gLast.fr, gLast.sa, gLast.mods,
                           gLast.modName.empty() ? "-" : gLast.modName, gLast.modMax, gLast.panel, tess - gTessPrev);
        ++gLines;
    }
    gPrev = now;
    gTessPrev = tess;
}
void mark(char const* what) { // [BF] a gameplay moment (you got hit), so hitches around a fight can be lined up
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
    std::ofstream(file(), std::ios::app | std::ios::binary) << std::format("{:02}:{:02}:{:02}.{:03} ** {}\n", tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms), what);
}
} // namespace hitch

// ============================================================================ runtime
void installRuntime() {
    if (Registry::get().on("hand_movement")) { try { setHandHook(true); } catch (...) {} }   // switched on last time
    gSubs.on<ll::event::BeforeUIRenderEvent>([](ll::event::BeforeUIRenderEvent& ev) {   // HUD Hider: before the HUD draws
        if (!bf::hud::isHudScreen(ev.screenView())) return;
        try { hudHiderFrame(ev.screenView()); } catch (...) {}
    });
    gSubs.on<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
        try { onModuleKey(ev.keyCode() == 0x10 ? (w32::keyDown(0xA1) ? 0xA1 : 0xA0) : ev.keyCode(), ev.isDown()); } catch (...) {}
    });
    installVisualHooks();
    installChatHooks();
    serverauto::install();
    matchfx::install();
    BfFovHook::hook();
    BfTurnHook::hook();
    BfCrosshairHook::hook();
    BfAttackHook::hook();
    BfSurvivalAttackHook::hook();

    // Clicks: counted from the game's own mouse events (no per-frame sampling misses), CPS cap + wheel disabler.
    gSubs.on<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
        char const  action = ev.actionButtonId();
        schar const data   = ev.buttonData();
        if (menu::isOpen()) return;
        if ((action == MouseAction::ActionLeft || action == MouseAction::ActionRight) && data == MouseAction::DataDown) {
            std::lock_guard lock(gMutex);
            auto&           q = action == MouseAction::ActionLeft ? gLeftClicks : gRightClicks;
            if (action == MouseAction::ActionLeft && Registry::get().on("cps_cap") && hudInputAllowed()
                && clicksPerSecond(q) >= int(Registry::get().find("cps_cap")->num("limit", 15))) {
                ev.cancel();
                return;
            }
            q.push_back(nowMs());
        }
        if (action == MouseAction::ActionWheel && data != 0 && Registry::get().on("scroll_disabler") && hudInputAllowed()) ev.cancel();
    });

    // Combo resets when the local player is hurt.
    gSubs.on<ll::event::ActorHurtEvent>([](ll::event::ActorHurtEvent& ev) {
        if (gLocalId.load() != 0 && ev.self().getOrCreateUniqueID().rawID == gLocalId.load()) {
            gCombo = 0;
            ::bf::lyrics::win::noteCombat();
        }
    });

    gSubs.on<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
        auto& view = ev.screenView();
        if (!bf::hud::isHudScreen(view)) {
            gLastNonHudFrameMs = nowMs();
            return;
        }
        gFps.frame();
        noteRenderThread();
        try { hitch::check(w32::cursorHidden() && !menu::isOpen()); } catch (...) {}
        hitch::gLast = hitch::gCur;
        hitch::gCur  = {};
        auto tSec = std::chrono::steady_clock::now();
        static int hudFrames = 0;
        if (++hudFrames == 120) shaderpacks::reportOnce(); // ~2 s into the first world
        if (hudFrames % 600 == 0) { diag::write(); shaderpacks::writeReport(); }
        hitch::gCur.reports = hitch::since(tSec); tSec = std::chrono::steady_clock::now();
        auto& ctx    = ev.uiRenderContext();
        fullbrightTick(ctx.mClient);
        try { pvppreset::tick(ctx.mClient); } catch (...) {}
        hitch::gCur.fb = hitch::since(tSec); tSec = std::chrono::steady_clock::now();
        forgerender::tick();
        try { atmosTick(); } catch (...) {}
        try { cost::tick(); } catch (...) {}   // [BF] FPS cost badge measurements
        refreshAccent();
        hitch::gCur.fr = hitch::since(tSec); tSec = std::chrono::steady_clock::now();
        auto* player = ctx.mClient.getLocalPlayer();
        gLocalPlayer = static_cast<void*>(static_cast<Player*>(player));
        if (player) gLocalId = player->getOrCreateUniqueID().rawID;
        if (player) { try { std::string const n = player->getRealName(); std::lock_guard l(gNickMutex); if (n != gNickReal) gNickReal = n; } catch (...) {} }
        if (player) postfx::hudFrame(player->getRotation().x, player->getRotation().z);
        if (player) {   // Jump Circles / Shockwave: leaving the ground upward, landing from a fall
            static bool wasGround = true;
            static float fallV = 0.f;
            try {
                bool const g = player->isOnGround();
                Vec3 const pos = player->getPosition();
                float const vy = player->getPosDelta().y;
                if (!g) fallV = std::min(fallV, vy);
                if (wasGround && !g && vy > 0.15f) pushFxEvent(1u, pos.x, pos.y - 1.62f, pos.z);
                if (!wasGround && g) { if (fallV < -0.45f) pushFxEvent(4u, pos.x, pos.y - 1.62f, pos.z, -fallV); fallV = 0.f; }
                wasGround = g;
                // where the game draws you this frame: it blends the last two tick states by the frame's place between
                // ticks. The ticks are timed here (a new tick = a new position or body yaw), the blend follows them.
                static Vec3    tPrev = pos, tCur = pos;
                static float   yPrev = 0.f, yCur = 0.f;
                static bool    init = false;
                // The game ticks every 50 ms on its own clock and draws you at prev + (cur - prev) * (time since that tick
                // / 50 ms). We only SEE a tick at the first frame after it - up to a whole frame late, a different
                // lateness every tick - and timing the blend from that sighting made the wings / cape / halo / trail
                // shake against the body (Fernando 2026-11-19: "still stutter while moving"). The ticks' true phase is
                // the EARLIEST sighting within the 50 ms cycle over the last couple of seconds: lock to that.
                constexpr double kTick = 50.0;
                static double  phase = 0.0;            // tick boundaries are at times == phase (mod 50 ms)
                static double  late[40];               // how far after `phase` each recent sighting came (ms, -25..25)
                static int     lateN = 0;
                double const   nowD = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
                float const    body = player->getYBodyRotation();
                if (!init) { tPrev = tCur = pos; yPrev = yCur = body; phase = std::fmod(nowD, kTick); init = true; }
                bool const moved = pos.x != tCur.x || pos.y != tCur.y || pos.z != tCur.z || body != yCur;
                if (moved) {
                    bool const jump = (pos.x - tCur.x) * (pos.x - tCur.x) + (pos.y - tCur.y) * (pos.y - tCur.y) + (pos.z - tCur.z) * (pos.z - tCur.z) > 64.f;
                    tPrev = jump ? pos : tCur, yPrev = jump ? body : yCur;   // teleported: no glide across the map
                    tCur = pos, yCur = body;
                    double d = std::fmod(nowD - phase, kTick);               // this sighting, relative to the phase we hold
                    if (d < 0) d += kTick;
                    if (d > kTick * 0.5) d -= kTick;
                    late[lateN++ % 40] = d;
                    double lo = 1e9;
                    for (int i = 0; i < std::min(lateN, 40); ++i) lo = std::min(lo, late[i]);
                    if (lateN >= 4 && std::fabs(lo) > 0.25) {                // slide the phase onto the earliest sighting
                        phase += lo;
                        for (int i = 0; i < std::min(lateN, 40); ++i) late[i] -= lo;
                    }
                }
                static long long seenTick = 0;         // the tick (counted on the locked clock) we last saw you move in
                long long const  tickNow = (long long)std::floor((nowD - phase) / kTick);
                if (moved) seenTick = tickNow;
                else if (tickNow > seenTick) { tPrev = tCur; yPrev = yCur; }   // a tick went by and you did not move: you stand there
                double sinceTick = std::fmod(nowD - phase, kTick);
                if (sinceTick < 0) sinceTick += kTick;
                float const tickMs = float(kTick);
                float const a = std::clamp(float(sinceTick / kTick), 0.f, 1.f);
                float dy = std::fmod(yCur - yPrev + 540.f, 360.f) - 180.f;   // the short way round
                gFxEv.px = tPrev.x + (tCur.x - tPrev.x) * a;
                gFxEv.py = tPrev.y + (tCur.y - tPrev.y) * a - 1.62f;
                gFxEv.pz = tPrev.z + (tCur.z - tPrev.z) * a;
                gFxEv.pyaw = yPrev + dy * a;
                float const perS = 1000.f / std::max(tickMs, 1.f);
                gFxEv.pvx = (tCur.x - tPrev.x) * perS, gFxEv.pvy = (tCur.y - tPrev.y) * perS, gFxEv.pvz = (tCur.z - tPrev.z) * perS;
                gFxEv.pyawRate = dy * perS;
                gFxEv.pflags = 1u | (g ? 2u : 0u);
                gFxEv.pAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            } catch (...) {}
            try {   // [test] config/test.txt: one command, run once, then renamed test.done.txt - so effects can be checked
                    // remotely without a mouse: hit | crit | swing | hitswing | jump
                static int64_t lastCheck = 0;
                if (nowMs() - lastCheck > 500) {
                    lastCheck = nowMs();
                    auto const dir = ll::mod::NativeMod::current()->getModDir() / "config";
                    std::error_code ec;
                    if (std::filesystem::exists(dir / "test.txt", ec)) {
                        std::string c;
                        { std::ifstream in(dir / "test.txt"); std::getline(in, c); }
                        std::filesystem::remove(dir / "test.done.txt", ec);
                        std::filesystem::rename(dir / "test.txt", dir / "test.done.txt", ec);
                        while (!c.empty() && (c.back() == '\r' || c.back() == ' ')) c.pop_back();
                        Vec3 const pos = player->getPosition();
                        float const yw = player->getRotation().y * 3.14159265f / 180.f;
                        float const hx = pos.x - std::sin(yw) * 2.f, hz = pos.z + std::cos(yw) * 2.f;   // 2 blocks ahead, chest high
                        if (c == "hit" || c == "crit" || c == "hitswing") {
                            pushFxEvent(c == "crit" ? 3u : 2u, hx, pos.y - 0.5f, hz);
                            cosmetics::noteLocalHit(c == "crit");
                        }
                        if (c == "swing" || c == "hitswing") cosmetics::testSwing();
                        if (c == "jump") pushFxEvent(1u, pos.x, pos.y - 1.62f, pos.z);
                        {   // set <module> <setting> <value> | on <module> | off <module>: flip settings for a remote check
                            std::istringstream ss(c);
                            std::string verb, mid, sid; float v = 0;
                            ss >> verb >> mid;
                            if (auto* md = Registry::get().find(mid)) {
                                if (verb == "on" || verb == "off") {
                                    bool const want = verb == "on";
                                    if (md->enabled != want) { md->enabled = want; if (md->onToggle) md->onToggle(want); }
                                } else if (verb == "set" && (ss >> sid >> v)) {
                                    if (auto* st = md->setting(sid)) { if (st->kind == Setting::Kind::Toggle) st->on = v != 0; else st->value = v; }
                                }
                            }
                        }
                        ll::mod::NativeMod::current()->getLogger().info("[BF] test command: {}", c);
                    }
                }
            } catch (...) {}
            try {   // Block Outline: the block you aim at (only while the module is on)
                gFxEv.bface = 255u;
                if (Registry::get().on("block_outline")) {
                    auto const& hr = player->getLevel().getHitResult();
                    if (hr.mType == HitResultType::Tile) {
                        gFxEv.bx = hr.mBlock.x; gFxEv.by = hr.mBlock.y; gFxEv.bz = hr.mBlock.z; gFxEv.bface = hr.mFacing;
                    }
                }
                gFxEv.bAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            } catch (...) {}
        }
        // (2026-11-20 audit: Radiance, Ripple's body glass and Bokeh's subject focus all read this list of nearby bodies too -
        //  it was only sent with Motion Blur or Cute World on, so Radiance "barely worked")
        auto& R = Registry::get();
        if (player && (R.on("motion_blur") || R.on("cute_world") || R.on("radiance") || R.on("ripple") || R.on("bokeh") || R.on("hit_splash"))) { try { matchfx::blurActorsFrame(*player, R.on("cute_world"), R.on("radiance") || R.on("ripple") || R.on("bokeh") || R.on("hit_splash")); } catch (...) {} }
        if (player) { // [BF] hitch log: health drops = you took a hit (servers send health to the client)
            static int lastHp = -1;
            int const hp = player->getHealth();
            if (lastHp >= 0 && hp < lastHp && hp > 0) {
                ::bf::lyrics::win::noteCombat(); // [LyricsForge] servers don't fire the hurt event client-side: health drops do
                try { hitch::mark(std::format("hit: hp {} -> {}", lastHp, hp).c_str()); } catch (...) {}
            }
            else if (lastHp > 0 && hp <= 0) { try { hitch::mark("died"); } catch (...) {} }
            lastHp = hp;
        }
        tSec = std::chrono::steady_clock::now();
        try { matchfx::frame(ctx.mClient, player); } catch (...) {}
        try { cosmetics::frame(player); } catch (...) {}
        try { serverauto::frame(ctx.mClient, player, hudInputAllowed() && !menu::isOpen(), w32::gameFocused() && !menu::isOpen()); } catch (...) {}
        hitch::gCur.sa = hitch::since(tSec);

        std::vector<std::string> info;
        Frame f{ctx, view, ctx.mClient, player, view.mSize->x, view.mSize->y, info};
        for (auto& m : Registry::get().all())
            if (m->enabled && !m->planned && m->hud) {
                auto const tm0 = std::chrono::steady_clock::now();
                try { m->hud(f); } catch (...) {}
                double const c = hitch::since(tm0);
                hitch::gCur.mods += c;
                if (c > hitch::gCur.modMax) { hitch::gCur.modMax = c; hitch::gCur.modName = m->id; }
            }
        tSec = std::chrono::steady_clock::now();
        auto* lay = Registry::get().find("hud_layout");
        float ix = lay ? lay->num("x", 4) : 4, iy = lay ? lay->num("y", 96) : 96;
        if (ix < 0) ix += view.mSize->x - 100;
        if (iy < 0) iy += view.mSize->y;
        panel(ctx, ix, iy, info, lay ? lay->num("opacity", 0.42f) : 0.42f, {1, 1, 1, 1});
        if (!info.empty()) {   // the info panel's box for the HUD editor (its x / y are pixels, negative = from the right / bottom)
            float w = 0;
            for (auto& l : info) w = std::max(w, textWidth(l));
            hudedit::note("hud_layout", "Info panel", "x", "y", "", ix - 3, iy - 2, w + 4, 10.5f * float(info.size()) + 3, view.mSize->x, view.mSize->y, true);
            gInfoRect[0] = ix - 3; gInfoRect[1] = iy - 2; gInfoRect[2] = w + 4; gInfoRect[3] = 10.5f * float(info.size()) + 3;
        } else gInfoRect[2] = 0;
        hitch::gCur.panel = hitch::since(tSec);
    });
}

void uninstallRuntime() {
    uninstallVisualHooks();
    uninstallChatHooks();
    serverauto::uninstall();
    gSubs.clear();
    BfFovHook::unhook();
    BfTurnHook::unhook();
    BfCrosshairHook::unhook();
    BfAttackHook::unhook();
    BfSurvivalAttackHook::unhook();
}

} // namespace bedrock_forge::client
