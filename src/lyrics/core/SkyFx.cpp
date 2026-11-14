// LyricsFlow port - sky layers (WorldSky). See SkyFx.h. Constants and formulas follow the 2.1 bytecode.
#include "lyrics/core/SkyFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "lyrics/core/Config.h"
#include "lyrics/core/Perf.h"

namespace bf::lyrics::sky {

namespace {
constexpr double kPi = 3.14159265358979323846;
double           rad(double d) { return d * kPi / 180.0; }

struct V3 {
    double x = 0, y = 0, z = 0;
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double k) const { return {x * k, y * k, z * k}; }
};
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
V3 at(double az, double el) { double d[3]; dirOf(az, el, d); return {d[0], d[1], d[2]}; }

uint32_t lerpColor(uint32_t a, uint32_t b, float t) { // Gfx.lerpColor
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](int sh) {
        float x = float((a >> sh) & 255), y = float((b >> sh) & 255);
        return uint32_t(std::lround(x + (y - x) * t)) & 255;
    };
    return ch(16) << 16 | ch(8) << 8 | ch(0);
}

Vert vt(V3 p, float u, float v, uint32_t rgb, float a) { return {float(p.x), float(p.y), float(p.z), u, v, rgb, a}; }

void quad(std::vector<Quad>& out, Layer l, Tex t, Vert a, Vert b, Vert c, Vert d) { out.push_back({{a, b, c, d}, l, t}); }

// WorldSky.billboard: flat square facing the camera
void billboard(std::vector<Quad>& out, Cam const& c, V3 p, double size, uint32_t rgb, float a) {
    double h = size / 2;
    V3     r{c.rx, c.ry, c.rz}, u{c.ux, c.uy, c.uz};
    quad(out, Layer::Sky, Tex::Solid, vt(p - r * h - u * h, 0, 0, rgb, a), vt(p + r * h - u * h, 0, 0, rgb, a),
         vt(p + r * h + u * h, 0, 0, rgb, a), vt(p - r * h + u * h, 0, 0, rgb, a));
}
// WorldSky.streak: a band from a to b, `w` wide, alpha aA at a and aB at b
void streak(std::vector<Quad>& out, V3 a, V3 b, double w, uint32_t rgb, float aA, float aB) {
    V3     d = b - a, m = (a + b) * 0.5, s = cross(d, m);
    double l = len(s);
    if (l < 1e-6) return;
    s = s * (w / 2 / l);
    quad(out, Layer::Sky, Tex::Solid, vt(a - s, 0, 0, rgb, aA), vt(a + s, 0, 0, rgb, aA), vt(b + s, 0, 0, rgb, aB),
         vt(b - s, 0, 0, rgb, aB));
}
// WorldSky.softGlow: 28-segment disc, `a` in the middle fading to 0 at radius r
void softGlow(std::vector<Quad>& out, Cam const& c, V3 p, double r, uint32_t rgb, float a) {
    V3 rr{c.rx, c.ry, c.rz}, uu{c.ux, c.uy, c.uz};
    for (int i = 0; i < 28; ++i) {
        double t0 = i * kPi * 2 / 28, t1 = (i + 1) * kPi * 2 / 28;
        V3     p0 = p + rr * (std::cos(t0) * r) + uu * (std::sin(t0) * r);
        V3     p1 = p + rr * (std::cos(t1) * r) + uu * (std::sin(t1) * r);
        quad(out, Layer::Sky, Tex::Solid, vt(p, 0, 0, rgb, a), vt(p, 0, 0, rgb, a), vt(p1, 0, 0, rgb, 0), vt(p0, 0, 0, rgb, 0));
    }
}

