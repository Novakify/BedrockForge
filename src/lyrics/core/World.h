// LyricsFlow port - what the placement maths and the letter physics need from the game, as an interface.
// The Bedrock implementation (LeviLamina BlockSource / Actor) lives in lyrics/win/; tests use a fake voxel world.
// Original code for Bedrock Forge.
#pragma once
#include <cmath>
#include <limits>
#include <vector>

#include "lyrics/core/Math.h"

namespace bf::lyrics {

enum class Face : int { Down = 0, Up, North, South, West, East }; // North = -Z, South = +Z, West = -X, East = +X

inline Vec3 normalOf(Face f) {
    switch (f) {
    case Face::Down: return {0, -1, 0};
    case Face::Up: return {0, 1, 0};
    case Face::North: return {0, 0, -1};
    case Face::South: return {0, 0, 1};
    case Face::West: return {-1, 0, 0};
    default: return {1, 0, 0};
    }
}
inline bool horizontal(Face f) { return f != Face::Up && f != Face::Down; }

struct RayHit {
    bool hit  = false;
    Vec3 pos  {};
    Face face = Face::Up;
};

/// Ray shapes, after Minecraft's ClipContext: Collider = collision boxes, Visual = what you see (leaves, glass...).
enum class RayShape { Collider, Visual };

struct Entity {
    Aabb box;
    bool isLocalPlayer = false;
    bool isPlayer      = true;
    bool isTarget      = false; // the entity under the crosshair (Java mc.crosshairPickEntity)
    Vec3 center() const { return {(box.x0 + box.x1) * 0.5, (box.y0 + box.y1) * 0.5, (box.z0 + box.z1) * 0.5}; }
    double width() const { return box.x1 - box.x0; }
    double height() const { return box.y1 - box.y0; }
};

class IWorld {
public:
    virtual ~IWorld() = default;
    /// First block hit between from and to. withFluids = also stop at water/lava (Java Fluid.ANY).
    virtual RayHit clip(Vec3 from, Vec3 to, RayShape shape, bool withFluids) = 0;
    virtual bool   isAir(BlockPos p) = 0;
    /// Collision boxes of the block, in block-local 0..1 coordinates (empty = passable).
    virtual void   collisionBoxes(BlockPos p, std::vector<Aabb>& out) = 0;
    virtual bool   hasFluid(BlockPos p) = 0;
    virtual bool   canSeeSky(BlockPos p) = 0;
    virtual int    skyLight(BlockPos p) = 0;
    /// Cloud layer height here, NaN when clouds are off / in a dimension without clouds.
    virtual double cloudY() { return std::numeric_limits<double>::quiet_NaN(); }
    virtual std::vector<Entity> entities() { return {}; }

    // helpers with LyricsFlow's definitions
    bool empty(BlockPos p) { // no collision shape and no fluid
        std::vector<Aabb> b;
        collisionBoxes(p, b);
        return b.empty() && !hasFluid(p);
    }
    bool solid(BlockPos p) { return !empty(p); }
};

/// The camera as LyricsFlow sees it (Proj.update): eye position, yaw/pitch in degrees, far plane in blocks.
struct CameraState {
    Vec3  pos;
    float yaw = 0, pitch = 0;
    float depthFar    = 256.0f; // GameRenderer.getDepthFar() (render distance)
    bool  thirdPerson = false;
    float screenAspect = 16.0f / 9.0f; // width / height, for SCREEN mode
};

} // namespace bf::lyrics
