// LyricsFlow port - [BF] break-apart exit effects. See LetterFx.h.
#include "lyrics/core/LetterFx.h"

#include <algorithm>
#include <cmath>

namespace bf::lyrics {

namespace {
float h01(int64_t s, int k) { return anim::hash01(s * 1315423911LL + k * 2654435761LL); }
uint32_t mixRgba(uint32_t a, uint32_t b, float t) { // R in the low byte, alpha kept from a
    t = anim::clamp01(t);
    uint32_t o = a & 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) {
        float x = float((a >> sh) & 255), y = float((b >> sh) & 255);
        o |= uint32_t(std::lround(x + (y - x) * t)) << sh;
    }
    return o;
}
Vec3 v3(float const* f) { return {f[0], f[1], f[2]}; }
} // namespace

bool LetterFx::handles(ExitAnim m) {
    return m == ExitAnim::Shatter || m == ExitAnim::Dissolve || m == ExitAnim::Explode || m == ExitAnim::Burn || m == ExitAnim::Glitch;
}

void LetterFx::spawn(std::vector<BfGlyphV1> const& quads, Vec3 origin, ExitAnim mode, int64_t seed, float intensity) {
    if (quads.empty() || !handles(mode)) return;
    intensity = std::clamp(intensity, 0.2f, 2.0f);
    // word centre (fragments fly away from it)
    auto abs = [&](BfGlyphV1 const& q) { return v3(q.pos) + ((q.flags & BF_GLYPH_SCREEN) ? Vec3{} : origin); };
    Vec3 c{};
    for (auto const& q : quads) c += abs(q);
    c = c * (1.0 / double(quads.size()));
    int gx = 1, gy = 1;
    switch (mode) {
    case ExitAnim::Shatter: gx = 3; gy = 3; break;
    case ExitAnim::Dissolve: gx = 6; gy = 6; break;
    case ExitAnim::Burn: gx = 4; gy = 5; break;
    case ExitAnim::Glitch: gx = 1; gy = 5; break;
    default: break; // Explode: whole letters
    }
    int qi = 0;
    for (auto const& q : quads) {
        bool const screen = (q.flags & BF_GLYPH_SCREEN) != 0;
        Vec3 const R = v3(q.right), U = v3(q.up), P = abs(q);
        Vec3 const upDir = screen ? Vec3{0, -1, 0} : Vec3{0, 1, 0};
        double const letterH = std::max(1e-4, U.length() * 2.0);
        Vec3 const normal = R.cross(U).normalized();
        for (int j = 0; j < gy; ++j)
            for (int i = 0; i < gx; ++i) {
                if (mFrags.size() >= kMaxFrags) return;
                int  k = qi * 64 + j * gx + i;
                Frag f;
                f.tmpl   = q;
                f.mode   = mode;
                f.screen = screen;
                f.upDir  = upDir;
                f.size   = float(letterH);
                f.seedF  = h01(seed, k);
                double fx = (2.0 * i + 1) / gx - 1.0, fy = 1.0 - (2.0 * j + 1) / gy; // -1..1 inside the glyph
                f.pos   = P + R * fx + U * fy;
                f.right = R * (1.0 / gx);
                f.up    = U * (1.0 / gy);
                float u0 = q.uv[0], v0 = q.uv[1], u1 = q.uv[2], v1 = q.uv[3];
                f.tmpl.uv[0] = u0 + (u1 - u0) * float(i) / float(gx);
                f.tmpl.uv[2] = u0 + (u1 - u0) * float(i + 1) / float(gx);
                f.tmpl.uv[1] = v0 + (v1 - v0) * float(j) / float(gy);
                f.tmpl.uv[3] = v0 + (v1 - v0) * float(j + 1) / float(gy);
                f.baseFill = q.fill;
                Vec3 out = (f.pos - c);
                double ol = out.length();
                out = ol > 1e-9 ? out * (1.0 / ol) : upDir;
                double r1 = h01(seed, k * 3 + 1) * 2 - 1, r2 = h01(seed, k * 3 + 2) * 2 - 1, r3 = h01(seed, k * 3 + 3);
                Vec3 side = upDir.cross(normal).normalized();
                switch (mode) {
                case ExitAnim::Shatter:
                    f.vel    = (out * (2.2 + 2.0 * r3) + upDir * (2.0 + 1.5 * r3) + normal * (1.2 * r1)) * (letterH * intensity);
                    f.angVel = Vec3{r1, r2, r1 * r2}.normalized() * (5.0 + 5.0 * r3);
                    f.life   = 1.25f + 0.4f * float(r3);
                    f.delay  = 0.02f * float(qi);
                    break;
                case ExitAnim::Explode:
                    f.vel    = (out * (5.0 + 3.0 * r3) + normal * (2.5 * r1) + upDir * (1.0 + r2)) * (letterH * intensity);
                    f.angVel = Vec3{r2, r1, 1.0}.normalized() * (7.0 + 6.0 * r3);
                    f.life   = 0.95f + 0.3f * float(r3);
                    break;
                case ExitAnim::Dissolve:
                    f.vel    = (upDir * (0.5 + 0.9 * r3) + side * (0.45 * r1)) * (letterH * intensity);
                    f.angVel = Vec3{0, 0, 1} * (2.0 * r2);
                    f.delay  = 0.35f * float(h01(seed, k * 7 + 5)) + 0.04f * float(qi); // grains leave one by one
                    f.life   = 1.1f + 0.5f * float(r3);
                    break;
                case ExitAnim::Burn:
                    f.vel    = (upDir * (0.9 + 0.8 * r3) + side * (0.25 * r1)) * (letterH * intensity);
                    f.angVel = Vec3{r1, r2, 0.3}.normalized() * (1.5 + 2.0 * r3);
                    f.delay  = 0.45f * (1.0f - float(j) / float(std::max(1, gy - 1))) + 0.05f * float(qi); // bottom rows first
                    f.life   = 1.0f + 0.4f * float(r3);
                    break;
                case ExitAnim::Glitch:
                    f.vel    = Vec3{};
                    f.life   = 0.55f + 0.25f * float(r3);
                    f.jitter = float(letterH) * (0.25f + 0.35f * float(r3)) * intensity;
                    break;
                default: break;
                }
                mFrags.push_back(f);
            }
        ++qi;
    }
}