// --- world-layer sprites (weather), camera-relative blocks
void sprite(std::vector<Quad>& out, Cam const& c, V3 p, double size, int frame, uint32_t rgb, float a) {
    if (a <= 0.02f) return;
    double h = size / 2;
    float  u0 = float(frame) * 0.25f, u1 = u0 + 0.25f;
    V3     r{c.rx, c.ry, c.rz}, u{c.ux, c.uy, c.uz};
    quad(out, Layer::World, Tex::Sprite, vt(p - r * h - u * h, u0, 1, rgb, a), vt(p + r * h - u * h, u1, 1, rgb, a),
         vt(p + r * h + u * h, u1, 0, rgb, a), vt(p - r * h + u * h, u0, 0, rgb, a));
}
void spriteStreak(std::vector<Quad>& out, V3 tail, V3 head, double w, uint32_t rgb, float a) {
    if (a <= 0.02f) return;
    V3     d = head - tail, m = (tail + head) * 0.5, s = cross(d, m);
    double l = len(s);
    if (l < 1e-6) return;
    s = s * (w / 2 / l);
    quad(out, Layer::World, Tex::Sprite, vt(head - s, 0.75f, 1, rgb, a), vt(head + s, 1, 1, rgb, a),
         vt(tail + s, 1, 0, rgb, a), vt(tail - s, 0.75f, 0, rgb, a));
}
void petal(std::vector<Quad>& out, V3 p, double size, double ax, double ay, double az, double ang, uint32_t rgb, float a) {
    if (a <= 0.02f) return;
    double c = std::cos(ang), s = std::sin(ang), k = 1 - c;
    V3     e1{c + ax * ax * k, ay * ax * k + az * s, az * ax * k - ay * s};
    V3     e2{ax * az * k + ay * s, ay * az * k - ax * s, c + az * az * k};
    double h = size / 2;
    e1 = e1 * h; e2 = e2 * h;
    quad(out, Layer::World, Tex::Sprite, vt(p - e1 - e2, 0, 1, rgb, a), vt(p + e1 - e2, 0.25f, 1, rgb, a),
         vt(p + e1 + e2, 0.25f, 0, rgb, a), vt(p - e1 + e2, 0, 0, rgb, a));
}
double wrap(double v, double half) { // WorldSky.wrap
    double span = half * 2;
    v = std::fmod(v + half, span);
    if (v < 0) v += span;
    return v - half;
}

// WorldSky.CONST: four little constellations (az, el)
std::vector<std::vector<std::pair<double, double>>> const kConst = {
    {{30, 48}, {36, 50}, {42, 49}, {47, 46}, {50, 41}, {57, 42}, {56, 47}},
    {{160, 38}, {165, 44}, {171, 40}, {177, 46}, {183, 41}},
    {{250, 30}, {254, 36}, {258, 30}, {251, 25}, {257, 24}, {254, 36}, {254, 43}},
    {{320, 55}, {326, 58}, {331, 55}, {326, 52}, {320, 55}},
};
} // namespace

void dirOf(double azDeg, double elDeg, double out[3]) {
    double a = rad(azDeg), e = rad(elDeg), ce = std::cos(e);
    out[0] = -std::sin(a) * ce; out[1] = std::sin(e); out[2] = std::cos(a) * ce;
}

Cam Cam::of(double yawDeg, double pitchDeg) {
    Cam    c;
    double y = rad(yawDeg), p = rad(pitchDeg);
    c.yaw = yawDeg; c.pitch = pitchDeg;
    c.fx = -std::sin(y) * std::cos(p); c.fy = -std::sin(p); c.fz = std::cos(y) * std::cos(p);
    c.rx = -std::cos(y); c.ry = 0; c.rz = -std::sin(y);
    c.ux = c.ry * c.fz - c.rz * c.fy; c.uy = c.rz * c.fx - c.rx * c.fz; c.uz = c.rx * c.fy - c.ry * c.fx;
    return c;
}

