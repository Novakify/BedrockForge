// LyricsFlow port - IWorld over LeviLamina (client BlockSource + players). Original code for Bedrock Forge.
// STAGED, UNVERIFIED IN GAME (spike S4 in docs/PORT_LYRICSFLOW.md): client block reads on servers, solid/air tests.
// Collision shapes are approximated per block: air = passable, "solid" (CachedComponentData::mIsSolid) = full cube,
// anything else (flowers, grass, rails...) = passable. Slabs/stairs/snow layers as real shapes is a later step
// (BlockType collision shapes), see docs/LYRICSFLOW_SPEC.md "Open items".
#ifdef _WIN32
#include "lyrics/win/World_win.h"

#include <cmath>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/CachedComponentData.h"

namespace bf::lyrics::win {

namespace {
Block const* blockAt(BlockSource& bs, BlockPos p) {
    if (p.y < -64 || p.y > 319) return nullptr;
    return &bs.getBlock(::BlockPos(p.x, p.y, p.z));
}
} // namespace

GameWorld::GameWorld(Actor& player) : mPlayer(player), mBs(player.getDimensionBlockSource()) {}

bool GameWorld::isAir(BlockPos p) {
    Block const* b = blockAt(mBs, p);
    return !b || b->isAir();
}
bool GameWorld::solidCube(BlockPos p) {
    Block const* b = blockAt(mBs, p);
    return b && !b->isAir() && b->mCachedComponentData->mIsSolid;
}
void GameWorld::collisionBoxes(BlockPos p, std::vector<Aabb>& out) {
    if (solidCube(p)) out.push_back(Aabb{});
}
bool GameWorld::hasFluid(BlockPos p) {
    if (p.y < -64 || p.y > 319) return false;
    return !mBs.getLiquidBlock(::BlockPos(p.x, p.y, p.z)).isAir();
}
bool GameWorld::canSeeSky(BlockPos p) { return mBs.canSeeSky(::BlockPos(p.x, p.y, p.z)); }
int  GameWorld::skyLight(BlockPos p) { return canSeeSky(p) ? 15 : 0; } // TODO(S4): real sky light (getBrightnessPair)

// Voxel walk (Amanatides & Woo) over whole blocks; Visual stops at any non-air block, Collider at solid cubes.
RayHit GameWorld::clip(Vec3 from, Vec3 to, RayShape shape, bool withFluids) {
    Vec3   d   = to - from;
    double len = d.length();
    if (len < 1e-9) return {};
    Vec3     dir = d * (1.0 / len);
    BlockPos b   = BlockPos::of(from);
    int      sx = dir.x > 0 ? 1 : -1, sy = dir.y > 0 ? 1 : -1, sz = dir.z > 0 ? 1 : -1;
    auto     tMaxOf = [](double p, double dd, int cell) {
        if (std::fabs(dd) < 1e-12) return 1e30;
        double edge = dd > 0 ? cell + 1.0 : double(cell);
        return (edge - p) / dd;
    };
    double tx = tMaxOf(from.x, dir.x, b.x), ty = tMaxOf(from.y, dir.y, b.y), tz = tMaxOf(from.z, dir.z, b.z);
    double dx = std::fabs(dir.x) < 1e-12 ? 1e30 : 1.0 / std::fabs(dir.x);
    double dy = std::fabs(dir.y) < 1e-12 ? 1e30 : 1.0 / std::fabs(dir.y);
    double dz = std::fabs(dir.z) < 1e-12 ? 1e30 : 1.0 / std::fabs(dir.z);
    double t = 0;
    Face   face = Face::Up;
    for (int i = 0; i < 512 && t <= len; ++i) {
        bool stop = shape == RayShape::Visual ? !isAir(b) : solidCube(b);
        if (!stop && withFluids) stop = hasFluid(b);
        if (stop && i > 0) return {true, from + dir * t, face};
        if (tx < ty && tx < tz) { b.x += sx; t = tx; tx += dx; face = sx > 0 ? Face::West : Face::East; }
        else if (ty < tz) { b.y += sy; t = ty; ty += dy; face = sy > 0 ? Face::Down : Face::Up; }
        else { b.z += sz; t = tz; tz += dz; face = sz > 0 ? Face::North : Face::South; }
    }
    return {};
}

std::vector<Entity> GameWorld::entities() {
    std::vector<Entity> out;
    Vec3 const          me{mPlayer.getFeetPos().x, mPlayer.getFeetPos().y, mPlayer.getFeetPos().z};
    mPlayer.getLevel().forEachPlayer([&](Player& p) {
        auto const& bb = static_cast<Actor&>(p).getAABB();
        Entity      e;
        e.box           = {bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z};
        e.isLocalPlayer = static_cast<Actor*>(&p) == &mPlayer;
        e.isPlayer      = true;
        if (e.center().dist(me) < 64.0) out.push_back(e);
        return true;
    });
    return out;
}

} // namespace bf::lyrics::win
#endif
