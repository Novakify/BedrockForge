// LyricsFlow port - IWorld over LeviLamina (see World_win.cpp). Original code for Bedrock Forge.
#pragma once
#ifdef _WIN32
#include "lyrics/core/World.h"

class Actor;
class BlockSource;

namespace bf::lyrics::win {

class GameWorld final : public IWorld {
public:
    explicit GameWorld(Actor& player);
    RayHit              clip(Vec3 from, Vec3 to, RayShape shape, bool withFluids) override;
    bool                isAir(BlockPos p) override;
    void                collisionBoxes(BlockPos p, std::vector<Aabb>& out) override;
    bool                hasFluid(BlockPos p) override;
    bool                canSeeSky(BlockPos p) override;
    int                 skyLight(BlockPos p) override;
    std::vector<Entity> entities() override;

private:
    bool         solidCube(BlockPos p);
    Actor&       mPlayer;
    BlockSource& mBs;
};

} // namespace bf::lyrics::win
#endif