// ---------------------------------------------------------------- java.util.Random
int Fx::Rnd::next(int bits) {
    s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return int(int64_t(s) >> (48 - bits));
}
float  Fx::Rnd::nextFloat() { return float(next(24)) / float(1 << 24); }
double Fx::Rnd::nextDouble() { return double((int64_t(next(26)) << 27) + next(27)) * 0x1.0p-53; }
bool   Fx::Rnd::nextBoolean() { return next(1) != 0; }
int    Fx::Rnd::nextInt(int bound) {
    int r = next(31), m = bound - 1;
    if ((bound & m) == 0) return int((int64_t(bound) * int64_t(r)) >> 31);
    for (int u = r; u - (r = u % bound) + m < 0; u = next(31)) {}
    return r;
}
double Fx::Rnd::nextGaussian() {
    if (haveG) { haveG = false; return g; }
    double v1, v2, q;
    do {
        v1 = 2 * nextDouble() - 1; v2 = 2 * nextDouble() - 1; q = v1 * v1 + v2 * v2;
    } while (q >= 1 || q == 0);
    double m = std::sqrt(-2 * std::log(q) / q);
    g = v2 * m; haveG = true;
    return v1 * m;
}

// ---------------------------------------------------------------- layers
void Fx::stars(Preset const& p, Config const& cfg, int quality, float alpha, double time, Cam const& c,
               std::vector<Quad>& out) const {
    std::vector<StarDraw> sd;
    float                 f[3] = {float(c.fx), float(c.fy), float(c.fz)};
    drawStars(p, cfg, quality, alpha, time, f, sd);
    for (auto const& s : sd) billboard(out, c, {s.x, s.y, s.z}, s.size, kStarColor, s.alpha);
}

void Fx::constellations(float alpha, Cam const& c, std::vector<Quad>& out) const {
    // [BF] audit: quieter. Small stars, hairline links that barely read (a star chart, not fairy lights)
    double const u = 0.0016;
    for (auto const& k : kConst)
        for (size_t i = 0; i < k.size(); ++i) {
            V3 a = at(k[i].first, k[i].second);
            billboard(out, c, a, u * 1.9, 0xF4F0FF, 0.75f * alpha);
            if (i + 1 < k.size()) streak(out, a, at(k[i + 1].first, k[i + 1].second), u * 0.45, 0xD8CCFF, 0.09f * alpha, 0.09f * alpha);
        }
}

void Fx::meteorBurst(int n, int64_t nowMs) {
    if (mT0 < 0) mT0 = nowMs;
    n = std::clamp(n, 0, 16);
    for (int i = 0; i < n; ++i) {
        Shot s;
        s.az   = float(c_yaw + (float(i) / std::max(1, n - 1) - 0.5f) * 110.0f + (mRnd.nextFloat() - 0.5f) * 14.0f);
        s.el   = 38.0f + mRnd.nextFloat() * 32.0f;
        s.born = float(nowMs - mT0 + int64_t(i) * 70 + mRnd.nextInt(60));
        s.dir  = i % 2 ? 1.0f : -1.0f;
        s.big  = 1;
        mShooting.push_back(s);
    }
}

void Fx::wave(float t, uint32_t rgb, float alpha, std::vector<Quad>& out) const {
    if (t < 0 || t > 1 || alpha <= 0.004f) return;
    // [BF] audit: near the zenith the ring is tiny, and 64 wide segments piled up into a block of squares.
    // Start lower, fade in, and thin the strokes with the ring's own size so it reads as one clean line of light.
    float const e  = 1.0f - (1.0f - t) * (1.0f - t);              // fast out of the top, slowing at the horizon
    double const el = 74.0 - 72.0 * double(e);
    float const a  = alpha * std::min(1.0f, t * 6.0f) * std::sin(3.14159265f * std::min(1.0f, t * 1.1f)) * (1.0f - 0.5f * t);
    double const k = std::clamp(std::cos(el * 3.14159265 / 180.0) / 0.8, 0.3, 1.0);
    int const   N  = 96;
    for (int i = 0; i < N; ++i) {
        double const a0 = i * 360.0 / N, a1 = (i + 1) * 360.0 / N;
        streak(out, at(a0, el), at(a1, el), 0.0045 * k, 0xFFFFFF, a * 0.9f, a * 0.9f);              // the bright edge
        streak(out, at(a0, el + 1.6), at(a1, el + 1.6), 0.026 * k, rgb, a * 0.32f, a * 0.32f);       // its glow
        streak(out, at(a0, el + 4.5), at(a1, el + 4.5), 0.06 * k, rgb, a * 0.10f, a * 0.10f);        // the trail
    }
}