void LetterFx::step(float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    for (auto& f : mFrags) {
        f.age += dt;
        float t = f.age - f.delay;
        if (t <= 0) continue;
        double const g = f.size;
        switch (f.mode) {
        case ExitAnim::Shatter: f.vel += f.upDir * (-9.0 * g * dt); break;
        case ExitAnim::Explode: f.vel = f.vel * std::exp(-1.6 * dt) + f.upDir * (-2.0 * g * dt); break;
        case ExitAnim::Dissolve: f.vel += f.upDir * (0.6 * g * dt); break;
        case ExitAnim::Burn: f.vel += f.upDir * (1.1 * g * dt); break;
        case ExitAnim::Glitch:
            f.jitterT -= dt;
            if (f.jitterT <= 0) { // jump sideways every ~60 ms
                f.jitterT = 0.05f + 0.04f * f.seedF;
                f.seedF   = anim::hash01(int64_t(f.seedF * 1e9) + 17);
            }
            break;
        default: break;
        }
        f.pos += f.vel * dt;
        double w = f.angVel.length();
        if (w > 1e-6) f.orient = (Quat::axisAngle(f.angVel, w * dt) * f.orient).normalized();
    }
    std::erase_if(mFrags, [](Frag const& f) { return f.age - f.delay > f.life; });
}

void LetterFx::emit(Vec3 origin, std::vector<BfGlyphV1>& out) const {
    for (auto const& f : mFrags) {
        float t = std::max(0.0f, f.age - f.delay);
        float k = anim::clamp01(t / f.life);
        BfGlyphV1 q = f.tmpl;
        float a = 1, s = 1;
        Vec3  shift{};
        switch (f.mode) {
        case ExitAnim::Shatter: a = 1 - anim::easeInCubic(anim::clamp01((k - 0.45f) / 0.55f)); break;
        case ExitAnim::Explode: a = 1 - anim::easeInCubic(k); s = 1 + 0.4f * k; break;
        case ExitAnim::Dissolve: {
            a = 1 - anim::easeInOut(k);
            s = 1 - 0.6f * k;
            q.fill = mixRgba(f.baseFill, 0x00FFFFFFu | (f.baseFill & 0xFF000000u), k * 0.8f); // grains whiten as they go
            break;
        }
        case ExitAnim::Burn: {
            a = t <= 0 ? 1.0f : 1 - anim::easeInCubic(k);
            s = 1 - 0.5f * k;
            // ember: fill -> orange -> deep red, glow orange (R low byte)
            uint32_t ember = 0xFF1A8CFFu, ash = 0xFF14148Cu;
            q.fill  = k < 0.5f ? mixRgba(f.baseFill, ember, std::min(1.0f, t * 3.0f)) : mixRgba(ember, ash, (k - 0.5f) * 2.0f);
            q.glow  = 0xFF1060FFu;
            q.glowW = 0.35f;
            q.glowK = 0.9f * (1 - k);
            break;
        }
        case ExitAnim::Glitch: {
            bool on  = f.seedF > 0.18f || k < 0.3f; // flickers off now and then
            a        = on ? 1 - anim::easeInCubic(k) : 0.0f;
            double d = (double(f.seedF) * 2 - 1) * f.jitter;
            Vec3   sideways = f.screen ? Vec3{1, 0, 0} : f.right.normalized();
            shift    = sideways * d;
            q.fill   = f.seedF > 0.6f ? mixRgba(f.baseFill, 0xFFFFFF00u | 0xFF000000u, 0.6f)   // cyan cast
                     : f.seedF < 0.3f ? mixRgba(f.baseFill, 0xFFFF00FFu, 0.6f) : f.baseFill;    // magenta cast
            break;
        }
        default: break;
        }
        if (t <= 0 && f.mode != ExitAnim::Burn) a = 1;
        q.alpha *= a;
        if (q.alpha <= 0.004f) continue;
        Vec3 p = f.pos + shift - (f.screen ? Vec3{} : origin);
        Vec3 r = f.orient.rotate(f.right) * double(s), u = f.orient.rotate(f.up) * double(s);
        if (f.screen) { r.z = 0; u.z = 0; p.z = 0; }
        q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
        out.push_back(q);
    }
}

} // namespace bf::lyrics
