// Renderer Foundation - Cinematic Interactions port: the few game queries the scene needs (LeviLamina side; the scene
// itself lives in Cinematic.cpp, which includes d3d12.h and so must not include LeviLamina headers). Original code.
#include "CinematicGame.h"
#include "Cinematic.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/game/ClientInputCallbacks.h"
#include "mc/client/gui/CraftingType.h"
#include "mc/deps/nbt/CompoundTag.h"
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
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/level/block/actor/CampfireBlockActor.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/PlayerUIContainer.h"

namespace rf::cine::game {
namespace {
template <class T> auto ptrOf(T& v) {
    if constexpr (requires { v.get(); }) return v.get();
    else return &v;
}
Actor* player() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return nullptr;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return nullptr;
    return static_cast<Actor*>(lp);
}
} // namespace
Actor* playerActor() { return player(); }

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

bool creative() {
    Actor* a = player();
    return a && a->isCreative();
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

bool useHitBlock() {
    Actor* a = player();
    if (!a) return false;
    HitResult& hit = a->getLevel().getHitResult();
    Player&    p   = *static_cast<Player*>(a);
    GameMode*  gm  = &*p.mGameMode; // the storage hands out the GameMode itself
    if (!gm) return false;
    if (hit.mType != HitResultType::Tile) { // nothing looked at: a book and quill in hand opens to be written in
        ItemStack const& held = p.getSelectedItem();
        if (held.getTypeName() != "minecraft:writable_book") return false;
        p.openBook(p.getSelectedItemSlot(), true, 0, nullptr);
        return true;
    }
    gm->buildBlock(hit.mBlock, hit.mFacing, HandSlot::Mainhand, false);
    return true;
}

thread_local int tOwnUse = 0; // a use the scene makes itself: not taken over by the interception below
bool useBlock(int x, int y, int z, int face) {
    Actor* a = player();
    if (!a) return false;
    Player&   p  = *static_cast<Player*>(a);
    GameMode* gm = &*p.mGameMode;
    if (!gm) return false;
    ++tOwnUse;
    bool r = gm->buildBlock(BlockPos(x, y, z), uchar(face), HandSlot::Mainhand, false);
    --tOwnUse;
    return r;
}
int selectedSlot() {
    Actor* a = player();
    return a ? static_cast<Player*>(a)->getSelectedItemSlot() : 0;
}
bool openInventory() {
    // the inventory key's own path (ClientInputCallbacks: tells the server the inventory is open, which then accepts
    // the screen's item moves); Player::openInventory alone opened a screen whose moves the server turned down
    auto ci = ll::service::getClientInstance();
    if (!ci || !player()) return false;
    ClientInputCallbacks::handleInventoryButtonPress(*ci, CraftingType::Inventory);
    return true;
}
std::string jukeboxRecord(int x, int y, int z) {
    Actor* a = player();
    if (!a) return {};
    BlockActor* be = a->getDimensionBlockSource().getBlockEntity(BlockPos(x, y, z));
    if (!be || a->getDimensionBlockSource().getBlock(BlockPos(x, y, z)).getTypeName() != "minecraft:jukebox") return {};
    ItemStack const& rec = static_cast<JukeboxBlockActor*>(be)->mRecord;
    return rec.isNull() ? std::string() : rec.getTypeName();
}

bool campfireFood(int x, int y, int z, std::string names[4], int times[4]) {
    Actor* a = player();
    if (!a) return false;
    BlockActor* be = a->getDimensionBlockSource().getBlockEntity(BlockPos(x, y, z));
    std::string type = a->getDimensionBlockSource().getBlock(BlockPos(x, y, z)).getTypeName();
    if (!be || (type != "minecraft:campfire" && type != "minecraft:soul_campfire")) return false;
    auto* c = static_cast<CampfireBlockActor*>(be);
    for (int i = 0; i < 4; ++i) {
        ItemInstance const& it = c->mCookingItem[i].get();
        names[i] = it.isNull() ? std::string() : it.getTypeName();
        times[i] = c->mCookingTime[i];
    }
    return true;
}

std::string blockName(int x, int y, int z) {
    Actor* a = player();
    if (!a) return {};
    return a->getDimensionBlockSource().getBlock(BlockPos(x, y, z)).getTypeName();
}

int facing(int x, int y, int z) {
    Actor* a = player();
    if (!a) return -1;
    Block const& b = a->getDimensionBlockSource().getBlock(BlockPos(x, y, z));
    std::string snbt = b.mSerializationId->toSnbt(SnbtFormat::ForceQuote, 0);
    for (char const* key : {"minecraft:cardinal_direction", "direction", "facing_direction"}) {
        auto at = snbt.find(std::string("\"") + key + "\"");
        if (at == std::string::npos) continue;
        auto colon = snbt.find(':', at + std::strlen(key) + 2);
        if (colon == std::string::npos) continue;
        std::string v = snbt.substr(colon + 1, 12);
        if (v.find("north") != std::string::npos) return 0;
        if (v.find("east") != std::string::npos) return 1;
        if (v.find("south") != std::string::npos) return 2;
        if (v.find("west") != std::string::npos) return 3;
        // numeric: facing_direction 2 north 3 south 4 west 5 east; direction 0 south 1 west 2 north 3 east
        int n = std::atoi(v.c_str() + (v[0] == ' ' ? 1 : 0));
        if (std::string(key) == "facing_direction") return n == 2 ? 0 : n == 3 ? 2 : n == 4 ? 3 : n == 5 ? 1 : -1;
        if (std::string(key) == "direction") return n == 0 ? 2 : n == 1 ? 3 : n == 2 ? 0 : 1;
    }
    return -1;
}

std::string blockStates(int x, int y, int z) {
    Actor* a = player();
    if (!a) return {};
    return a->getDimensionBlockSource().getBlock(BlockPos(x, y, z)).mSerializationId->toSnbt(SnbtFormat::ForceQuote, 0);
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
    // header variants differ (value / unique_ptr / shared_ptr): reach the object either way
    SerializedSkinRef* ref = ptrOf(*p.mSkin);
    if (!ref) return false;
    auto* owner = ptrOf(*ref->mSkinImpl);
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


std::string carriedItem(int& count) {
    count    = 0;
    Actor* a = player();
    if (!a) return {};
    Player&          p  = *static_cast<Player*>(a);
    ItemStack const& it = p.mPlayerUIContainer->getItem(0); // PlayerUISlot::CursorSelected
    if (it.isNull() || it.mCount == 0) return {};
    count = it.mCount;
    return it.getTypeName();
}

std::string handItem(int& count) {
    count    = 0;
    Actor* a = player();
    if (!a) return {};
    ItemStack const& it = static_cast<Player*>(a)->getSelectedItem();
    if (it.isNull() || it.mCount == 0) return {};
    count = it.mCount;
    return it.getTypeName();
}
} // namespace rf::cine::game

namespace rf::cine::game {
void installHooks() {} // the take-over is done on the input side (SpatialPanel), no engine hooks
} // namespace rf::cine::game