void Fx::shooting(float alpha, int64_t nowMs, Cam const& c, std::vector<Quad>& out, bool spawn) {
    if (mT0 < 0) mT0 = nowMs;
    if (spawn && nowMs > mNextShooting) {
        mNextShooting = nowMs + 1200 + mRnd.nextInt(3500);
        Shot s;
        s.az   = float(c_yaw + (mRnd.nextFloat() - 0.5f) * 80.0f);
        s.el   = 30.0f + mRnd.nextFloat() * 30.0f;
        s.born = float(nowMs - mT0);
        s.dir  = mRnd.nextBoolean() ? 1.0f : -1.0f;
        mShooting.push_back(s);
    }
    std::erase_if(mShooting, [&](Shot const& s) {
        int64_t age = nowMs - (mT0 + int64_t(s.born));
        return age > (s.big > 0 ? 1500 : 900) || age < -1000;
    });
    for (auto const& s : mShooting) {
        float const life = s.big > 0 ? 1500.0f : 900.0f;
        float  raw = float(nowMs - (mT0 + int64_t(s.born))) / life;
        if (raw < 0) continue; // a burst meteor that has not started yet
        float  t  = std::clamp(raw, 0.0f, 1.0f);
        if (s.big > 0) { // [BF] chorus meteors: longer, wider, a warm head
            double az = s.az + s.dir * t * 42.0f, el = s.el - t * 26.0f;
            V3     head = at(az, el), tail = at(az - s.dir * 14.0f, el + 8.0);
            float const a = alpha * (1 - t * t);
            float const in = std::min(1.0f, t * 12.0f); // no pop-in
            // [LyricsForge] a tapered white-hot core, a sheath in the sky's own colour, a lingering train, a bright head
            V3 const mid = at(az - s.dir * 6.0f, el + 3.4);
            streak(out, at(az - s.dir * 22.0f, el + 12.5), mid, 0.012, mTint, 0.0f, a * in * 0.10f);  // the fading train
            streak(out, tail, head, 0.0058, 0xFFFBF2, 0.0f, a * in);
            streak(out, mid, head, 0.0105, 0xFFFFFF, 0.0f, a * in * 0.45f);                            // brighter near the head
            streak(out, at(az - s.dir * 9.0f, el + 5.2), head, 0.022, mTint, 0.0f, a * in * 0.30f);
            softGlow(out, c, head, 0.016, 0xFFFFFF, a * in * 0.9f);
            softGlow(out, c, head, 0.045, mTint, a * in * 0.35f);
            continue;
        }
        double az = s.az + s.dir * t * 30.0f, el = s.el - t * 12.0f;
        V3     head = at(az, el), tail = at(az - s.dir * 6.0f, el + 2.4);
        float const a = alpha * (1 - t) * std::min(1.0f, t * 10.0f);
        streak(out, tail, head, 0.0042, 0xFFFFFF, 0.0f, a);
        streak(out, at(az - s.dir * 3.5f, el + 1.4), head, 0.011, mTint, 0.0f, a * 0.25f);
        softGlow(out, c, head, 0.010, 0xFFFFFF, a * 0.7f);
    }
}

