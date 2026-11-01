// FiguraBridge - runtime textures (see DynTex.h). Original code for Bedrock Forge.
//   mce::TextureGroup (ci.getTextureGroup()) owns every client texture, keyed by ResourceLocation:
//     updateTextureInPlace(loc, cg::ImageBuffer) - rewrites an already loaded texture of the same size
//     uploadTexture(loc, cg::ImageBuffer)        - registers/replaces the texture at loc (same call the game uses
//                                                  for runtime images such as skins)
//   Pixels go mce::Image (RGBA8Unorm, sRGB) -> cg::ImageBuffer.
#ifdef _WIN32
#include "figura/win/DynTex.h"

#include <cstring>
#include <exception>
#include <optional>
#include <utility>

#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ClientThreadExecutor.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTexture.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"

namespace bf::figura::win {

namespace {

constexpr int kMaxSide = 4096;

bool validArgs(std::string const& path, int w, int h, uint8_t const* rgba) {
    return !path.empty() && rgba && w > 0 && h > 0 && w <= kMaxSide && h <= kMaxSide;
}

cg::ImageBuffer makeBuffer(int w, int h, uint8_t const* rgba) {
    mce::Image img(uint32(w), uint32(h), mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
    img.setRawImage(mce::Blob(rgba, size_t(w) * size_t(h) * 4));
    return cg::ImageBuffer(std::move(img));
}

// Loaded, non-missing texture at loc: its data, else nullptr. Never triggers a load.
BedrockTextureData const* loadedData(mce::TextureGroup& tg, ResourceLocation const& loc, mce::TexturePtr& keep) {
    if (!tg.isLoaded(loc, false, cg::TextureSetLayerType::Color)) return nullptr;
    keep = tg.getTexture(loc, false, std::nullopt, cg::TextureSetLayerType::Color);
    // ll::TypedStorage<A,S,T> is T itself here (sizes match), so members are used directly.
    std::shared_ptr<BedrockTextureData const> const& sp = keep.mClientTexture;
    if (!sp) return nullptr;
    if (sp->mIsMissingTexture == IsMissingTexture::Yes) return nullptr;
    return sp.get();
}

} // namespace

DynTexResult dynTexUploadEx(IClientInstance& ci, std::string const& resourcePath, int w, int h, uint8_t const* rgba) {
    if (!validArgs(resourcePath, w, h, rgba)) return DynTexResult::Failed;
    try {
        std::shared_ptr<mce::TextureGroup> tg = ci.getTextureGroup();
        if (!tg) return DynTexResult::Failed;
        ResourceLocation loc{Core::PathView(resourcePath)};

        mce::TexturePtr keep;
        if (auto const* d = loadedData(*tg, loc, keep)) {
            auto const& desc = d->mTextureDescription;
            if (int(desc.mWidth) == w && int(desc.mHeight) == h
                && tg->updateTextureInPlace(loc, makeBuffer(w, h, rgba)))
                return DynTexResult::InPlace;
        }
        tg->uploadTexture(loc, makeBuffer(w, h, rgba));
        return DynTexResult::Uploaded;
    } catch (...) {
        return DynTexResult::Failed;
    }
}

void dynTexUploadAsync(std::string resourcePath, int w, int h, std::vector<uint8_t> rgba) {
    if (resourcePath.empty() || w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4) return;
    try {
        ll::thread::ClientThreadExecutor::getDefault().execute(
            [p = std::move(resourcePath), w, h, px = std::move(rgba)] {
                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                dynTexUploadEx(*ci, p, w, h, px.data());
            }
        );
    } catch (...) {}
}

bool dynTexUnload(IClientInstance& ci, std::string const& resourcePath) {
    if (resourcePath.empty()) return false;
    try {
        std::shared_ptr<mce::TextureGroup> tg = ci.getTextureGroup();
        if (!tg) return false;
        ResourceLocation loc{Core::PathView(resourcePath)};
        return tg->unloadTexture(loc, false);
    } catch (...) {
        return false;
    }
}

bool dynTexInfo(IClientInstance& ci, std::string const& resourcePath, int& w, int& h) {
    w = h = 0;
    if (resourcePath.empty()) return false;
    try {
        std::shared_ptr<mce::TextureGroup> tg = ci.getTextureGroup();
        if (!tg) return false;
        ResourceLocation loc{Core::PathView(resourcePath)};
        mce::TexturePtr keep;
        auto const* d = loadedData(*tg, loc, keep);
        if (!d) return false;
        auto const& desc = d->mTextureDescription;
        w = int(desc.mWidth);
        h = int(desc.mHeight);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace bf::figura::win
#endif
