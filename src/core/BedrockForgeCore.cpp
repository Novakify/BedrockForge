#include "core/BedrockForgeCore.h"

#include "mc/world/actor/player/Player.h"

#include <atomic>
#include <chrono>
#include <format>

#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/mod/RegisterHelper.h"

#include "bf/Config.h"
#include "bf/Hud.h"
#include "bf/Input.h"
#include "client/Menu.h"
#include "client/Module.h"

namespace bedrock_forge {

namespace {
std::atomic<bool>     gHudVisible{true};
std::atomic<bool>     gInWorld{false};
std::atomic<uint64_t> gTicks{0};

// Simple FPS estimate from HUD render calls (one AfterUIRenderEvent per frame on the HUD screen).
struct FpsCounter {
    using clock = std::chrono::steady_clock;
    clock::time_point windowStart = clock::now();
    int               frames      = 0;
    int               fps         = 0;
    void              frame() {
        ++frames;
        auto now = clock::now();
        if (now - windowStart >= std::chrono::seconds(1)) {
            fps         = frames;
            frames      = 0;
            windowStart = now;
        }
    }
} gFps;
} // namespace

Core& Core::getInstance() {
    static Core instance;
    return instance;
}

bool Core::load() {
    auto& log = getSelf().getLogger();
    log.info("Bedrock Forge Core loading (target MC 1.26.51 / LeviLamina 26.51.x)");
    bf::config::loadOrCreate(getSelf(), mConfig);
    gHudVisible = mConfig.hudEnabled;
    log.info("Config loaded: hud={} heartbeat={}", mConfig.hudEnabled, mConfig.logTickHeartbeat);
    return true;
}

bool Core::enable() {
    auto& log = getSelf().getLogger();

    // ---- Prototype 1: event callbacks ----
    mSubs.on<ll::event::ClientJoinLevelEvent>([this](ll::event::ClientJoinLevelEvent& ev) {
        impact::setLocalPlayerId(ev.player().getOrCreateUniqueID().rawID);
        gInWorld = true;
        gTicks   = 0;
        getSelf().getLogger().info("[BF] Joined a world - event bridge is alive.");
    });
    mSubs.on<ll::event::ClientExitLevelEvent>([this](ll::event::ClientExitLevelEvent&) {
        gInWorld = false;
        impact::setLocalPlayerId(0);
        getSelf().getLogger().info("[BF] Left the world after {} client ticks.", gTicks.load());
    });
    mSubs.on<ll::event::ClientLevelTickEvent>([this](ll::event::ClientLevelTickEvent&) {
        auto t = ++gTicks;
        if (mConfig.logTickHeartbeat && t % 600 == 0) getSelf().getLogger().info("[BF] heartbeat: {} ticks", t);
    });

    // ---- Prototype 2: custom HUD + keybind ----
    bf::input::bindKey("bedrock_forge.toggle_hud", bf::input::vk::J, [this] {
        gHudVisible = !gHudVisible;
        getSelf().getLogger().info("[BF] HUD {}", gHudVisible ? "shown" : "hidden");
    });

    mSubs.on<ll::event::AfterUIRenderEvent>([this](ll::event::AfterUIRenderEvent& ev) {
        if (!gHudVisible || !gInWorld) return;
        if (!bf::hud::isHudScreen(ev.screenView())) return;
        gFps.frame();
        auto&       ctx = ev.uiRenderContext();
        float const x = static_cast<float>(mConfig.hudX), y = static_cast<float>(mConfig.hudY);
        bf::hud::fillBox(ctx, x - 2, y - 2, 118, 26, {0.f, 0.f, 0.f, 0.45f});
        bf::hud::drawLabel(ctx, x, y, "Bedrock Forge v0.1", {0.55f, 0.85f, 1.f, 1.f});
        bf::hud::drawLabel(ctx, x, y + 11, std::format("{} fps | {} ticks | [J] hide", gFps.fps, gTicks.load()),
                           {1.f, 1.f, 1.f, 0.9f});
    });

    // ---- Client: module framework + ClickGUI (Right Shift) ----
    client::registerBuiltins();
    client::Registry::get().load((getSelf().getConfigDir() / "modules.json").string());
    client::installRuntime();
    client::menu::install();

    // ---- Prototype 3: cinematic impact frames (installed last so it draws over the HUD) ----
    impact::install(mSubs, getSelf(), mConfig.impact);
    bf::input::bindKey("bedrock_forge.toggle_impact", bf::input::vk::K, [this] {
        mConfig.impact.enabled = !mConfig.impact.enabled;
        getSelf().getLogger().info("[BF] Impact frames {}", mConfig.impact.enabled ? "on" : "off");
    });

    log.info("Bedrock Forge Core enabled with {} listeners. Right Shift = menu, J = HUD, K = impact frames, C = zoom.", mSubs.size());
    return true;
}

bool Core::disable() {
    client::menu::uninstall();
    client::uninstallRuntime();
    client::Registry::get().save();
    mSubs.clear();
    mConfig.hudEnabled = gHudVisible;
    bf::config::save(getSelf(), mConfig);
    getSelf().getLogger().info("Bedrock Forge Core disabled cleanly; all listeners removed.");
    return true;
}

} // namespace bedrock_forge

LL_REGISTER_MOD(bedrock_forge::Core, bedrock_forge::Core::getInstance());
