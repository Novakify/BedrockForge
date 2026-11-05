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
    return m == ExitAnim::Shatter || m == ExitAnim::Dissolve || m == ExitAnim::Explode || m == ExitAnim::Burn || m == ExitAnim::Glitch ||
           m == ExitAnim::Vortex;
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
    case ExitAnim::Shatter: gx = 2; gy = 3; break; // [BF] fewer, bigger shards read as glass; 3x3 + trails read as confetti in game
    case ExitAnim::Dissolve: gx = 6; gy = 6; break;
    case ExitAnim::Burn: gx = 4; gy = 5; break;
    case ExitAnim::Glitch: gx = 1; gy = 5; break;
    case ExitAnim::Vortex: gx = 2; gy = 2; break;
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
                    // glass: a small pop, then the pieces rain down (Explode is the one that flies everywhere)
                    // [BF] audit: fast random tumbling turned every shard edge-on into a white sliver (confetti).
                    // Glass keeps its face to you: a gentle pop, a slow tilt in the letter's own plane, then it falls.
                    f.vel    = (out * (0.35 + 0.45 * r3) + upDir * (0.5 + 0.5 * r3) + normal * (0.15 * r1)) *
                               (letterH * std::sqrt(intensity));
                    f.angVel = (normal * r1 + side * (0.35 * r2)) * (1.2 + 1.2 * r3);
                    f.life   = 1.1f + 0.35f * float(r3);
                    f.delay  = 0.02f * float(qi);
                    break;
                case ExitAnim::Explode:
                    // outwards and a little away from you (letters flying into the camera filled the screen)
                    f.vel    = (out * (5.0 + 3.0 * r3) + normal * (0.9 * r1 - 0.7) + upDir * (1.0 + r2)) * (letterH * intensity);
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
                case ExitAnim::Vortex: // [LyricsForge] gravity vortex: the word spirals into a point and is swallowed
                    f.center = c + upDir * (letterH * 0.25);
                    f.axis   = screen ? Vec3{0, 0, 1} : normal;
                    f.r0     = f.pos - f.center;
                    f.angVel = f.axis * (4.0 + 5.0 * r3);
                    f.life   = 1.15f + 0.35f * float(r3);
                    f.delay  = 0.03f * float(qi);
                    f.jitter = float(intensity);
                    break;
                case ExitAnim::Glitch:
                    // [BF] audit: every slice of every letter hopping on its own scattered the word into confetti.
                    // A real glitch tears whole rows of the word sideways together, briefly, then the word cuts out.
                    f.vel      = Vec3{};
                    f.life     = 0.5f;
                    f.jitter   = float(letterH) * 0.32f * std::min(intensity, 1.2f);
                    f.wordSeed = seed;
                    f.row      = j;
                    break;
                default: break;
                }
                mFrags.push_back(f);
            }
        ++qi;
    }
    if (mFrags.size() > kMaxFrags) // [BF] full: the oldest pieces (nearly gone anyway) make room, new words never vanish
        mFrags.erase(mFrags.begin(), mFrags.begin() + std::ptrdiff_t(mFrags.size() - kMaxFrags));
}

void LetterFx::step(float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    for (auto& f : mFrags) {
        f.age += dt;
        float t = f.age - f.delay;
        if (t <= 0) continue;
        double const g = f.size;
        switch (f.mode) {
        case ExitAnim::Shatter: f.vel += f.upDir * (-7.5 * g * dt); break;
        case ExitAnim::Explode: f.vel = f.vel * std::exp(-1.6 * dt) + f.upDir * (-2.0 * g * dt); break;
        case ExitAnim::Dissolve: f.vel += f.upDir * (0.6 * g * dt); break;
        case ExitAnim::Burn: f.vel += f.upDir * (1.1 * g * dt); break;
        case ExitAnim::Vortex: {
            float const k  = anim::clamp01(t / f.life);
            double const th = double(k * k) * (7.0 + 5.0 * f.seedF) * f.jitter;
            double const rr = 1.0 - anim::easeInCubic(k) * 0.97;
            f.pos = f.center + Quat::axisAngle(f.axis, th).rotate(f.r0) * rr;
            f.vel = Vec3{};
            break;
        }
        case ExitAnim::Glitch:
            f.jitterT -= dt;
            if (f.jitterT <= 0) { // jump sideways every ~60 ms
                f.jitterT = 0.05f + 0.04f * f.seedF;
                f.seedF   = anim::hash01(int64_t(f.seedF * 1e9) + 17);
            }
            break;
        default: break;
        }
        if (!f.hasTrail) { f.trail[0] = f.trail[1] = f.pos; f.hasTrail = true; }
        f.trailT += dt;
        if (f.trailT >= 0.04f) { f.trailT = 0; f.trail[1] = f.trail[0]; f.trail[0] = f.pos; }
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
        case ExitAnim::Shatter:
            a = 1 - anim::easeInOut(anim::clamp01((k - 0.2f) / 0.8f));
            q.glowK = std::max(q.glowK, 0.35f * (1 - k)); // a little light caught in each piece
            break;
        case ExitAnim::Explode: a = 1 - anim::easeInCubic(k); s = 1 + 0.4f * k; break;
        case ExitAnim::Vortex: {
            a = 1 - anim::easeInCubic(anim::clamp01((k - 0.55f) / 0.45f));
            s = 1 - 0.75f * anim::easeInCubic(k);
            q.fill  = mixRgba(f.baseFill, 0xFFFFD8E8u, k * 0.5f);   // pulled light: a little brighter as it falls in
            q.glowK = std::max(q.glowK, 0.6f * k);
            break;
        }
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
            int64_t const step = int64_t(t / 0.06f);
            float const h  = float(h01(f.wordSeed, int(step * 8 + f.row)));      // same for the whole row of the word
            float const hw = float(h01(f.wordSeed, int(step * 8 + 7) + 9001));   // same for the whole word
            bool const torn = h > 0.55f;                                         // only some rows tear each step
            bool on  = hw > 0.15f || k < 0.3f;                                   // the word cuts out now and then
            a        = on ? 1 - anim::easeInCubic(k) : 0.0f;
            double d = torn ? (double(h) * 2 - 1) * f.jitter * (0.4 + 0.6 * double(k)) : 0.0;
            float const cast = h;
            Vec3   sideways = f.screen ? Vec3{1, 0, 0} : f.right.normalized();
            shift    = sideways * d;
            q.fill   = !torn ? f.baseFill
                     : cast > 0.8f ? mixRgba(f.baseFill, 0xFFFFFF00u | 0xFF000000u, 0.45f)    // cyan cast on a torn row
                                   : mixRgba(f.baseFill, 0xFFFF00FFu, 0.45f);                   // magenta cast
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
        // [LyricsForge] motion trail: fast pieces leave two fading ghosts behind them (drawn first = underneath)
        bool const trails = f.hasTrail && t > 0 && (f.mode == ExitAnim::Explode || f.mode == ExitAnim::Vortex);
        if (trails && (f.trail[1] - f.pos).length() > double(f.size) * 0.15) {
            for (int k = 1; k >= 0; --k) {
                BfGlyphV1 g = q;
                Vec3 const tp = f.trail[k] + shift - (f.screen ? Vec3{} : origin);
                g.pos[0] = float(tp.x); g.pos[1] = float(tp.y); g.pos[2] = float(f.screen ? 0.0 : tp.z);
                g.alpha  = q.alpha * (k == 0 ? 0.32f : 0.14f);
                g.glowK  = std::max(g.glowK, 0.4f);
                out.push_back(g);
            }
        }
        out.push_back(q);
    }
}

} // namespace bf::lyrics
