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
    double const u = 0.0016;
    for (auto const& k : kConst)
        for (size_t i = 0; i < k.size(); ++i) {
            V3 a = at(k[i].first, k[i].second);
            billboard(out, c, a, u * 2.6, 0xFFFFFF, 0.9f * alpha);
            if (i + 1 < k.size()) streak(out, a, at(k[i + 1].first, k[i + 1].second), u * 0.7, 0xD8CCFF, 0.22f * alpha, 0.22f * alpha);
        }
}

void Fx::shooting(float alpha, int64_t nowMs, Cam const&, std::vector<Quad>& out) {
    if (mT0 < 0) mT0 = nowMs;
    if (nowMs > mNextShooting) {
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
        return age > 900 || age < -1000;
    });
    for (auto const& s : mShooting) {
        float  t  = std::clamp(float(nowMs - (mT0 + int64_t(s.born))) / 900.0f, 0.0f, 1.0f);
        double az = s.az + s.dir * t * 30.0f, el = s.el - t * 12.0f;
        V3     head = at(az, el), tail = at(az - s.dir * 6.0f, el + 2.4);
        streak(out, tail, head, 0.004, 0xFFFFFF, 0.0f, alpha * (1 - t));
    }
}

void Fx::storm(float alpha, double time, Cam const& c, std::vector<Quad>& out) {
    if (time >= mFlashNext) { // flashLevel
        mFlashT0   = time;
        mFlashNext = time + 0.5 + mRnd.nextDouble() * 2.0;
        int    n   = 10;
        double az  = mRnd.nextDouble() * 360.0;
        mBolt.assign(size_t(n) * 2, 0);
        for (int i = 0; i < n; ++i) {
            double t        = double(i) / (n - 1);
            mBolt[size_t(i) * 2]     = az + (mRnd.nextDouble() - 0.5) * 9.0;
            mBolt[size_t(i) * 2 + 1] = 58.0 - t * 48.0 + (mRnd.nextDouble() - 0.5) * 4.0;
            az += (mRnd.nextDouble() - 0.5) * 6.0;
        }
    }
    double dt = time - mFlashT0;
    float  fl = dt < 0.05 ? 1.0f : dt < 0.12 ? 0.25f : dt < 0.18 ? 0.9f : dt < 0.6 ? float(0.8 * std::exp(-(dt - 0.18) * 9.0)) : 0.0f;
    if (fl <= 0.02f || mBolt.size() < 4) return;
    softGlow(out, c, at(0, 70), 0.6, 0xC8D4FF, 0.35f * fl * alpha);
    for (size_t i = 0; i + 1 < mBolt.size() / 2; ++i) {
        V3 a = at(mBolt[i * 2], mBolt[i * 2 + 1]), b = at(mBolt[i * 2 + 2], mBolt[i * 2 + 3]);
        streak(out, a, b, 0.006, 0xB8C8FF, fl * alpha, fl * alpha);
        streak(out, a, b, 0.0025, 0xFFFFFF, fl * alpha, fl * alpha);
    }
}

void Fx::weather(Weather w, float amount, int quality, float outside, double dt, double time, FxIn const& in,
                 std::vector<Quad>& out) {
    static float const kQ[3] = {0.3f, 0.55f, 0.85f};
    int base = w == Weather::Blizzard ? 240 : w == Weather::Snow ? 200 : (w == Weather::Rain || w == Weather::Thunder) ? 220
             : w == Weather::Petals ? 150 : w == Weather::Embers ? 80 : w == Weather::Fireflies ? 45 : 0;
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
        case Weather::Snow: sprite(out, c, pos, 0.11 * p[9], 1, 0xFFFFFF, outside * fade * (0.55f + 0.4f * nearK)); break;
        case Weather::Rain:
        case Weather::Thunder:
            spriteStreak(out, pos - V3{vx, vy, vz} * 0.035, pos, 0.05, 0xC2D0EC, outside * fade * (0.3f + 0.35f * nearK)); break;
        case Weather::Petals:
            petal(out, pos, 0.16 * p[9], p[5], p[6], p[7], p[3] + time * p[8], lerpColor(0xFFFFFF, 0xFFD2E6, float(p[10])),
                  outside * fade * 0.95f);
            break;
        case Weather::Embers: {
            float fl = float(0.5 + 0.5 * std::sin(time * 9.0 + ph * 3.0));
            sprite(out, c, pos, 0.22 * p[9], 2, lerpColor(0xFF3B1F, 0xFFC04A, fl), outside * fade * (0.5f + 0.5f * fl));
            break;
        }
        default: {
            float gl = float(std::pow(0.5 + 0.5 * std::sin(time * 2.2 + ph * 4.0), 2.0));
            sprite(out, c, pos, 0.6, 2, 0xD6FF6A, outside * fade * (0.15f + 0.85f * gl));
        }
        }
    }
}

void Fx::frame(Preset const* p, Config const& cfg, AutoPerf const& perf, Frame const& f, FxIn const& in,
               std::vector<Quad>& out) {
    c_yaw = in.cam.yaw;
    if (!p) return;
    if (in.picture) return; // the picture replaces the whole sky (WorldSky skips everything else)
    if (f.visible <= 0.01f) return;
    int q = std::clamp(in.quality, 0, 2);
    if (p->stars > 0 && cfg.fxStars && perf.allow(5))
        stars(*p, cfg, q, f.visible * f.bright * (0.85f + 0.3f * f.beat), f.time, in.cam, out);
    if (p->stars > 0 && cfg.fxStars && cfg.fxConstellations && perf.allow(2)) constellations(f.visible * f.bright, in.cam, out);
    if (std::string_view(p->id) == "storm" && perf.allow(2)) storm(f.visible, f.time, in.cam, out);
    if ((p->shootingStars || p->stars >= 0.8f) && perf.allow(3)) shooting(f.visible, in.nowMs, in.cam, out);
    if (cfg.fxWeather && perf.allow(4) && cfg.weatherAmount > 0.01f && p->weather != Weather::None && f.outside > 0.01f)
        weather(p->weather, cfg.weatherAmount * (perf.allow(3) ? 1.0f : 0.5f), q, f.outside, std::min(0.1, in.dtSec), f.time,
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
