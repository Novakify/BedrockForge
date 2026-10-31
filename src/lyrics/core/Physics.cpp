// LyricsFlow port - [BF] falling-letter physics. Original code for Bedrock Forge; see Physics.h.
#include "lyrics/core/Physics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bf::lyrics {

namespace {
constexpr float kSub = 1.0f / 120.0f; // fixed substep

Vec3 v3(float const a[3]) { return {a[0], a[1], a[2]}; }

/// "Lying flat, text up": normal +Y, glyph up = current glyph up flattened onto the ground (keeps the heading).
Quat flatTarget(Quat const& q) {
    Vec3 up = q.rotate({0, 1, 0});
    up.y    = 0;
    if (up.length() < 1e-3) up = q.rotate({1, 0, 0}).cross({0, 1, 0}) * -1.0;
    up.y = 0;
    up   = up.length() < 1e-3 ? Vec3{0, 0, 1} : up.normalized();
    // glyph up points away from where it fell from -> readable from above
    Vec3 ez{0, 1, 0};
    Vec3 ex = up.cross(ez).normalized();
    return quatFromBasis(ex, up, ez);
}

Quat slerp(Quat a, Quat b, double t) {
    double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995) return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.normalized();
    double th = std::acos(d), s = std::sin(th);
    double ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
    return Quat{a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb}.normalized();
}
} // namespace

