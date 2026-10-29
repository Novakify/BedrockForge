// Renderer Foundation - Cinematic Interactions port: the few game queries the scene needs (LeviLamina side; the scene
// itself lives in Cinematic.cpp, which includes d3d12.h and so must not include LeviLamina headers). Original code.
#include "CinematicGame.h"

#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/image/Image.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/SerializedSkinImpl.h"
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/actor/player/SkinImage.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/phys/HitResult.h"

namespace rf::cine::game {
namespace {
Actor* player() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return nullptr;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return nullptr;
    return static_cast<Actor*>(lp);
}
} // namespace

bool playerState(double feet[3], float& yaw, float& pitch, bool& alive) {
    Actor* a = player();
    if (!a) return false;
    Vec3 f   = a->getFeetPos();
    feet[0]  = f.x;
    feet[1]  = f.y;
    feet[2]  = f.z;
    Vec2 r   = a->getRotation();
    pitch    = r.x;
    yaw      = r.y;
    alive    = a->isAlive();
    return true;
}

bool hitBlock(int& x, int& y, int& z, std::string& typeName) {
    Actor* a = player();
    if (!a) return false;
    HitResult& hit = a->getLevel().getHitResult();
    if (hit.mType != HitResultType::Tile) return false;
    x        = hit.mBlock.x;
    y        = hit.mBlock.y;
    z        = hit.mBlock.z;
    typeName = a->getDimensionBlockSource().getBlock(hit.mBlock).getTypeName();
    return true;
}

std::string blockName(int x, int y, int z) {
    Actor* a = player();
    if (!a) return {};
    return a->getDimensionBlockSource().getBlock(BlockPos(x, y, z)).getTypeName();
}

bool solid(int x, int y, int z) {
    Actor* a = player();
    if (!a) return false;
    Block const& b = a->getDimensionBlockSource().getBlock(BlockPos(x, y, z));
    return !b.isAir() && b.mCachedComponentData->mIsSolid;
}

void hideHand(int& saved, bool on) {
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto& o = ci->getOptions();
    if (on) {
        if (saved < 0) {
            saved = o.getHideItemInHand() ? 1 : 0;
            o.setHideItemInHand(true);
        }
    } else if (saved >= 0) {
        o.setHideItemInHand(saved == 1);
        saved = -1;
    }
}

bool skin(std::vector<unsigned char>& rgba, int& w, int& h, bool& slim) {
    Actor* a = player();
    if (!a) return false;
    Player& p = *static_cast<Player*>(a);
    std::unique_ptr<SerializedSkinRef>& ref = *p.mSkin;
    if (!ref) return false;
    auto& owner = *ref->mSkinImpl;
    if (!owner) return false;
    SerializedSkinImpl& impl = owner->mObject;
    mce::Image const&   img  = *impl.mSkinImage;
    if (img.imageFormat != mce::ImageFormat::RGBA8Unorm || img.mWidth < 64 || img.mHeight < 32) return false;
    size_t need = size_t(img.mWidth) * img.mHeight * 4;
    if (img.mImageBytes.size() < need) return false;
    auto const* src = reinterpret_cast<unsigned char const*>(img.mImageBytes.data());
    rgba.assign(src, src + need);
    w    = int(img.mWidth);
    h    = int(img.mHeight);
    slim = impl.mArmSizeType == SharedTypes::persona::ArmSizeType::Slim;
    return true;
}

} // namespace rf::cine::game