void Fx::storm(float alpha, double time, Cam const& c, std::vector<Quad>& out, bool inView) {
    if (time >= mFlashNext) { // flashLevel
        mFlashT0   = time;
        mFlashNext = time + 0.5 + mRnd.nextDouble() * 2.0;
        double az  = inView ? c_yaw + (mRnd.nextDouble() - 0.5) * 90.0 : mRnd.nextDouble() * 360.0; // [BF] Moments: where you look
        // [BF] audit: a real-looking bolt. Midpoint displacement (5 levels, 33 points) instead of 10 straight
        // segments, plus one fork that splits off part way down and thins out.
        auto bolt = [&](double a0, double e0, double a1, double e1, double rough) {
            std::vector<double> pts{a0, e0, a1, e1};
            for (int lvl = 0; lvl < 5; ++lvl) {
                std::vector<double> nx;
                for (size_t i = 0; i + 3 < pts.size(); i += 2) {
                    double ma = (pts[i] + pts[i + 2]) * 0.5, me = (pts[i + 1] + pts[i + 3]) * 0.5;
                    double len = std::hypot(pts[i + 2] - pts[i], pts[i + 3] - pts[i + 1]);
                    ma += (mRnd.nextDouble() - 0.5) * len * rough;
                    nx.insert(nx.end(), {pts[i], pts[i + 1], ma, me});
                }
                nx.insert(nx.end(), {pts[pts.size() - 2], pts.back()});
                pts.swap(nx);
            }
            return pts;
        };
        double const endAz = az + (mRnd.nextDouble() - 0.5) * 16.0;
        mBolt = bolt(az, 60.0, endAz, 6.0, 0.55);
        size_t const k = (mBolt.size() / 2) * (3 + mRnd.nextInt(3)) / 10; // fork from 30..50% down
        double const fa = mBolt[k * 2], fe = mBolt[k * 2 + 1];
        mFork = bolt(fa, fe, fa + (mRnd.nextBoolean() ? 1 : -1) * (8.0 + mRnd.nextDouble() * 8.0), fe - 14.0 - mRnd.nextDouble() * 10.0, 0.6);
        mBoltAz = az;
    }
    double dt = time - mFlashT0;
    float  fl = dt < 0.05 ? 1.0f : dt < 0.12 ? 0.25f : dt < 0.18 ? 0.9f : dt < 0.6 ? float(0.8 * std::exp(-(dt - 0.18) * 9.0)) : 0.0f;
    if (fl <= 0.02f || mBolt.size() < 4) return;
    softGlow(out, c, at(mBoltAz, 52), 0.55, 0xC8D4FF, 0.30f * fl * alpha); // the cloud lit from inside, behind the bolt
    auto draw = [&](std::vector<double> const& b, float w, float a) {
        size_t const n = b.size() / 2;
        for (size_t i = 0; i + 1 < n; ++i) {
            float const taper = 1.0f - 0.55f * float(i) / float(std::max<size_t>(1, n - 1)); // thinner towards the ground
            V3 p0 = at(b[i * 2], b[i * 2 + 1]), p1 = at(b[i * 2 + 2], b[i * 2 + 3]);
            streak(out, p0, p1, 0.0105 * w * taper, 0x9FB4FF, fl * alpha * a * 0.22f, fl * alpha * a * 0.22f); // glow
            streak(out, p0, p1, 0.0022 * w * taper, 0xF4F7FF, fl * alpha * a, fl * alpha * a);                 // hot core
        }
    };
    draw(mBolt, 1.0f, 1.0f);
    if (mFork.size() >= 4) draw(mFork, 0.6f, 0.65f);
}

