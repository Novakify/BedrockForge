// LyricsFlow port - small vector/quaternion math + LyricsFlow's animation helpers (dev.lyricsflow.util.Anim) and
// Minecraft's yaw/pitch conventions (dev.lyricsflow.render.Proj). Original code for Bedrock Forge except where noted.
// Conventions (same as Java and Bedrock): +Y up; yaw 0 looks toward +Z, yaw grows turning right (toward -X);
// pitch > 0 looks down. Degrees everywhere in the public API.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bf::lyrics {

inline constexpr double kPi = 3.14159265358979323846;
inline double rad(double d) { return d * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    double dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3   cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    double dist(Vec3 o) const { return (*this - o).length(); }
    Vec3   normalized() const { double l = length(); return l > 1e-12 ? *this * (1.0 / l) : Vec3{}; }
};

struct BlockPos {
    int x = 0, y = 0, z = 0;
    static BlockPos of(Vec3 p) { return {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))}; }
    bool operator==(BlockPos const&) const = default;
};

struct Aabb {
    double x0 = 0, y0 = 0, z0 = 0, x1 = 1, y1 = 1, z1 = 1;
};

/// Unit quaternion (x, y, z, w), JOML conventions (Quaternionf.rotationYXZ, transform).
struct Quat {
    double x = 0, y = 0, z = 0, w = 1;
    /// JOML rotationYXZ(angleY, angleX, angleZ): rotate Z, then X, then Y (radians).
    static Quat rotationYXZ(double ay, double ax, double az) {
        double sx = std::sin(ax * 0.5), cx = std::cos(ax * 0.5);
        double sy = std::sin(ay * 0.5), cy = std::cos(ay * 0.5);
        double sz = std::sin(az * 0.5), cz = std::cos(az * 0.5);
        double x = cy * sx, y = sy * cx, z = sy * sx, w = cy * cx;
        return {x * cz + y * sz, y * cz - x * sz, w * sz - z * cz, w * cz + z * sz};
    }
    static Quat rotationZ(double a) { return {0, 0, std::sin(a * 0.5), std::cos(a * 0.5)}; }
    static Quat axisAngle(Vec3 axis, double a) {
        Vec3   n = axis.normalized();
        double s = std::sin(a * 0.5);
        return {n.x * s, n.y * s, n.z * s, std::cos(a * 0.5)};
    }
    Quat operator*(Quat q) const { // this * q (apply q first)
        return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
    }
    Vec3 rotate(Vec3 v) const {
        Vec3 u{x, y, z};
        Vec3 t = u.cross(v) * 2.0;
        return v + t * w + u.cross(t);
    }
    Quat normalized() const {
        double l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-12 ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
};

// ---------------------------------------------------------------- Proj (LyricsFlow)
/// Direction for a Minecraft yaw/pitch (Proj.dir).
inline Vec3 dir(double yawDeg, double pitchDeg) {
    double yr = rad(yawDeg), pr = rad(pitchDeg);
    return {-std::sin(yr) * std::cos(pr), -std::sin(pr), std::cos(yr) * std::cos(pr)};
}
/// yaw/pitch (degrees) of an offset vector, the inverse of dir().
inline double yawOf(Vec3 d) { return deg(std::atan2(-d.x, d.z)); }
inline double pitchOf(Vec3 d) {
    double l = d.length();
    return l < 1e-4 ? 0.0 : -deg(std::asin(std::clamp(d.y / l, -1.0, 1.0)));
}
/// WorldLyrics.wrap: degrees into (-180, 180].
inline float wrap(float d) {
    d = std::fmod(d, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

// ---------------------------------------------------------------- Anim (LyricsFlow)
namespace anim {
inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
/// Frame-rate independent exponential approach (Anim.approach).
inline float approach(float cur, float target, float speed, float dt) {
    float k = 1.0f - float(std::exp(double(-speed * dt)));
    float v = cur + (target - cur) * k;
    return std::fabs(target - v) < 5.0e-4f ? target : v;
}
inline float easeOutCubic(float t) { t = clamp01(t); float f = 1 - t; return 1 - f * f * f; }
inline float easeInCubic(float t) { t = clamp01(t); return t * t * t; }
inline float easeInOut(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - float(std::pow(double(-2 * t + 2), 3.0)) / 2;
}
inline float easeOutBack(float t) {
    t = clamp01(t);
    float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * float(std::pow(double(t - 1), 3.0)) + c1 * float(std::pow(double(t - 1), 2.0));
}
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float progress(double t, double a, double b) {
    if (b <= a) return t >= b ? 1.0f : 0.0f;
    return clamp01(float((t - a) / (b - a)));
}
/// Anim.hash01 - same 64-bit mixing as Java (two's complement wraparound, logical shifts).
inline float hash01(int64_t seed) {
    uint64_t x = uint64_t(seed) * uint64_t(-7046029254386353131LL);
    x ^= x >> 31;
    x *= uint64_t(-4658895280553007687LL);
    x ^= x >> 27;
    return float(x >> 40) / 1.6777216E7f;
}
/// WorldLyrics.rnd
inline float rnd(int64_t seed, int k) { return hash01(int64_t(uint64_t(seed) * 31ULL + uint64_t(int64_t(k)))); }
} // namespace anim

} // namespace bf::lyrics