Quat quatFromBasis(Vec3 ex, Vec3 ey, Vec3 ez) {
    double m00 = ex.x, m01 = ey.x, m02 = ez.x, m10 = ex.y, m11 = ey.y, m12 = ez.y, m20 = ex.z, m21 = ey.z, m22 = ez.z;
    double tr = m00 + m11 + m22;
    Quat   q;
    if (tr > 0) {
        double s = std::sqrt(tr + 1.0) * 2;
        q        = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s};
    } else if (m00 > m11 && m00 > m22) {
        double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
        q        = {0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
        q        = {(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
        q        = {(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s};
    }
    return q.normalized();
}

PhysicsParams PhysicsParams::from(Config const& c) {
    PhysicsParams p;
    p.land       = c.letterLand;
    p.gravity    = std::max(0.0f, c.gravity);
    p.bounce     = std::clamp(c.bounce, 0.0f, 0.95f);
    p.friction   = std::clamp(c.friction, 0.0f, 1.0f);
    p.spin       = std::max(0.0f, c.spin);
    p.lifetimeMs = float(std::max(0, c.letterLifetimeMs));
    p.fadeMs     = float(std::max(1, c.letterFadeMs));
    p.maxLetters = std::max(0, c.maxLetters);
    return p;
}

double LetterPhysics::supportY(IWorld& w, double x, double y, double z) {
    std::vector<Aabb> boxes;
    int const         by = int(std::floor(y));
    for (int yy = by; yy >= by - 4; --yy) {
        BlockPos bp{int(std::floor(x)), yy, int(std::floor(z))};
        boxes.clear();
        w.collisionBoxes(bp, boxes);
        double lx = x - bp.x, lz = z - bp.z, best = -std::numeric_limits<double>::infinity();
        for (auto const& b : boxes)
            if (lx >= b.x0 && lx <= b.x1 && lz >= b.z0 && lz <= b.z1) {
                double top = yy + b.y1;
                if (top <= y + 0.05) best = std::max(best, top);
            }
        if (best > -1e300) return best;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void LetterPhysics::spawn(std::vector<BfGlyphV1> const& quads, Vec3 origin, int64_t seed, PhysicsParams const& prm) {
    int k = 0;
    for (auto const& q : quads) {
        if (q.flags & BF_GLYPH_SCREEN) continue; // screen-mode text has no world to fall into
        Vec3 r = v3(q.right), u = v3(q.up);
        double hw = r.length(), hh = u.length();
        if (hw < 1e-5 || hh < 1e-5) continue;
        Vec3   ex = r * (1.0 / hw), ey = u * (1.0 / hh);
        Vec3   ez = ex.cross(ey).normalized();
        ey        = ez.cross(ex).normalized();
        Letter l;
        l.tmpl       = q;
        l.tmpl.flags |= BF_GLYPH_LETTER;
        l.pos        = origin + v3(q.pos);
        l.fallStartY = l.pos.y;
        l.orient     = quatFromBasis(ex, ey, ez);
        l.halfW      = float(hw);
        l.halfH      = float(hh);
        int64_t s    = seed * 131 + k;
        float   r1 = anim::hash01(s * 7 + 1), r2 = anim::hash01(s * 7 + 2), r3 = anim::hash01(s * 7 + 3), r4 = anim::hash01(s * 7 + 4);
        if (prm.land == LetterLand::Flat) {
            // reference video: the line lets go letter by letter, each one tips over backwards (top away from you)
            // and lands where it hung, so the lyric stays readable lying on the ground
            Vec3 away = ez * -1.0;
            away.y    = 0;
            if (away.length() < 1e-3) { away = ey; away.y = 0; }
            away        = away.length() < 1e-3 ? Vec3{0, 0, 1} : away.normalized();
            double jit  = (double(r4) - 0.5) * (0.12 + 0.5 * std::min(1.0, double(prm.spin)));
            double cj = std::cos(jit), sj = std::sin(jit);
            Vec3   aw{away.x * cj - away.z * sj, 0, away.x * sj + away.z * cj};
            Vec3   fy{0, 1, 0};
            l.target    = quatFromBasis(aw.cross(fy).normalized(), aw, fy);
            l.hasTarget = true;
            l.vel       = Vec3{(r1 - 0.5) * 0.25, 0.25 + r2 * 0.35, (r3 - 0.5) * 0.25} + aw * 0.3;
            l.angVel    = {};
            l.waitMs    = 45.0f * float(k);
            if (l.waitMs > 0) l.state = Letter::State::Waiting;
        } else {
            l.vel        = {(r1 - 0.5) * 1.2, 0.4 + r2 * 1.2, (r3 - 0.5) * 1.2};
            l.angVel     = Vec3{(r4 - 0.5) * 6.0, (r1 - 0.5) * 8.0, (r3 - 0.5) * 6.0} * prm.spin;
        }
        l.restLift   = 0.003f + 0.0009f * float(k % 9);
        mLetters.push_back(l);
        ++k;
    }
    if (prm.maxLetters > 0 && int(mLetters.size()) > prm.maxLetters) // drop the oldest
        mLetters.erase(mLetters.begin(), mLetters.begin() + (int(mLetters.size()) - prm.maxLetters));
}

void LetterPhysics::substep(Letter& l, float dt, IWorld* w, PhysicsParams const& prm) {
    // integrate
    l.vel.y -= double(prm.gravity) * dt;
    l.vel = l.vel * (1.0 - 0.15 * dt); // a little air drag
    Vec3 prev = l.pos;
    l.pos += l.vel * dt;
    double av = l.angVel.length();
    if (av > 1e-6) l.orient = (Quat::axisAngle(l.angVel, av * dt) * l.orient).normalized();
    if (prm.land == LetterLand::Flat) // video A look: they tip over backwards while falling
        l.orient = slerp(l.orient, l.hasTarget ? l.target : flatTarget(l.orient), std::min(1.0, 7.0 * dt));
    if (!w) return;

    // walls: center inside a block that isn't "below us" -> bounce off horizontally
    BlockPos          bp = BlockPos::of(l.pos);
    std::vector<Aabb> boxes;
    w->collisionBoxes(bp, boxes);
    double lx = l.pos.x - bp.x, ly = l.pos.y - bp.y, lz = l.pos.z - bp.z;
    for (auto const& b : boxes)
        if (lx > b.x0 && lx < b.x1 && lz > b.z0 && lz < b.z1 && ly > b.y0 && ly < b.y1 - 0.25) {
            l.pos   = {prev.x, l.pos.y, prev.z};
            l.vel.x = -l.vel.x * prm.bounce;
            l.vel.z = -l.vel.z * prm.bounce;
            break;
        }

    // ground under the letter (its lowest point when tumbling, its centre when lying flat)
    double lowest = l.pos.y;
    if (prm.land == LetterLand::Tumble) {
        Vec3 r = l.orient.rotate({double(l.halfW), 0, 0}), u = l.orient.rotate({0, double(l.halfH), 0});
        lowest = l.pos.y - std::fabs(r.y) - std::fabs(u.y);
    }
    // query from where the letter was before this substep, so a fast fall can't tunnel through a top face
    double ground = supportY(*w, l.pos.x, std::max(prev.y, l.pos.y) + 0.02, l.pos.z);
    if (std::isnan(ground) || lowest > ground + 0.001) return;

    // contact
    l.pos.y += ground - lowest + (prm.land == LetterLand::Flat ? double(l.restLift) : 0.001);
    double impact = -l.vel.y;
    if (impact > 2.0 && prm.bounce > 0.01) {
        l.vel.y = impact * prm.bounce;
        l.vel.x *= 1.0 - prm.friction * 0.5;
        l.vel.z *= 1.0 - prm.friction * 0.5;
        l.angVel = l.angVel * 0.6;
        return;
    }
    l.vel.y = 0;
    l.vel.x *= 1.0 - prm.friction;
    l.vel.z *= 1.0 - prm.friction;
    l.angVel = l.angVel * (1.0 - prm.friction);
    double hv = std::sqrt(l.vel.x * l.vel.x + l.vel.z * l.vel.z);
    if (prm.land == LetterLand::Flat || (hv < 0.5 && l.angVel.length() < 1.5)) { // settle: lie on the top face
        l.orient = prm.land == LetterLand::Flat && l.hasTarget ? l.target : flatTarget(l.orient);
        l.pos.y  = ground + double(l.restLift);
        l.vel    = {};
        l.angVel = {};
        l.state  = Letter::State::Resting;
    }
}

void LetterPhysics::step(float dt, IWorld* w, PhysicsParams const& prm) {
    float const ms = dt * 1000.0f;
    for (auto& l : mLetters) {
        switch (l.state) {
        case Letter::State::Waiting:
            l.waitMs -= ms;
            if (l.waitMs <= 0) l.state = Letter::State::Falling;
            break;
        case Letter::State::Falling: {
            float left = std::min(dt, 0.1f);
            while (left > 1e-6f && l.state == Letter::State::Falling) {
                float h = std::min(kSub, left);
                substep(l, h, w, prm);
                left -= h;
            }
            // fell out of the world / into the void / far below where it started
            if (l.pos.y < -128.0 || l.fallStartY - l.pos.y > 48.0) { l.state = Letter::State::Fading; l.fadeMs = 0; }
            break;
        }
        case Letter::State::Resting:
            l.restMs += ms;
            if (l.restMs >= prm.lifetimeMs) l.state = Letter::State::Fading;
            break;
        case Letter::State::Fading:
            l.fadeMs += ms;
            l.alpha = std::max(0.0f, 1.0f - l.fadeMs / prm.fadeMs);
            break;
        }
    }
    std::erase_if(mLetters, [](Letter const& l) { return l.state == Letter::State::Fading && l.alpha <= 0.0f; });
}

void LetterPhysics::emit(Vec3 origin, std::vector<BfGlyphV1>& out) const {
    for (auto const& l : mLetters) {
        BfGlyphV1 q = l.tmpl;
        Vec3      p = l.pos - origin, r = l.orient.rotate({double(l.halfW), 0, 0}), u = l.orient.rotate({0, double(l.halfH), 0});
        q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
        q.right[0] = float(r.x); q.right[1] = float(r.y); q.right[2] = float(r.z);
        q.up[0] = float(u.x); q.up[1] = float(u.y); q.up[2] = float(u.z);
        q.alpha *= l.alpha;
        if (q.alpha > 0.004f) out.push_back(q);
    }
}

} // namespace bf::lyrics