void Fx::weather(Weather w, float amount, int quality, float outside, double dt, double time, FxIn const& in,
                 std::vector<Quad>& out) {
    static float const kQ[3] = {0.3f, 0.55f, 0.85f};
    int base = w == Weather::Blizzard ? 240 : w == Weather::Snow ? 200 : (w == Weather::Rain || w == Weather::Thunder) ? 220
             : w == Weather::Petals ? 150 : w == Weather::Embers ? 80 : w == Weather::Fireflies ? 45 : w == Weather::SpaceDust ? 140 : 0;
    int n = int(float(base) * amount * kQ[std::clamp(quality, 0, 2)]);
    double const H = 14, V = 9;
    if (mPartType != int(w) || int(mParts.size()) != n) {
        mPartType = int(w);
        mParts.assign(size_t(n), Part{});
        for (auto& q : mParts) {
            double* p = q.p;
            p[0] = in.camX + (mRnd.nextDouble() * 2 - 1) * H;
            p[1] = in.camY + (mRnd.nextDouble() * 2 - 1) * V;
            p[2] = in.camZ + (mRnd.nextDouble() * 2 - 1) * H;
            p[3] = mRnd.nextDouble() * 6.28;
            p[4] = 0.6 + mRnd.nextDouble() * 0.8;
            double gx = mRnd.nextGaussian(), gy = mRnd.nextGaussian() * 0.6, gz = mRnd.nextGaussian();
            double l  = std::sqrt(gx * gx + gy * gy + gz * gz) + 1e-9;
            p[5] = gx / l; p[6] = gy / l; p[7] = gz / l;
            p[8]  = (mRnd.nextBoolean() ? 1 : -1) * (1.2 + mRnd.nextDouble() * 2.6);
            p[9]  = 0.75 + mRnd.nextDouble() * 0.6;
            p[10] = mRnd.nextDouble();
        }
    }
    Cam const& c = in.cam;
    for (auto& q : mParts) {
        double* p  = q.p;
        double  ph = p[3], sp = p[4], vx, vy, vz;
        switch (w) {
        case Weather::Blizzard: vx = -8.5 * sp; vy = -4.0 * sp; vz = 2.8 * sp; break;
        case Weather::Snow:
            vx = 0.4 + 0.5 * std::sin(time * 0.8 + ph); vy = -1.3 * sp; vz = 0.3 * std::cos(time * 0.7 + ph); break;
        case Weather::Rain:
        case Weather::Thunder: vx = 0.6; vy = -17.0 * sp; vz = 0.2; break;
        case Weather::Petals:
            vx = 0.7 + 0.8 * std::sin(time * 0.9 + ph);
            vy = -0.55 * sp - 0.25 * std::sin(time * 2.1 + ph * 2.0);
            vz = 0.45 * std::cos(time * 0.7 + ph);
            break;
        case Weather::Embers:
            vx = 0.3 * std::sin(time * 1.3 + ph); vy = 0.9 * sp; vz = 0.3 * std::cos(time + ph); break;
        case Weather::SpaceDust: // [LyricsForge] weightless motes: a slow 3D drift that curls, nothing falls
            vx = 0.12 * std::sin(time * 0.21 + ph) * sp; vy = 0.08 * std::sin(time * 0.17 + ph * 2.0); vz = 0.12 * std::cos(time * 0.19 + ph) * sp;
            break;
        default: // fireflies
            vx = 0.4 * std::sin(time * 0.5 + ph); vy = 0.25 * std::sin(time * 0.7 + ph * 2.0); vz = 0.4 * std::cos(time * 0.45 + ph);
        }
        p[0] += vx * dt; p[1] += vy * dt; p[2] += vz * dt;
        double rx = wrap(p[0] - in.camX, H), ry = wrap(p[1] - in.camY, V), rz = wrap(p[2] - in.camZ, H);
        p[0] = in.camX + rx; p[1] = in.camY + ry; p[2] = in.camZ + rz;
        if (rx * c.fx + ry * c.fy + rz * c.fz < 0.3) continue;
        double dist = std::sqrt(rx * rx + ry * ry + rz * rz);
        float  nearK = float(std::min(1.0, 5.0 / std::max(0.5, dist)));
        float  fade  = float(1 - std::max(0.0, std::min(1.0, (dist - 9.0) / 6.0)));
        V3     pos{rx, ry, rz};
        switch (w) {
        case Weather::Blizzard:
            spriteStreak(out, pos - V3{vx, vy, vz} * 0.06, pos, 0.07, 0xF4F2FF, outside * fade * (0.45f + 0.4f * nearK)); break;
        case Weather::Snow: sprite(out, c, pos, 0.13 * p[9], 1, 0xFFFFFF, outside * fade * (0.6f + 0.4f * nearK)); break;
        case Weather::Rain:
        case Weather::Thunder:
            spriteStreak(out, pos - V3{vx, vy, vz} * 0.045, pos, 0.022, 0xDCE6F8, outside * fade * (0.38f + 0.4f * nearK)); break;
        case Weather::Petals:
            petal(out, pos, 0.16 * p[9], p[5], p[6], p[7], p[3] + time * p[8], lerpColor(0xFFFFFF, 0xFFD2E6, float(p[10])),
                  outside * fade * 0.95f);
            break;
        case Weather::Embers: { // [BF] a slower, irregular flicker (9 Hz read as strobing sparks)
            float fl = float(0.5 + 0.5 * std::sin(time * 3.7 + ph * 3.0) * std::sin(time * 1.3 + ph));
            sprite(out, c, pos, 0.22 * p[9], 2, lerpColor(0xFF3B1F, 0xFFC04A, fl), outside * fade * (0.5f + 0.5f * fl));
            break;
        }
        case Weather::SpaceDust: { // [LyricsForge] tiny coloured motes that twinkle; they read as being IN space
            static uint32_t const kCol[6] = {0x9EC8FF, 0xFFFFFF, 0xFFD38A, 0xFF8A70, 0x8AF2FF, 0xD9A0FF};
            float tw = float(0.35 + 0.65 * std::pow(0.5 + 0.5 * std::sin(time * (1.1 + p[9]) + ph * 5.0), 3.0));
            sprite(out, c, pos, 0.035 + 0.04 * p[10], 2, kCol[int(p[10] * 5.999)], outside * fade * (0.25f + 0.6f * tw) * (0.5f + 0.5f * nearK));
            break;
        }
        default: { // [BF] fireflies: sizes vary, slow breathing glow, colour from the sky (gold under warm skies)
            float gl = float(std::pow(0.5 + 0.5 * std::sin(time * 1.4 + ph * 4.0), 2.0));
            sprite(out, c, pos, 0.32 + 0.22 * (p[9] - 0.75) / 0.6, 2, in.fireflyRgb, outside * fade * (0.12f + 0.78f * gl));
        }
        }
    }
}

