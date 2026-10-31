// LyricsFlow port - the sky layers drawn around the live shader (dev.lyricsflow.app.sky.WorldSky, LyricsFlow 2.1 by
// 374m, MIT): stars, constellations, shooting stars, storm lightning, weather particles and "My picture".
// Pure geometry: camera-relative quads with per-corner colour; the renderer (RF) draws them after the sky dome.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lyrics/core/Sky.h"

namespace bf::lyrics {
class AutoPerf;
}

namespace bf::lyrics::sky {

/// Layer of a quad: Sky quads are unit-sphere directions (WorldSky's sky distance = 1, draw them at the far plane),
/// World quads are camera-relative blocks (weather near the player, depth tested).
enum class Layer : uint8_t { Sky, World };
/// Texture of a quad: Solid = flat colour; Sprite = sprites.png (256x64, 4 frames: petal, snow, glow, streak), uv set.
enum class Tex : uint8_t { Solid, Sprite };

struct Vert {
    float    x, y, z;
    float    u, v;
    uint32_t rgb;
    float    a;
};
struct Quad {
    Vert  v[4];
    Layer layer;
    Tex   tex;
};

/// Camera basis (WorldSky.renderSky): yaw/pitch in Minecraft degrees, +Y up.
struct Cam {
    double yaw = 0, pitch = 0;
    double fx = 0, fy = 0, fz = 1, rx = -1, ry = 0, rz = 0, ux = 0, uy = 1, uz = 0;
    static Cam of(double yawDeg, double pitchDeg);
};

/// WorldSky.dir: azimuth (0 = +Z, grows toward -X like yaw) and elevation in degrees -> unit vector.
void dirOf(double azDeg, double elDeg, double out[3]);

struct FxIn {
    int64_t nowMs = 0;          // wall clock (shooting stars run on real time like Java)
    double  dtSec = 0;          // frame time (weather motion)
    Cam     cam;
    double  camX = 0, camY = 0, camZ = 0; // world position (weather wraps around it)
    int     quality = 2;        // AutoPerf.quality: 0..2
    bool    picture = false;    // "My picture" is on and loaded: it replaces the dome and every layer here
};

/// The weather particle textures (SkyPainter.sprites(), 256x64 RGBA): resources/lyricsflow/sky/sprites.png.
inline constexpr char const* kSpritesFile = "sky/sprites.png";

class Fx {
public:
    Fx() { mRnd = int64_t(0x4C59524943); }
    /// Everything WorldSky draws besides the dome for this frame. `perf` gates effects like AutoPerf.allow().
    void frame(Preset const* p, Config const& cfg, AutoPerf const& perf, Frame const& f, FxIn const& in,
               std::vector<Quad>& out);

    // pieces (public for tests / previews)
    void stars(Preset const& p, Config const& cfg, int quality, float alpha, double time, Cam const& c,
               std::vector<Quad>& out) const;
    void constellations(float alpha, Cam const& c, std::vector<Quad>& out) const;
    void shooting(float alpha, int64_t nowMs, Cam const& c, std::vector<Quad>& out);
    void storm(float alpha, double time, Cam const& c, std::vector<Quad>& out);
    void weather(Weather w, float amount, int quality, float outside, double dt, double time, FxIn const& in,
                 std::vector<Quad>& out);

    void seed(int64_t s) { mRnd = s; }
    size_t particles() const { return mParts.size(); }
    size_t shootingCount() const { return mShooting.size(); }

private:
    struct Rnd { // java.util.Random
        uint64_t s = 0;
        bool     haveG = false;
        double   g = 0;
        Rnd& operator=(int64_t seed) { s = (uint64_t(seed) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); haveG = false; return *this; }
        int    next(int bits);
        float  nextFloat();
        double nextDouble();
        bool   nextBoolean();
        int    nextInt(int bound);
        double nextGaussian();
    };
    struct Shot { float az, el, born, dir; };
    struct Part { double p[11]; };

    Rnd                  mRnd;
    std::vector<Shot>    mShooting;
    int64_t              mNextShooting = 0, mT0 = -1;
    double               mFlashNext = 0, mFlashT0 = -10;
    std::vector<double>  mBolt; // az, el pairs
    std::vector<Part>    mParts;
    int                  mPartType = -1;
    double               c_yaw = 0; // camera yaw of this frame (new shooting stars start around it)
};

/// "My picture" (SkyImage): which file to show - cfg.skyImageFile when it is in `files` (sorted png/jpg/jpeg names of
/// the sky folder), else the first; "" when the folder is empty.
std::string pictureChoice(Config const& cfg, std::vector<std::string> files);
/// Is `name` a picture file SkyImage lists (.png / .jpg / .jpeg, case-insensitive)?
bool isPictureFile(std::string const& name);
/// SkyImage.load: the size the picture is resampled to (2:1, width clamped 2048..4096).
void pictureSize(int w, int h, int& outW, int& outH);

} // namespace bf::lyrics::sky
