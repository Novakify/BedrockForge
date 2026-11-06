// Bedrock Forge client - built-in modules + the runtime that drives them.
// Feature set modelled on what Blueberry-class PvP clients offer (docs/REFERENCE_CLIENT_ANALYSIS.md),
// implemented from scratch on LeviLamina. Display/quality-of-life only: no module gives a gameplay advantage
// that vanilla doesn't (no reach/aim/velocity changes).

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
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
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
#include "mc/world/level/Level.h"

#include "bf/Events.h"
#include "bf/Hud.h"
#include "client/Menu.h"
#include "client/ForgeRender.h"
#include "client/ShaderPacks.h"
#include "client/PostFx.h"
#include "client/ServerAuto.h"
#include "core/Win32Input.h"
#include "lyrics/win/Win.h"
namespace bf::lyrics::win { float cinemaFovScale(); } // LyricsForge Director "FOV breath" (1 = off)

namespace bedrock_forge::client {

void installVisualHooks();
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
std::atomic<int>      gCombo{0};
std::atomic<int64_t>  gComboAt{0};
std::atomic<float>    gZoomNow{1.0f};
int64_t               gSessionStart = nowMs();

struct Fps {
    int64_t start = nowMs();
    int     frames = 0, value = 0;
    void    frame() {
        ++frames;
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
    (void)target;
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
        << "overlay hook " << overlayHook.load() << "\nbob hook " << bobHook.load() << "\ntime hook " << timeHook.load()
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
    r.add("fps", "FPS", Category::Hud, "Frames per second", true).hud = [](Frame& f) {
        f.infoLines.push_back(std::format("FPS {}", gFps.value));
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
    r.add("direction", "Direction", Category::Hud, "Facing and yaw", true).hud = [](Frame& f) {
        if (!f.player) return;
        float yaw = f.player->getRotation().z;
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
    r.add("session", "Session Timer", Category::Hud, "Time since the game started").hud = [](Frame& f) {
        int64_t s = (nowMs() - gSessionStart) / 1000;
        f.infoLines.push_back(std::format("Session {}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60));
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
        .slider("x", "X (negative = from right)", 4, -400, 400, 2)
        .slider("y", "Y (negative = from bottom)", -76, -400, 400, 2)
        .hud = [](Frame& f) {
        auto* m  = Registry::get().find("keystrokes");
        float k  = 18, g = 2, wTot = 3 * k + 2 * g;
        float sx = m->num("x"), sy = m->num("y");
        float bx = sx >= 0 ? sx : f.width + sx - wTot, by = sy >= 0 ? sy : f.height + sy;
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
    r.add("armor_hud", "Armor HUD", Category::Hud, "Worn armor and held item with durability used").hud = [](Frame& f) {
        if (!f.player) return;
        std::vector<std::string> lines;
        using Slot = SharedTypes::Legacy::ArmorSlot;
        for (Slot s : {Slot::Head, Slot::Torso, Slot::Legs, Slot::Feet}) {
            auto const& st = f.player->getArmor(s);
            if (st.isNull()) continue;
            lines.push_back(st.isDamageableItem() ? std::format("{} (-{})", st.getName(), st.getDamageValue()) : st.getName());
        }
        auto const& held = f.player->getSelectedItem();
        if (!held.isNull()) lines.push_back(std::format("> {}", held.getName()));
        panel(f.ctx, f.width - 4 - [&] { float w = 0; for (auto& l : lines) w = std::max(w, textWidth(l)); return w; }(), f.height / 2 - 20, lines, 0.35f, {1, 1, 1, 1});
    };
    r.add("potion_hud", "Potion HUD", Category::Hud, "Active effects, level and time left").hud = [](Frame& f) {
        if (!f.player) return;
        std::vector<std::string> lines;
        for (auto const& e : f.player->getAllEffects()) {
            int ticks = e.mDuration->mValue, secs = ticks / 20;
            lines.push_back(std::format("{} {} {}:{:02}", e.getDisplayName(), roman(e.mAmplifier + 1), secs / 60, secs % 60));
        }
        panel(f.ctx, 4, f.height / 2 - 10, lines, 0.35f, {0.85f, 0.9f, 1, 1});
    };
    r.add("player_list", "List Players", Category::Hud, "Everyone on the server (top right)").hud = [](Frame& f) {
        if (!f.player) return;
        std::vector<std::string> lines{"Players"};
        for (auto const& [uuid, entry] : f.player->getLevel().getPlayerList()) {
            (void)uuid;
            lines.push_back(*entry.mName);
            if (lines.size() > 20) break;
        }
        float w = 0;
        for (auto& l : lines) w = std::max(w, textWidth(l));
        panel(f.ctx, f.width - w - 4, 4, lines, 0.35f, {1, 1, 1, 1});
    };
    r.add("crosshair", "Crosshair", Category::Hud, "A custom crosshair drawn over the vanilla one")
        .slider("size", "Size", 4, 1, 12, 1).slider("gap", "Gap", 1, 0, 6, 1).toggleSetting("dot", "Center dot", true)
        .hud = [](Frame& f) {
        auto* m  = Registry::get().find("crosshair");
        float cx = f.width / 2, cy = f.height / 2, s = m->num("size", 4), g = m->num("gap", 1);
        Rgba  c{0.4f, 1.0f, 0.9f, 0.95f};
        bf::hud::fillBox(f.ctx, cx - g - s, cy - 0.5f, s, 1, c);
        bf::hud::fillBox(f.ctx, cx + g, cy - 0.5f, s, 1, c);
        bf::hud::fillBox(f.ctx, cx - 0.5f, cy - g - s, 1, s, c);
        bf::hud::fillBox(f.ctx, cx - 0.5f, cy + g, 1, s, c);
        if (m->flag("dot", true)) bf::hud::fillBox(f.ctx, cx - 0.5f, cy - 0.5f, 1, 1, c);
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
    r.add("zoom", "Zoom", Category::Camera, "Hold C to zoom", true)
        .slider("factor", "Zoom factor", 4, 1.5f, 12, 0.5f).slider("key", "Key", 0x43, 8, 254, 1)
        .toggleSetting("smooth", "Smooth", true).toggleSetting("slow_turn", "Slower turning while zoomed", true);
    r.add("fov_changer", "FOV Changer", Category::Camera, "Scale the field of view beyond the settings range")
        .slider("scale", "FOV scale", 1.15f, 0.5f, 1.8f, 0.05f);
    r.add("sensitivity", "Sensitivity Multiplier", Category::Camera, "Multiply mouse sensitivity")
        .slider("multiplier", "Multiplier", 1.0f, 0.1f, 4.0f, 0.05f);
    r.plan("freelook", "Freelook", Category::Camera, "Look around without turning (Lamium has this today)");
    r.plan("snap_look", "Snap Look", Category::Camera, "Hold a key for third-person front view");
    r.add("view_bobbing", "No View Bobbing", Category::Camera, "Turn off the walking camera bob");
    r.plan("view_model", "View Model", Category::Camera, "Move / scale the held item");
    r.add("no_hurt_cam", "No Hurt Cam", Category::Camera, "No camera tilt when you take damage");
    r.add("motion_blur", "Motion Blur", Category::Camera, "Smooth blur when you turn the camera (drawn by Renderer Foundation)")
        .slider("strength", "Strength", 1.0f, 0.1f, 3.0f, 0.05f);

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
    r.plan("block_outline", "Block Outline", Category::Visual, "Custom selection outline colour / width");
    r.plan("better_nametags", "Better Nametags", Category::Visual, "Health and distance under names");
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
        .toggleSetting("camswing", "Camera swing", true);
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
    installVisualHooks();
    installChatHooks();
    serverauto::install();
    BfFovHook::hook();
    BfTurnHook::hook();
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
        hitch::gCur.fr = hitch::since(tSec); tSec = std::chrono::steady_clock::now();
        auto* player = ctx.mClient.getLocalPlayer();
        gLocalPlayer = static_cast<void*>(static_cast<Player*>(player));
        if (player) gLocalId = player->getOrCreateUniqueID().rawID;
        if (player) postfx::hudFrame(player->getRotation().x, player->getRotation().z);
        if (player) { // [BF] hitch log: health drops = you took a hit (servers send health to the client)
            static int lastHp = -1;
            int const hp = player->getHealth();
            if (lastHp >= 0 && hp < lastHp && hp > 0) { try { hitch::mark(std::format("hit: hp {} -> {}", lastHp, hp).c_str()); } catch (...) {} }
            else if (lastHp > 0 && hp <= 0) { try { hitch::mark("died"); } catch (...) {} }
            lastHp = hp;
        }
        tSec = std::chrono::steady_clock::now();
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
    BfAttackHook::unhook();
    BfSurvivalAttackHook::unhook();
}

} // namespace bedrock_forge::client