// [LyricsForge] lightning only belongs to dark / stormy skies (a bolt out of a clear blue day looked wrong)
bool darkSky(Preset const& p) {
    auto lum = [](uint32_t c) { return (0.2126f * float((c >> 16) & 255) + 0.7152f * float((c >> 8) & 255) + 0.0722f * float(c & 255)) / 255.0f; };
    if (0.5f * (lum(p.top) + lum(p.horizon)) >= 0.32f) return false;
    // ...and to skies with weight in them: a bolt across a clear star field (galaxy, bright night) read as a glitch
    bool const heavy = p.weather == Weather::Rain || p.weather == Weather::Thunder || p.weather == Weather::Embers ||
                       p.weather == Weather::Blizzard;
    return heavy || p.stars < 0.3f;
}

void Fx::frame(Preset const* p, Config const& cfg, AutoPerf const& perf, Frame const& f, FxIn const& in,
               std::vector<Quad>& out) {
    c_yaw = in.cam.yaw;
    if (!p) return;
    mTint = lerpColor(p->aurora1 ? p->aurora1 : p->lyricColor, 0xFFFFFF, 0.25f);
    if (in.picture) return; // the picture replaces the whole sky (WorldSky skips everything else)
    if (f.visible <= 0.01f) return;
    int q = std::clamp(in.quality, 0, 2);
    // [LyricsForge] the reworked skies draw their own stars (real magnitudes, colours, the Milky Way); the old dot
    // stars stay only on the skies that still use them
    bool const ownStars = p->ordinal == 14 || p->ordinal == 16 || p->ordinal == 31;
    if (ownStars && p->stars > 0 && cfg.fxStars && perf.allow(5))
        stars(*p, cfg, q, f.visible * f.bright * (0.85f + 0.3f * f.beat), f.time, in.cam, out);
    if (p->stars >= 0.8f && cfg.fxStars && cfg.fxConstellations && perf.allow(2)) // [BF] only real night skies
        constellations(f.visible * f.bright * std::clamp((p->stars - 0.8f) * 5.0f, 0.0f, 1.0f), in.cam, out);
    if (std::string_view(p->id) == "storm" && perf.allow(2)) storm(f.visible, f.time, in.cam, out);
    else if (in.lightning > 0.05f && perf.allow(2) && darkSky(*p)) storm(f.visible * in.lightning, f.time, in.cam, out, true); // [BF] Moments (not on a clear day)
    if (in.meteors > 0) meteorBurst(in.meteors, in.nowMs);
    bool const spawn = (p->shootingStars || p->stars >= 0.8f) && perf.allow(3);
    if (spawn || !mShooting.empty()) shooting(f.visible, in.nowMs, in.cam, out, spawn);
    if (in.eq && !in.eq->empty() && in.eqAlpha > 0.004f && perf.allow(3)) { // [BF] the horizon equaliser, on the skyline
        size_t const n = in.eq->size();
        float const a = in.eqAlpha * f.visible;
        uint32_t const tip = ((((in.eqColor >> 16) & 0xFF) * 2 + 255) / 3) << 16 | ((((in.eqColor >> 8) & 0xFF) * 2 + 255) / 3) << 8 |
                             ((in.eqColor & 0xFF) * 2 + 255) / 3;
        uint32_t const glowC = lerpColor(in.eqColor, mTint, 0.4f);
        for (size_t i = 0; i < n; ++i) { // [LyricsForge] pillars of light: bright on the skyline, fading as they rise
            double const az = double(i) * 360.0 / double(n), top = double((*in.eq)[i]);
            streak(out, at(az, -0.6), at(az, top), 0.024, glowC, a * 0.16f, 0.0f);   // the soft column
            streak(out, at(az, -0.6), at(az, top), 0.0026, tip, a * 0.7f, a * 0.18f); // its core
            softGlow(out, in.cam, at(az, top), 0.006, tip, a * 0.45f);               // a little light at the top
        }
    }
    // [LyricsForge] the chorus light wave is drawn by the sky shader now (a soft sweep in the sky's own colours)
    Weather wx = cfg.snowAnySky ? Weather::Snow : p->weather; // LyricsFlow 2.2: snow with any sky
    float   wa = cfg.weatherAmount;
    if (in.burst != Weather::None && in.burstAmt > 0.02f) { wx = in.burst; wa = std::max(0.6f, cfg.weatherAmount) * std::min(1.0f, in.burstAmt); } // [BF] Moments
    if (cfg.fxWeather && perf.allow(4) && wa > 0.01f && wx != Weather::None && f.outside > 0.01f)
        weather(wx, wa * (perf.allow(3) ? 1.0f : 0.5f), q, f.outside, std::min(0.1, in.dtSec), f.time,
                in, out);
}

// ---------------------------------------------------------------- My picture
bool isPictureFile(std::string const& name) {
    std::string l = name;
    for (auto& ch : l) ch = char(std::tolower((unsigned char)ch));
    auto ends = [&](char const* e) { size_t n = std::char_traits<char>::length(e); return l.size() >= n && l.compare(l.size() - n, n, e) == 0; };
    return ends(".png") || ends(".jpg") || ends(".jpeg");
}
std::string pictureChoice(Config const& cfg, std::vector<std::string> files) {
    std::erase_if(files, [](std::string const& s) { return !isPictureFile(s); });
    std::sort(files.begin(), files.end());
    if (files.empty()) return "";
    if (!cfg.skyImageFile.empty() && std::find(files.begin(), files.end(), cfg.skyImageFile) != files.end()) return cfg.skyImageFile;
    return files.front();
}
void pictureSize(int w, int, int& outW, int& outH) {
    outW = std::max(2048, std::min(4096, w));
    outH = outW / 2;
}

} // namespace bf::lyrics::sky
