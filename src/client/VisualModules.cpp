// Bedrock Forge client - visual modules that need engine hooks (fullbright, hit colour, hurt cam, view bob,
// time changer). Every hook falls through to vanilla when its module is off.

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#include "client/Diag.h"

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/client/world/level/dimension/NetherLightTextureImageBuilder.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Module.h"

namespace bedrock_forge::client {

namespace {
std::atomic<std::thread::id> gRenderThread{};

void brighten(std::unique_ptr<BaseLightData>& data) {
    auto* m = Registry::get().find("fullbright");
    ++diag::lightHook;
    if (!data || !m || !m->enabled) return;
    ++diag::lightBrightened;
    float k = m->num("strength", 1.0f);
    data->mNightvisionActive = true;
    data->mNightvisionScale  = std::max(data->mNightvisionScale, k);
    data->mDarknessFactor    = 0.0f;
    data->mDarkenWorldAmount = data->mDarkenWorldAmount * (1.0f - k);
    if (data->mGamma < k) data->mGamma = k;
}
} // namespace

void noteRenderThread() { gRenderThread = std::this_thread::get_id(); }

LL_TYPE_INSTANCE_HOOK(BfLightHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
                      &BaseLightTextureImageBuilder::$createBaseLightTextureData, std::unique_ptr<BaseLightData>,
                      IClientInstance* client, BaseLightData const& current) {
    auto data = origin(client, current);
    brighten(data);
    return data;
}
LL_TYPE_INSTANCE_HOOK(BfNetherLightHook, ll::memory::HookPriority::Normal, NetherLightTextureImageBuilder,
                      &NetherLightTextureImageBuilder::$createBaseLightTextureData, std::unique_ptr<BaseLightData>,
                      IClientInstance* client, BaseLightData const& current) {
    auto data = origin(client, current);
    brighten(data);
    return data;
}

LL_TYPE_INSTANCE_HOOK(BfOverlayHook, ll::memory::HookPriority::Normal, ActorShaderManager, &ActorShaderManager::$_getOverlayColor,
                      mce::Color, Actor& actor, float a) {
    ++diag::overlayHook;
    mce::Color c = origin(actor, a);
    auto*      m = Registry::get().find("hit_color");
    // vanilla hurt overlay is a translucent red; recolour it and keep its strength
    if (m && m->enabled && c.a > 0.01f && c.r > c.g + 0.2f && c.r > c.b + 0.2f) {
        c.r = m->num("r", 0.2f);
        c.g = m->num("g", 0.6f);
        c.b = m->num("b", 1.0f);
    }
    return c;
}

LL_TYPE_INSTANCE_HOOK(BfHurtCamHook, ll::memory::HookPriority::Normal, LevelRendererPlayer, &LevelRendererPlayer::bobHurt, void,
                      Matrix& mv, float a) {
    if (Registry::get().on("no_hurt_cam")) return;
    origin(mv, a);
}
LL_TYPE_INSTANCE_HOOK(BfBobHook, ll::memory::HookPriority::Normal, LevelRendererPlayer, &LevelRendererPlayer::bobView, void,
                      Matrix& mv, float a) {
    ++diag::bobHook;
    if (Registry::get().on("view_bobbing")) return;
    origin(mv, a);
}

// Dimension is shared with the integrated server in single-player: only answer differently on the render thread.
LL_TYPE_INSTANCE_HOOK(BfTimeHook, ll::memory::HookPriority::Normal, Dimension, &Dimension::getTimeOfDay, float, float a) {
    ++diag::timeHook;
    if (Registry::get().on("time_changer") && std::this_thread::get_id() == gRenderThread.load())
        return Registry::get().find("time_changer")->num("time", 0.0f);
    return origin(a);
}

void installVisualHooks() {
    BfLightHook::hook();
    BfNetherLightHook::hook();
    BfOverlayHook::hook();
    BfHurtCamHook::hook();
    BfBobHook::hook();
    BfTimeHook::hook();
}
void uninstallVisualHooks() {
    BfLightHook::unhook();
    BfNetherLightHook::unhook();
    BfOverlayHook::unhook();
    BfHurtCamHook::unhook();
    BfBobHook::unhook();
    BfTimeHook::unhook();
}

} // namespace bedrock_forge::client
