// LyricsFlow port - [BF] falling-letter physics (new feature; LyricsFlow has nothing like it). Original code for
// Bedrock Forge. Each letter of a word that leaves becomes a small rigid quad: it falls under gravity, spins, lands on
// the top face of the block below (reference video A: letters lie flat on grass, steps, slabs), optionally bounces
// and tumbles, settles, stays a while, then fades. Pure C++ against IWorld so it is unit-tested with a fake world.
#pragma once
#include <cstdint>
#include <vector>

#include "lyrics/BfLyrics.h"
#include "lyrics/core/Config.h"
#include "lyrics/core/World.h"

namespace bf::lyrics {

struct PhysicsParams {
    LetterLand land     = LetterLand::Flat;
    float      gravity  = 18.0f;  // blocks/s^2
    float      bounce   = 0.25f;  // restitution
    float      friction = 0.6f;
    float      spin     = 1.0f;
    float      lifetimeMs = 6000.0f;
    float      fadeMs     = 1200.0f;
    int        maxLetters = 400;
    static PhysicsParams from(Config const& c);
};

struct Letter {
    enum class State : uint8_t { Waiting, Falling, Resting, Fading };
    BfGlyphV1 tmpl{};            // look of the letter (uv, colours, flags); pos/right/up are rebuilt every frame
    Vec3      pos, vel, angVel;  // world
    Quat      orient;            // local +x = glyph right, +y = glyph up, +z = glyph normal
    Quat      target;            // Flat: how it ends up lying (text reads from where you stood, small yaw jitter)
    float     waitMs = 0;        // Flat: letters of a word let go one after another (reference video)
    float     halfW = 0.1f, halfH = 0.1f;
    float     restMs = 0, fadeMs = 0, alpha = 1;
    float     restLift = 0.003f; // tiny per-letter height so overlapping letters don't z-fight
    double    fallStartY = 0;
    State     state = State::Falling;
    bool      hasTarget = false;
};

class LetterPhysics {
public:
    /// Turn quads (positions relative to `origin`, as built by buildWordGlyphs with mainPassOnly) into letters.
    void spawn(std::vector<BfGlyphV1> const& quads, Vec3 origin, int64_t seed, PhysicsParams const& prm);
    void step(float dt, IWorld* world, PhysicsParams const& prm);
    /// Append the letters as quads relative to `origin`.
    void emit(Vec3 origin, std::vector<BfGlyphV1>& out) const;
    void clear() { mLetters.clear(); }
    std::vector<Letter> const& letters() const { return mLetters; }

    /// Height of the highest collision surface under (x, z) at or below y (NaN if none within 4 blocks).
    static double supportY(IWorld& w, double x, double y, double z);

private:
    void substep(Letter& l, float dt, IWorld* w, PhysicsParams const& prm);
    std::vector<Letter> mLetters;
};

/// Rotation whose columns are the given orthonormal axes.
Quat quatFromBasis(Vec3 ex, Vec3 ey, Vec3 ez);

} // namespace bf::lyrics
