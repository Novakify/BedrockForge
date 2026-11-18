// LyricsForge - [BF] material-aware impacts: falling letters that hit the ground kick up what they landed on -
// stone chips, dirt and sand puffs, grass bits, snow powder, water droplets, lava sparks, metal sparks, glass
// glints, leaves. Original code for Bedrock Forge. Pure logic: BfGlyphV1 SOLID quads, billboarded to the camera.
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Math.h"

namespace bf::lyrics {

enum class Surface : uint8_t { Generic, Stone, Dirt, Sand, Grass, Snow, Water, Lava, Wood, Metal, Glass, Leaves, Wool };

/// "minecraft:grass_block" -> Grass (keyword match, unknown -> Generic)
Surface surfaceOf(std::string_view blockName);

struct Landing {
    Vec3    pos;
    Surface surface = Surface::Generic;
    float   strength = 1; // impact speed / 6, 0.2 .. 2
    float   size = 0.1f;  // letter height (blocks)
};

class Bursts {
public:
    void spawn(Landing const& l, uint32_t seed);
    void step(float dt);
    /// billboard quads relative to `origin` facing a camera with this yaw / pitch (degrees)
    void emit(Vec3 const& origin, float camYaw, float camPitch, std::vector<BfGlyphV1>& out) const;
    size_t size() const { return mBits.size(); }
    void   clear() { mBits.clear(); }

    static constexpr size_t kMax = 1500;

private:
    struct Bit {
        Vec3     pos, vel;
        float    age = 0, life = 0.6f, size = 0.02f, gravity = 9.8f, drag = 1.5f;
        uint32_t argb = 0xFFFFFFFF;
        bool     glow = false;
    };
    std::vector<Bit> mBits;
};

} // namespace bf::lyrics
