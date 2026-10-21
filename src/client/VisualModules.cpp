// Bedrock Forge client - visual modules that need engine hooks (fullbright, hit colour, hurt cam, view bob,
// time changer). Every hook falls through to vanilla when its module is off.

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#include "client/Diag.h"

#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/block/BlockTessellatorCache.h"
#include "mc/client/renderer/chunks/RenderChunkCoordinator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/client/world/level/dimension/NetherLightTextureImageBuilder.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/image/Image.h"
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

// The finished light texture: in 1.26 the BaseLightData night-vision fields don't reach the image, so fullbright
// also lifts the built pixels toward white. Blob's members are private; mirror them (same types -> same layout).
struct BlobMirror {
    std::unique_ptr<mce::Blob::value_type[], mce::Blob::Deleter> blob;
    std::size_t                                                  size;
};
static_assert(sizeof(BlobMirror) == sizeof(mce::Blob));
void brightenImage(mce::Image* img) {
    auto* m = Registry::get().find("fullbright");
    if (!img || !m || !m->enabled) return;
    int bpp = img->imageFormat == mce::ImageFormat::RGBA8Unorm ? 4 : img->imageFormat == mce::ImageFormat::RGB8Unorm ? 3 : 0;
    if (!bpp) return;
    auto&       b    = reinterpret_cast<BlobMirror&>(img->mImageBytes);
    std::size_t need = std::size_t(img->mWidth) * img->mHeight * std::max<uint32_t>(img->mDepth, 1) * bpp;
    if (!b.blob || need == 0 || need > b.size || need > (1u << 20)) return;
    float k = std::clamp(m->num("strength", 1.0f), 0.0f, 1.0f);
    auto* px = b.blob.get();
    for (std::size_t i = 0; i < need; i += bpp)
        for (int c = 0; c < 3; ++c) px[i + c] = (unsigned char)(px[i + c] + (255 - px[i + c]) * k);
    ++diag::lightImage;
}

void noteRenderThread() { gRenderThread = std::this_thread::get_id(); }

// ---- Fullbright v3: light baked into terrain vertices -------------------------------------------------------------
// E-019 showed the 1.26 light texture ignores BaseLightData + pixel edits, so v3 lifts the per-vertex light the chunk
// tessellator bakes (BlockTessellatorCache::getLightColor -> lightmap UV) and asks every RenderChunkCoordinator to
// relight its geometry when fullbright changes. Client-only path: the integrated server never touches it.
namespace {
std::atomic<int>  gFbLevel{-1};       // 0..15 block/sky light floor while on, -1 = off (read on chunk-build workers)
std::atomic<int>  gFbApplied{-1};     // level the loaded chunks were last lit with
std::atomic<long> gFbRelights{0};
int fullbrightLevel() {
    auto* m = Registry::get().find("fullbright");
    if (!m || !m->enabled) return -1;
    return std::clamp(int(m->num("strength", 1.0f) * 15.0f + 0.5f), 0, 15);
}
} // namespace

LL_TYPE_INSTANCE_HOOK(BfTessLightHook, ll::memory::HookPriority::Normal, BlockTessellatorCache, &BlockTessellatorCache::getLightColor,
                      BrightnessPair, BlockPos const& p, Block const* blockHint) {
    diag::tessLight.fetch_add(1, std::memory_order_relaxed);
    BrightnessPair r = origin(p, blockHint);
    int lv = gFbLevel.load(std::memory_order_relaxed);
    if (lv >= 0) {
        if (r.sky->mValue < lv) r.sky->mValue = uchar(lv);
        if (r.block->mValue < lv) r.block->mValue = uchar(lv);
    }
    return r;
}

LL_TYPE_INSTANCE_HOOK(BfAmbientHook, ll::memory::HookPriority::Normal, LevelRendererCamera, &LevelRendererCamera::$getAmbientBrightness,
                      float) {
    float v = origin();
    return gFbLevel.load(std::memory_order_relaxed) >= 0 ? std::max(v, 1.0f) : v;
}

// Called every HUD frame (client thread). Relights loaded chunks when the module or its strength changed.
void fullbrightTick(IClientInstance& client) {
    int want = fullbrightLevel();
    gFbLevel = want;
    if (want == gFbApplied.load()) return;
    auto* lr = client.getLevelRenderer();
    if (!lr) return;
    gFbApplied = want;
    for (auto& [dim, coord] : lr->mRenderChunkCoordinators.get()) {
        (void)dim;
        if (coord) { coord->relightAllRenderChunkGeometry(); ++gFbRelights; }
    }
    diag::fbRelights = gFbRelights.load();
}

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

LL_TYPE_INSTANCE_HOOK(BfLightImageHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
                      &BaseLightTextureImageBuilder::$buildImage, bool, BaseLightData const& lightData, mce::Image* targetImage,
                      uint imageLength, float a, float ambientBoost, bool clampToMinimum) {
    bool r = origin(lightData, targetImage, imageLength, a, ambientBoost, clampToMinimum);
    brightenImage(targetImage);
    return r;
}
LL_TYPE_INSTANCE_HOOK(BfLightImageSplitHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
                      &BaseLightTextureImageBuilder::$buildImageSplit, bool, BaseLightData const& lightData,
                      mce::Image* targetImage, uint imageLength, float a, bool clampToMinimum) {
    bool r = origin(lightData, targetImage, imageLength, a, clampToMinimum);
    brightenImage(targetImage);
    return r;
}

void installVisualHooks() {
    BfTessLightHook::hook();
    BfAmbientHook::hook();
    BfLightImageHook::hook();
    BfLightImageSplitHook::hook();
    BfLightHook::hook();
    BfNetherLightHook::hook();
    BfOverlayHook::hook();
    BfHurtCamHook::hook();
    BfBobHook::hook();
    BfTimeHook::hook();
}
void uninstallVisualHooks() {
    BfTessLightHook::unhook();
    BfAmbientHook::unhook();
    BfLightImageHook::unhook();
    BfLightImageSplitHook::unhook();
    BfLightHook::unhook();
    BfNetherLightHook::unhook();
    BfOverlayHook::unhook();
    BfHurtCamHook::unhook();
    BfBobHook::unhook();
    BfTimeHook::unhook();
}

} // namespace bedrock_forge::client
