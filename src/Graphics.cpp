// Renderer Foundation - graphics controls for 1.26 (see Graphics.h, Notice.h).
#include "Graphics.h"

#include <atomic>
#include <format>
#include <mutex>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/options/GraphicsMode.h"
#include "mc/options/IAdvancedGraphicsOptions.h"
#include "mc/resources/ResourcePackManager.h"

#include "Options.h"

namespace rf::graphics {

namespace {
std::mutex        gStatusMutex;
std::string       gStatus = "ready";
std::atomic<long> gReloads{0};
void              setStatus(std::string s) {
    std::lock_guard l(gStatusMutex);
    gStatus = std::move(s);
}

IAdvancedGraphicsOptions* advancedOptions(ClientInstance& ci) {
    auto tg = ci.getTextureGroup();
    return tg ? tg->mAdvancedGraphicsOptions.get().get() : nullptr;
}
} // namespace

LL_TYPE_INSTANCE_HOOK(RfPacksChangedHook, ll::memory::HookPriority::Normal, MinecraftGame,
                      &MinecraftGame::$onActiveResourcePacksChanged, void, ResourcePackManager& mgr) {
    origin(mgr);
    if (RfOptions::autoReloadOnPackChange && RfOptions::redirectShaders) RfOptions::reloadShaders = true;
}

void install() {
    RfOptions::reloadShadersAvailable = true;
    RfPacksChangedHook::hook();
}
void uninstall() { RfPacksChangedHook::unhook(); }

void tick() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    if (auto* o = advancedOptions(*ci)) RfOptions::vibrantVisualsAllowed = o->isVibrantVisualsAllowed();

    if (RfOptions::reloadShaders.exchange(false)) {
        static_cast<MinecraftGame&>(ci->getMinecraftGame()).cleanReloadMaterials();
        ++gReloads;
        setStatus(std::format("shaders reloaded ({})", gReloads.load()));
    }
    if (RfOptions::applyGraphicsMode.exchange(false)) {
        auto* o  = advancedOptions(*ci);
        bool  on = RfOptions::vibrantVisualsEnabled;
        if (!o) setStatus("graphics options not available yet");
        else if (on && !o->isVibrantVisualsAllowed()) setStatus("Vibrant Visuals is not allowed here (device or server)");
        else if (!o->canSwitchGraphicsModeInGame()) setStatus("graphics mode can't be switched right now");
        else {
            o->setGraphicsMode(on ? GraphicsMode::Advanced : GraphicsMode::Fancy);
            setStatus(on ? "Vibrant Visuals on" : "Vibrant Visuals off (Fancy)");
        }
    }
}

std::string status() {
    std::lock_guard l(gStatusMutex);
    return gStatus;
}
long reloadCount() { return gReloads.load(); }

} // namespace rf::graphics
